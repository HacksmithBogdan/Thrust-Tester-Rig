# Headless ESP32-S3 Zero verification

The active restoration target is the **Waveshare ESP32-S3 Zero**, using USB to the measurement Nano and a Wi-Fi phone dashboard. No display or microSD is required; recordings use the Zero's SPIFFS flash storage.

## Checks on 2026-10-27

- The small board was identified separately from the P4: ESP32-S3 revision 0.2, 4 MB embedded flash, 2 MB embedded PSRAM, native USB VID/PID `303A:1001`, COM5 on the test computer.
- A complete 4 MiB original flash backup was saved locally before writing. Backups and device diagnostics are ignored by Git.
- The 4 MB Zero firmware built successfully: about 1.05 MB application binary, 10.9% static RAM use, 50.3% of the 2 MiB application slot.
- Bootloader, partition table, and application were written only to the confirmed S3 COM5 device. Esptool verified all written hashes.
- Six dashboard-model tests, JavaScript syntax checking, and native production-parser/stream tests passed.
- After a normal power cycle with BOOT released, the application booted and the computer COM port disappeared as expected for native USB host mode.
- The computer joined the Zero's `ThrustTester` access point. Its BSSID matched the identified Zero, and `/api/status` reported firmware `1.0.0`, USB waiting, no samples, no errors, and ready SPIFFS storage (1,860,161 usable bytes; 0 used).
- Embedded HTML/CSS/JavaScript and status/library/sample/console endpoints returned HTTP 200 over the board's Wi-Fi. The actual dashboard rendered correctly with no browser errors and disabled recording while disconnected.
- Recording with no Nano and calibration without advertised capabilities both returned HTTP 409 with the expected reasons. No synthetic measurements or test recordings were created.

The Nano was not connected for the initial checks above. The matching [Nano firmware was then built, flashed and tested directly over USB](../Nano_USB_Controller/HARDWARE_TEST.md), at 115200 baud.

## Paired USB link on 2026-10-27

- The user connected the measurement Nano through its Mini-USB/data cable and USB-A-to-USB-C adapter to the Zero, powered separately from the rig's regulated 5V/GND supply.
- Joined the Zero's identified access point (`3c:0f:02:e4:7f:09`) and read its actual API. The CH340 converter enumerated as `1A86:7523`, but the stored gateway setting was still 9600; raw data was unreadable and there were no samples.
- Changed only the gateway baud setting to **115200** through `/api/settings`; confirmed the persisted setting after restart. No firmware reflash was needed.
- Received 278 real Nano measurements before a temporary USB disconnect. After reconnection, protocol v1, all five calibration/configuration capabilities, current configuration and fresh samples were present. Malformed and queue-drop counters remained zero at the correct baud.
- Configuration retained factor **100340**, poles **6**, shunt `0.000192` and divider `34.130001`. An acknowledged tare was observed through the phone/API, with EEPROM unchanged. A read-only configuration request was also sent over the USB bridge.
- Nano SD still reported `sd_ok:false` and an empty filename. Nano uptime continued across the cable reconnection, so this was not yet a fresh SD initialization attempt after the card was reseated; a measurement-Nano reset was requested.
- A `USB command too long; discarded` event from the Nano was observed after the baud change. Protocol discovery succeeded after reconnection; no calibration was inferred from that earlier error.

This validates the tested converter/cable data path and live USB stream. Completed real recording/download, reference calibration, SD backup, sustained operation and power-interruption behavior remain to be checked. Temporary test Wi-Fi profiles and device captures are local only.

## Display-board cleanup

The P4 installer failed during its initial read-only verification before reaching the firmware write or OTA-selection steps. Its factory launcher was not replaced. It was restarted into its existing firmware; its complete 32 MiB backup remains local. The display prototype sources were saved on local branch `codex/archive-display-prototypes`; they are outside the active headless firmware.
