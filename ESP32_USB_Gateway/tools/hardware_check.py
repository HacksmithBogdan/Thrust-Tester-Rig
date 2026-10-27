"""Record a short real bench run on a connected gateway; never changes calibration.

Requires an idle gateway with live Nano data. Leaves its clearly named recording
on the gateway and downloads CSV/metadata locally. No simulated data is used.
"""
import argparse
import csv
import io
import json
import statistics
import time
import urllib.request
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="http://192.168.4.1")
    parser.add_argument("--seconds", type=float, default=15)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    client = urllib.request.build_opener(urllib.request.ProxyHandler({}))

    def request(path, data=None, raw=False):
        body = None if data is None else json.dumps(data).encode()
        req = urllib.request.Request(args.url + path, data=body,
                                     headers={} if body is None else {"Content-Type": "application/json"})
        with client.open(req, timeout=8) as reply:
            content = reply.read().decode("utf-8")
        return content if raw else json.loads(content)

    before = request("/api/status")
    assert before["live"] and before["mode"] == "protocol_v1", "No live protocol-v1 Nano"
    assert not before["recording"]["active"], "An existing run is active; do not interrupt it"
    assert before["command"]["state"] != "pending", "A Nano command is in progress"
    assert "nano_seq" in before["sample"], "Not receiving compact S1 measurements"
    assert before["sample_rate_hz"] >= 65, "Expected the HX711 fast-rate configuration"
    request("/api/command", {"command": "get_config", "args": {}})
    deadline = time.monotonic() + 3
    while True:
        current = request("/api/status")
        if current["command"]["state"] != "pending":
            assert current["command"]["state"] == "success", current["command"]
            break
        assert time.monotonic() < deadline, "Configuration read timed out"
        time.sleep(0.1)
    request("/api/record", {"action": "start", "name": "High-rate USB bench check",
                            "note": "Real idle sensor data; no propulsion. Firmware verification.",
                            "epoch_ms": int(time.time() * 1000)})
    run_id = request("/api/status")["recording"]["id"]
    assert run_id
    samples = []
    after = before["sample"]["seq"]
    try:
        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline:
            batch = request(f"/api/samples?after={after}")
            assert not batch["gap"], "Browser/API history overran"
            after = batch["next_seq"]
            samples.extend(batch["samples"])
            time.sleep(0.2)
    finally:
        latest = request("/api/status")
        if latest["recording"]["active"] and latest["recording"]["id"] == run_id:
            request("/api/record", {"action": "stop"})
    csv_text = request(f"/api/download?id={run_id}", raw=True)
    metadata = request(f"/api/download?id={run_id}&format=json")
    rows = list(csv.DictReader(io.StringIO(csv_text)))
    (args.output / f"{run_id}.csv").write_text(csv_text, encoding="utf-8")
    (args.output / f"{run_id}.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    assert metadata["samples"] == len(rows) and len(rows) > 100, "CSV/metadata count mismatch"
    assert metadata["status"] == "complete", metadata["status"]
    intervals = []
    for previous, current in zip(rows, rows[1:]):
        assert (int(current["nano_sequence"]) - int(previous["nano_sequence"])) & 0xffffffff == 1, "Missing Nano sequence in recording"
        delta = float(current["nano_time_s"]) - float(previous["nano_time_s"])
        assert 0 < delta <= 0.1, f"Acquisition gap of {delta} seconds"
        intervals.append(delta)
    rate = 1 / statistics.mean(intervals)
    assert rate >= 65, f"Recorded rate is only {rate:.2f} Hz"
    after_status = request("/api/status")
    assert after_status["dropped"] == before["dropped"], "USB queue drops during recording"
    assert after_status["nano_sequence_gaps"] == before["nano_sequence_gaps"], "Nano gaps during recording"
    report = {"passed": True, "id": run_id, "csv_rows": len(rows), "measured_hz": rate,
              "max_interval_ms": max(intervals) * 1000, "api_samples": len(samples),
              "nano_config": metadata.get("nano_config"), "status": after_status}
    (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
