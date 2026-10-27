# Measurement Nano USB firmware

This replaces the firmware on the **main ATmega328P measurement Nano**, the board wired to the HX711 and SD module. It implements the [gateway USB protocol](../ESP32_USB_Gateway/PROTOCOL.md), phone calibration, and an independent SD backup. Leave the separate FrSky S.Port telemetry Nano alone. Original sketches elsewhere in the repository are preserved.

The ESP32-S3 Zero is the USB host and Wi-Fi dashboard. This Nano remains responsible for sensor acquisition, calibration and EEPROM. There are no propulsion-control outputs.

## Build and upload

Requirements: Python 3, Git, PowerShell, and internet for the first dependency installation. From this directory:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
powershell -ExecutionPolicy Bypass -File .\flash.ps1 -Port COM6 -Bootloader Old
```

Use the measurement Nano's actual COM port. COM6/CH340 was the board identified during restoration; COM assignments can change. Connect it directly to the computer for programming. The helper first backs up flash and all 1,024 EEPROM bytes, checks the ATmega328P signature, writes and verifies application flash, and compares EEPROM before/after. It preserves the bootloader and never writes fuses or EEPROM. Backups are saved under ignored `artifacts/backups/`; retain them separately before deleting build artifacts.

The inspected board uses the old Nano bootloader at **57600 upload baud**. `-Bootloader New` uses 115200 upload baud for a board with the newer bootloader. This choice does not change the runtime baud: **115200, 8-N-1** for both. The firmware HEX works with either bootloader. The build creates `artifacts/thrust-tester-measurement-nano.zip` containing the HEX, checksums, guide and flash helper. An extracted package needs AVR tools already installed or explicit `-Avrdude` / `-AvrdudeConfig` paths.

Pinned dependencies: PlatformIO AVR platform `5.1.0`, ArduinoJson `6.21.5`, Arduino SD `1.3.0`; helper PlatformIO Core `6.1.18`. The HX711 and bounded master-only I2C routines are implemented here, so historical HX711/Adafruit library versions are not needed. `pio run -e nanoatmega328` also builds using an existing PlatformIO installation.

## Connect to the gateway

1. Disconnect the Nano from the computer after upload. Leave the load cell unloaded: startup takes 20 fresh HX711 readings to establish zero.
2. Supply the Zero through its 5V/VBUS and GND pins from a verified regulated 5V source. Keep its USB disconnected from the computer while that external feed is attached; the Zero ties USB VBUS to its supply input.
3. Connect the Nano's Mini-USB to the Zero's USB-C using a data cable and suitable host adapter. The Nano's USB converter carries data; no 3.3V/5V UART level shifter is needed. Confirm the actual power arrangement against the [gateway hardware notes](../ESP32_USB_Gateway/README.md#hardware-and-usb-power).
4. Join **ThrustTester**, password **ThrustRig-2026**, and open **http://192.168.4.1**. In **Device & connection**, change Nano serial speed to **115200** and save/restart. On a phone, swipe the top navigation tabs left to reveal that page. The gateway's original default is 9600.
5. Expect protocol v1, live readings and enabled calibration controls once discovery finishes. Check each sensor with known inputs before propulsion tests.

## Existing wiring

| Connection | Nano pin/address |
| --- | --- |
| HX711 DOUT / CLK | D3 / D2 |
| ADS1115 shunt / voltage | AIN0 / AIN1, address `0x48` |
| I2C SDA / SCL | A4 / A5, 100 kHz |
| Hall input | D8, counts both transitions |
| Optional receiver throttle | D9, 1000–2000 µs maps to 0–100% |
| SD CS / MOSI / MISO / SCK | D4 / D11 / D12 / D13 |
| Optional old telemetry Nano | I2C address `0x08` |

D5/D6 receiver calibration inputs are ignored; a missing receiver cannot adjust settings. Throttle is `null` after 100 ms without a valid pulse. The optional telemetry board receives the original nine-byte packet; a missing board is nonfatal. Hall RPM uses the actual elapsed measurement interval and the configured transitions per revolution.

ADS1115 single-ended conversions explicitly select ±6.144V gain and **250 SPS**, using 187.5 µV/count. The two channels are converted sequentially; this is not 250 complete voltage/current pairs per second. This is a conversion setting, not permission to exceed ADC supply/input limits. Unavailable ADC channels are `null`. Sensors still need reference calibration, including electrical offsets.

## Fast acquisition

The Nano emits **one sample per fresh HX711 conversion**, with no fixed 100 ms gate or measurement averaging. No thrust frames are sent until startup tare succeeds. A missing/stalled HX711 produces no new frames. ADC values are the latest available pair; RPM is calculated independently over approximately 100 ms using the actual elapsed interval. Optional FrSky forwarding remains approximately 10 Hz, keeping that traffic out of the thrust sampling rate.

The SparkFun HX711's underside **RATE** trace jumper selects nominal 10 SPS when closed and 80 SPS when open. The photographed board's jumper was opened by the owner. Actual fresh-reading cadence in the subsequent USB/SD bench test was **98.7 samples/second**, with a maximum 18 ms interval during the 20-second steady capture. This is the observed rate of this board, not a guaranteed HX711 specification. A 0.6-second event would contain about 59 samples at that measured cadence. See the [SparkFun schematic](https://cdn.sparkfun.com/datasheets/Sensors/ForceFlex/SparkFun_HX711_Load_Cell_v11.pdf).

Measurements use compact newline ASCII at **115200 baud**:

```text
S1,sequence,nano_time_ms,thrust,throttle,voltage,current,rpm
S1,11734,119348,0.00019,,-0.00640,-1.95312,0.0
```

Empty optional channels mean unavailable; the thrust value and timestamp must be present. These frames averaged 48 bytes on the bench, leaving serial bandwidth for commands. Calibration, discovery, settings and acknowledgements remain JSON v1. Use **gateway firmware 1.1.0 or newer**; the original 1.0.0 gateway cannot decode S1. No baud change is required from the previous paired test.

USB and SD share one numeric formatting pass to reduce Nano CPU time. `sample_hz` and `max_sample_gap_ms` in configuration report conversion cadence and the largest observed interval since startup, including command handling. SD writes and commands can introduce timing gaps; timestamps preserve actual intervals. No claim of hard real-time or noise-free acquisition is made. Preserve raw measurements and choose filtering after capture to avoid flattening a short pulse.

## Calibration and persistence

The phone can tare, calibrate against a known load in kgf equivalent, edit the factor/pole count/shunt/divider, and explicitly save EEPROM. Tare and known-load calibration each average 20 fresh HX711 readings with a 3.5-second timeout. Calibration rejects a reference that is too small or unstable. It changes RAM settings first; verify the result before **Save EEPROM**. Tare is re-established on every reset and is not stored.

Configuration bounds: factor magnitude 1–10,000,000; shunt 1e-9–1 ohm; divider 1–1000; pole count 1–220. Bounds only prevent invalid arithmetic; verify values for the installed sensors. The shunt/divider defaults are `0.000192` ohm and `34.13`.

On first use, valid legacy factor bytes 0–1 and pole byte 2 are imported. The inspected Nano contained **100340** and **6**; these were retained. `persisted:false` means no modern saved record exists yet, even when the imported legacy factor is intact. Saving writes a versioned, CRC-protected 22-byte record at EEPROM address **32**, leaving legacy bytes 0–2 intact. On later boots a valid modern record takes precedence. Invalid records fall back to valid legacy fields or source defaults.

Commands use newline-delimited JSON, positive request IDs, and acknowledgements. The Nano accepts up to **223 characters per command** excluding CR/LF; oversized lines are discarded to the next newline. The last completed request is cached to avoid duplicate application within a USB session. Hello resets that cache and cancels an outstanding user calibration. Do not send multiple user commands concurrently.

## Two recording locations

**ESP32 flash is the phone's primary run library.** Start/stop recording there, download CSV and metadata to the phone, and export before storage fills. This requires no SD card in the Zero.

**The Nano SD card is a continuous parallel backup.** On each boot it creates the next unused `RIG00001.CSV`, `RIG00002.CSV`, etc. Every valid measurement is logged, including idle values and with no receiver present. Existing files and historical `datalog.txt` are retained. The firmware does not format cards. A missing/unwritable card reports an event and USB measurements continue.

```csv
Time,Thrust,Throttle,Vbat,Current,RPM
```

`Time` is Nano uptime in seconds with millisecond precision, matching the gateway CSV's `nano_time_s`. Empty cells mean unavailable values. Configuration messages include `sd_ok` and `sd_file`; the gateway stores the configuration snapshot in run metadata so a phone run can be matched to its Nano backup. Nano files span all activity since boot; one Nano file can cover several phone runs. The updated loggers flush approximately once per second, and on explicit gateway stop, so up to about one second of buffered data can be lost on power failure. A filename collision is skipped to the next unused name; existing files are never opened for append as a new run.

SD initialization runs only at Nano startup. After reseating/replacing a card, reset or power-cycle the measurement Nano with the load cell unloaded; cable reconnection alone may leave it powered by the rig and will not retry initialization. Removing an active card is not a supported logging workflow.

There is no SD file browsing, copying or automatic synchronization over USB in this version. Remove the Nano card to retrieve its backup files. The phone can open those six-column CSV files locally.

## Verification

See [HARDWARE_TEST.md](HARDWARE_TEST.md) for measured results and outstanding checks.

Native configuration/math tests (requires `ziglang==0.14.1`):

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\run-config-tests.ps1 -Python python
```

Read-only serial protocol smoke test (requires `pyserial`; USB opening may reset the Nano and perform startup tare):

```powershell
python .\tests\serial_smoke.py --port COM6 --seconds 20 --output .\artifacts\serial-smoke.json
```

The smoke test sends only discovery/read requests and deliberately invalid mutations; it does not save EEPROM or apply a calibration. It checks S1/JSON framing, rate, maximum gap, sequence continuity and unchanged configuration. It discards an initial partial line when attaching to an existing stream. Default minimum rate is 65 Hz; use `--min-rate 5 --max-gap-ms 250` for a board still configured at 10 Hz. Known-load, electrical/RPM accuracy, independent SD file retrieval and power-loss recovery require physical checks.
