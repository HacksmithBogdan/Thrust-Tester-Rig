"""Read-only protocol checks on a connected measurement Nano (requires pyserial).

Invalid calibration/configuration requests are intentional; this does not tare,
apply a calibration, or write EEPROM. Opening USB may reset a classic Nano,
whose normal startup performs an unloaded tare.
"""
import argparse
import json
import math
import statistics
import time
from pathlib import Path

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    rows, malformed = [], []
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
    port.dtr = False
    port.rts = False
    port.port = args.port
    port.open()

    def receive(seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            line = port.readline().strip()
            if not line:
                continue
            try:
                row = json.loads(line)
                assert row["protocol"] == 1
                rows.append(row)
            except (ValueError, KeyError, AssertionError):
                malformed.append(line.decode("ascii", errors="replace"))

    def command(request_id, name, values=None):
        message = {"type": "command", "protocol": 1, "request_id": request_id,
                   "command": name, "args": values or {}}
        port.write(json.dumps(message, separators=(",", ":")).encode() + b"\n")
        port.flush()
        receive(0.6)
        matches = [r for r in rows if r.get("type") == "ack" and r.get("request_id") == request_id]
        return matches[-1] if matches else None

    try:
        receive(4.5)  # Allow boot tare if this USB adapter resets the Nano.
        command(0, "hello")
        assert any(r.get("type") == "hello" for r in rows), "No hello response"
        assert command(101, "get_config")["ok"] is True
        before = [r for r in rows if r.get("type") == "config"][-1]
        assert before["tare_ready"] is True, "Startup load-cell tare did not finish"
        assert command(102, "calibrate", {"known_load": 0})["ok"] is False
        invalid = {"calibration_factor": before["calibration_factor"], "poles": 0,
                   "shunt_ohms": before["shunt_ohms"], "divider_ratio": before["divider_ratio"]}
        rejected = command(103, "set_config", invalid)
        assert rejected["ok"] is False
        assert command(103, "set_config", invalid) == rejected, "Duplicate ACK mismatch"
        assert command(104, "unknown_command")["ok"] is False
        port.write(b"x" * 250 + b"\n")
        receive(0.4)
        assert command(105, "get_config")["ok"] is True, "Oversized-line recovery failed"
        after = [r for r in rows if r.get("type") == "config"][-1]
        for key in ("calibration_factor", "poles", "shunt_ohms", "divider_ratio", "persisted"):
            assert before[key] == after[key], f"Invalid request changed {key}"
        receive(5)
        samples = [r for r in rows if r.get("type") == "sample"]
        assert len(samples) >= 50, f"Only {len(samples)} samples received"
        intervals = []
        for previous, current in zip(samples, samples[1:]):
            assert current["seq"] == previous["seq"] + 1, "Sequence discontinuity/reset"
            delta = (current["time_ms"] - previous["time_ms"]) & 0xffffffff
            assert 90 <= delta <= 500, f"Unexpected sample interval: {delta} ms"
            intervals.append(delta)
        for row in samples:
            assert math.isfinite(row["thrust"])
            for key in ("throttle", "voltage", "current", "rpm"):
                assert row[key] is None or math.isfinite(row[key])
        assert not malformed, f"Malformed lines: {malformed[:3]}"
        report = {"port": args.port, "baud": 115200, "passed": True,
                  "samples": len(samples), "mean_interval_ms": statistics.mean(intervals),
                  "max_interval_ms": max(intervals), "config": after,
                  "latest_sample": samples[-1], "malformed_lines": len(malformed),
                  "checks": ["hello/config", "invalid calibration/config rejection",
                             "duplicate ACK", "unknown command", "oversized-line recovery",
                             "unchanged configuration", "finite samples", "sample continuity"],
                  "frames": rows}
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({k: v for k, v in report.items() if k != "frames"}, indent=2))
    finally:
        port.close()


if __name__ == "__main__":
    main()
