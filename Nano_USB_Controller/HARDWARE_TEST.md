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
