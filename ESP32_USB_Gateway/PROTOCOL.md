# Nano ↔ ESP32 USB protocol, version 1

In Nano USB mode the ESP32 is the USB host. Fresh installations start in PC service mode; select Nano USB on the Device page before connecting the Nano. The Nano remains the measurement controller, accessed through its existing USB-to-UART converter. No Nano changes are included in this implementation; this document is the contract for the second step.

## Transport

- USB serial, 8 data bits, no parity, 1 stop bit.
- Initial gateway default: **9600 baud**, matching the version 4 sketch. The Schubler calibration variant uses 115200. Set the gateway to match the installed firmware.
- Recommended rate for updated Nano firmware: **115200 baud**, selected on both devices. No automatic baud switching.
- One UTF-8 JSON object per line, terminated by LF; CRLF is accepted. Maximum received line: **767 bytes**, excluding CR/LF. Use ASCII field names.
- Each line must be complete; a USB packet may contain partial lines or multiple lines. Oversized lines are discarded through the next LF. Queue overflow forces resynchronization and increments the diagnostic drop counter.
- Measurements should be sent at the established sampling rate, independently of whether a phone is connected. Do not mix unframed human diagnostics into a sample JSON line.
- Numbers must be finite. Use `null` for unavailable channels. Do not emit JSON `NaN` or `Infinity`.
- Unknown fields can be ignored. The main logger owns calibration, validation, units, and EEPROM persistence.

## Existing firmware support

Without implementing JSON, the gateway accepts the exact debug labels already in the archive:

```text
Time: 12.30	Thrust: 4.56	Throttle: 70.0	VBat: 46.80	I: 85.20	RPM: 18000.0	Trim: 0	TrainerSW: 1500	nPoles: 20
```

Here `\t` represents an actual tab; whitespace variations and label capitalization are accepted. Required fields are `Time`, `Thrust`, `Throttle`, `VBat`, `I`, and `RPM`. Optional trim/trainer/pole fields are retained in the raw monitor but are not treated as confirmed configuration. Startup messages are displayed in the raw monitor and do not become measurements.

The six-number CSV layout `time,thrust,throttle,voltage,current,rpm` is also accepted as a live stream. Headers are ignored. The existing `#define DEBUG 1` must be enabled to obtain debug measurements; the ESP32 cannot enable a compile-time Nano flag remotely.

Legacy mode is read-only for Nano configuration. ESP32 recording, file viewing and download still work. Gateway recordings are separate from the Nano's SD files.

## Discovery and capabilities

About 2.5 seconds after opening a USB converter, the gateway sends:

```json
{"type":"command","protocol":1,"request_id":0,"command":"hello","args":{}}
```

This changes no measurement settings. The old version 4 firmware ignores input. Updated firmware should answer with a hello and current configuration. It should also send these messages on startup, since a Nano reset can occur during USB enumeration.

```json
{"type":"hello","protocol":1,"device":"Thrust measurement Nano","capabilities":["tare","calibrate","set_config","get_config","save_config"]}
```

Advertise **only implemented commands**. The gateway disables controls until their exact capability is present and clears capabilities on USB disconnect. A sample alone does not enable commands. Unsupported protocol versions do not enable them.

## Measurements: Nano → gateway

```json
{"type":"sample","protocol":1,"seq":123,"time_ms":12300,"thrust":4.56,"throttle":70,"voltage":46.8,"current":85.2,"rpm":18000}
```

| Field | Meaning |
| --- | --- |
| `seq` | Optional Nano sample counter; reserved for future gap diagnostics. The gateway currently assigns its own receipt sequence. |
| `time_ms` | Required finite elapsed Nano time in milliseconds. Preserve timing independently of serial/USB/UI delays. |
| `thrust` | Required finite calibrated force reading. The operator selects the matching display/recording unit on the gateway. |
| `throttle` | Receiver throttle percentage, or `null` if the receiver/input is absent or stale. |
| `voltage` | Supply volts, or `null` when unavailable. |
| `current` | Amperes, or `null` when unavailable. |
| `rpm` | Mechanical RPM, or `null` when unavailable. |

The gateway calculates watts as volts × amperes only when both are available. It timestamps receipt using its own monotonic clock, stores up to 600 recent samples in RAM, and marks the stream stale after three seconds without a new sample. It never synthesizes zero readings for missing data.

## Commands: gateway → Nano

```json
{"type":"command","protocol":1,"request_id":17,"command":"tare","args":{}}
```

Request IDs are positive integers, increasing per ESP32 boot. ID zero is reserved for discovery. At most one user command is outstanding. The gateway displays success only after a matching acknowledgement; otherwise it times out after five seconds. The Nano should avoid applying a duplicated request twice during the same connection.

| Command | Arguments | Nano behavior |
| --- | --- | --- |
| `hello` | `{}` | Send hello and configuration. No calibration changes. |
| `get_config` | `{}` | Send current configuration, then acknowledgement. |
| `tare` | `{}` | Establish the unloaded zero using the load-cell interface. Send acknowledgement after completion. |
| `calibrate` | `{"known_load":10}` | With a known load already applied after taring, calculate and apply the scale factor. Reject zero, invalid, or unstable reference readings. Send updated configuration and acknowledgement. |
| `set_config` | See example below | Validate the entire configuration before applying it. Send updated configuration and acknowledgement. |
| `save_config` | `{}` | Persist validated current settings to EEPROM, then send configuration with `persisted:true` and acknowledgement. |

```json
{"type":"command","protocol":1,"request_id":18,"command":"set_config","args":{"calibration_factor":30310,"poles":20,"shunt_ohms":0.000192,"divider_ratio":34.13}}
```

The gateway checks pole count 1–220, nonzero scale factor with magnitude ≤10,000,000, shunt resistance >0 and ≤1 ohm, and divider ratio 1–1000. These are parsing bounds, not hardware ratings. The Nano must impose its own hardware-appropriate bounds. A new known-load calibration does not automatically save EEPROM. Tare is not automatically persisted.

The gateway rejects calibration commands during an active recording. Remove or disable the old trim/trainer calibration path when implementing this protocol, especially with an absent receiver.

## Configuration: Nano → gateway

```json
{"type":"config","protocol":1,"calibration_factor":30310,"poles":20,"shunt_ohms":0.000192,"divider_ratio":34.13,"thrust_unit":"kgf","persisted":true}
```

Send this after hello, `get_config`, and any settings change. `persisted` indicates whether current settings match EEPROM. Calibration controls populate from this snapshot, not historical constants in the repository. Configure the gateway's display unit to match `thrust_unit`; changing the display label does not convert the readings or recalibrate the Nano.

## Acknowledgements and errors

```json
{"type":"ack","protocol":1,"request_id":18,"ok":true,"message":"Measurement settings applied; EEPROM unchanged"}
```

```json
{"type":"ack","protocol":1,"request_id":18,"ok":false,"message":"Pole count must be greater than zero"}
```

Only the matching outstanding request changes the gateway's command result. `ok` must be a JSON Boolean. On error, retain the prior valid configuration. For asynchronous hardware errors, send:

```json
{"type":"event","protocol":1,"message":"SD card not available; live measurements continue"}
```

There are no motor, engine, ESC, or emergency-stop commands in this protocol.

## Gateway recording format

Recordings are stored on the LCD board’s microSD card under `/thrust/`, each with a CSV and JSON file. Startup and normal remount do not format the card.

CSV columns:

```csv
gateway_time_s,nano_time_s,thrust,throttle_percent,voltage_V,current_A,rpm,power_W
```

Gateway time is monotonic ESP32 uptime; Nano time can restart independently. Empty CSV cells represent unavailable channels. Metadata JSON stores the run name, notes, selected thrust unit, source format, wall-clock start time (phone-supplied, or the LCD board’s session clock when available; otherwise zero), final status, sample count, and available Nano configuration. The phone's clock provides a label and is not a measurement timebase.

Gateway recording is explicit start/stop and records all received samples, including idle samples. It does not inherit the Nano SD logger's activity threshold. Logging continues across phone disconnects and USB reconnections; gaps are visible through gateway timestamps. A power interruption ends the run and marks its metadata `interrupted` on the next boot; up to the most recent flush interval may be lost. Data is flushed every ten samples.

## Local HTTP API

The dashboard uses these same-origin routes on port 80:

| Route | Function |
| --- | --- |
| `GET /api/status` | USB/stream health, latest sample, public settings, recording/storage state, capabilities, configuration and command result. Includes LCD/touch readiness, touch count and microSD health. No Wi-Fi password is returned. |
| `GET /api/samples?after=N` | Up to 64 samples after gateway sequence N, `next_seq`, and RAM-history gap flag. |
| `GET /api/console?after=N` | Recent raw serial lines after console sequence N. |
| `GET /api/runs` | Up to 64 run metadata records, including CSV size. |
| `GET /api/download?id=RUN` | Download completed run CSV. Add `&format=json` for metadata. |
| `POST /api/record` | `{"action":"start","name":"Test","note":"...","epoch_ms":123}` or `{"action":"stop"}`. |
| `POST /api/delete` | `{"id":"run-..."}`; active runs cannot be deleted. |
| `POST /api/command` | `{"command":"tare","args":{}}`; capability-gated, returns request ID with HTTP 202. |
| `POST /api/settings` | SSID, optional replacement password, IP, baud, unit, and Boolean `usb_host` (`true` for Nano host, `false` for PC service); saves then restarts. |

Errors return non-2xx HTTP status and `{"error":"..."}`. Downloads require the run to be stopped. The network is a standalone WPA2 access point; anyone with its Wi-Fi password can use the dashboard and commands. There is no separate web login or internet dependency. No access to old Nano SD files is implemented in this version.
