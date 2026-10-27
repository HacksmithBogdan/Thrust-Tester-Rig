# Measurement Nano verification

## 2026-10-27: direct USB bench test

- Identified the measurement board on COM6: CH340 `1A86:7523`, ATmega328P signature `0x1e950f`, old bootloader at 57600 baud. Its original debug output contained thrust readings; its flash contained the logger/SD code.
- Saved original application/bootloader flash and all 1,024 EEPROM bytes locally before writing. Device backups and serial captures are ignored by Git.
- Imported the existing legacy factor **100340** and pole count **6**. All 1,024 EEPROM bytes compared equal before/after programming; no EEPROM save command was issued.
- Built application: **28,046 / 30,720 flash bytes**, **1,513 / 2,048 static SRAM bytes**. Runtime stack/heap share the remaining SRAM; sustained operation with a working SD card still needs verification.
- Avrdude wrote and verified all 28,046 application bytes on the identified board.
- Native tests passed for legacy import/fallback, configuration bounds, EEPROM CRC, ADC gain bits, actual-window RPM, stale PWM and timer rollover.
- Direct serial smoke test at **115200 baud** received **149 consecutive samples**, every **100 ms**, with no malformed JSON. Startup tare completed and samples/configuration were decoded.
- Hello/get_config, rejection of zero-load calibration and zero-pole settings, duplicate acknowledgement, unsupported-command rejection and oversized-line recovery passed. Configuration was unchanged after rejected requests.
- The receiver was absent: throttle was correctly `null`. RPM at rest was zero. Thrust was near unloaded zero. ADC values were received, but voltage/current offsets and accuracy were not calibrated in this test.
- **SD initialization failed** (`sd_ok:false`, no filename). Continuous Nano SD backup is implemented but has not been validated on this hardware. No card was erased or formatted.

The [paired ESP32-to-Nano USB link was subsequently verified](../ESP32_USB_Gateway/HARDWARE_TEST.md#paired-usb-link-on-2026-10-27) at 115200 baud, including live protocol-v1 samples/configuration and an observed tare acknowledgement. End-to-end recording/download, known-load calibration, EEPROM save/reload, electrical calibration, RPM accuracy, working SD logging and power-interruption behavior remain to be checked. These tests do not establish measurement accuracy or propulsion-test readiness.

## 2026-10-27: RATE jumper opened and fast firmware

- The owner identified the installed SparkFun HX711 board in a photo, opened its RATE jumper and reconnected the measurement Nano to the computer.
- Before the update the Nano reported factor **100340**, poles **6** and `persisted:false`. These are the values available on the controller; a different unsaved phone calibration could not be recovered after its previous reset.
- The card now initialized, but the earlier filename-selection code stopped on an existing filename. Replaced directory-name inference with direct checks that advance until an unused `RIGnnnnn.CSV` is found; old files remain untouched.
- New application built at **27,808 / 30,720 flash bytes** and **1,540 / 2,048 static SRAM bytes**. It was backed up, flashed and verified on the same ATmega328P/COM6. All 1,024 EEPROM bytes again compared equal before/after upload.
- Fresh-conversion output uses compact S1 frames at the existing **115200 baud**. Numeric values are formatted once for USB and SD. ADS1115 was configured for 250 SPS, without a deliberate delay between channel pairs. RPM and optional old telemetry retain approximately 100 ms windows.
- Direct USB protocol checks passed: discovery/configuration, rejected invalid calibration/settings, duplicate ACK, unsupported command, oversized-line recovery and unchanged settings. The capture synchronizes at LF when opening an already-running serial stream.
- The steady 20-second capture contained **1,976 consecutive samples**, **98.72 Hz** average, **18 ms** maximum interval, **48.18 bytes** average wire frame, and **zero malformed lines or missing sequence values**. These were idle real sensor readings, not simulated data. The nominal HX711 mode is 80 SPS; 98.72 Hz is the observed conversion-ready/read cadence of this installed board, not a promised specification for other boards.
- SD reported **`sd_ok:true`, `RIG00002.CSV`** throughout the fast test, with no reported write failure. This verifies the running write path; independent card removal/readback and power-loss recovery remain untested.

Known-load accuracy at the new configuration, persisted save/reload, electrical offsets and RPM reference checks still require the appropriate physical inputs. Thrust frames are fresh conversions; voltage/current and RPM fields retain their independently acquired latest values. Short-run rate results do not establish a worst-case real-time guarantee under all cards and workloads.

The subsequent [paired gateway recording/download test](../ESP32_USB_Gateway/HARDWARE_TEST.md#fast-firmware-110-on-2026-10-27) saved **1,256 consecutive samples at 98.83 Hz**, with a 19 ms maximum interval and SD backup `RIG00004.CSV` active. The owner performed another phone calibration; its factor **19298.07812** was present in run metadata but still reported `persisted:false`. A reminder to use **Save EEPROM** before power-off was given. The bench script did not modify that calibration.
