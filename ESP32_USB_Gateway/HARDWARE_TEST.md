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

## Fast firmware 1.1.0 on 2026-10-27

- The owner opened the SparkFun HX711 RATE jumper. The [Nano fast test](../Nano_USB_Controller/HARDWARE_TEST.md#2026-10-27-rate-jumper-opened-and-fast-firmware) then received 1,976 consecutive fresh samples over 20 seconds (98.72 Hz, maximum interval 18 ms), with SD `RIG00002.CSV` active and no malformed frames or missing sequences.
- The compact S1 frame averaged 48.18 bytes, using approximately 4.8 kB/s at that measured rate. The existing 115200 baud setting remains sufficient. Control messages remain JSON v1.
- Native production-parser tests passed for S1 values/missing channels, malformed/nonfinite/truncated/extra fields, sequence gaps/reset/wrap, and a 48-sample pulse fragmented into arbitrary USB chunks. Existing legacy/JSON/ACK/framing/history checks and all six browser-model tests passed. CSV round-trip now retains Nano sequence numbers.
- Gateway build: **1,057,424-byte binary**, **1,057,017 / 2,097,152 application bytes**, **35,640 / 327,680 static RAM bytes**. It includes 100 ms visible-page poll delay, one-/five-second graph windows, measured sample rate and gap/SD diagnostics, 60,000 browser samples, and up to 256 samples per API response.
- Identified the same Zero on COM5 by MAC **3c:0f:02:e4:7f:08** and ESP32-S3 revision 0.2. Saved its complete 4 MiB flash before updating. Esptool verified all written hashes; the partition layout, NVS and SPIFFS recordings were retained.
- S3 native USB remained in download mode after software reset. Requested normal reconnection with BOOT released and restored host power for the paired recording check.
- After normal reconnection, API firmware **1.1.0**, persisted **115200** baud, protocol-v1 capabilities and fresh S1 data were confirmed. The owner also confirmed live loads on the updated phone dashboard.
- The computer had trouble obtaining a DHCP address while the phone was connected, and the owner reported temporary gateway-unavailable messages with concurrent viewers. After the phone disconnected, the computer obtained `192.168.4.2` and could use the gateway normally. The exact cause of the multi-client issue is unconfirmed; use one viewer at a time pending further investigation.
- With the computer as the sole Wi-Fi client, `tools/hardware_check.py` created **High-rate USB bench check**, stopped it, and downloaded its CSV and JSON metadata. The final CSV contained **1,256 samples**; metadata sample count matched. Acquisition cadence was **98.83 Hz** with a **19 ms** maximum interval. All Nano sequences were consecutive, with **zero USB queue drops and zero Nano sequence gaps**.
- The malformed-line counter was 1 before and after the bench recording: there were no new malformed frames during capture. API streaming continued while recording, and SPIFFS used 89,607 bytes afterward.
- Run metadata identified active SD backup **RIG00004.CSV** and the owner's newly applied factor **19298.07812**, poles **6**. `persisted:false` showed that this new calibration still needed an explicit EEPROM save. No calibration command or EEPROM write was performed by the bench script; it only requested configuration.
- The real recording remains in the gateway library. CSV/metadata/test captures remain local under ignored `artifacts/high-rate-hardware-check/`. The temporary computer Wi-Fi profile was removed after testing so the owner could resume phone use.

The fast Nano write path and paired recording/download are verified for this bench test. Independent SD removal/readback, reference sensor accuracy, persisted calibration save/reload, multiple simultaneous viewers and power-interruption behavior still require checks. These short rate measurements are observed bench results, not worst-case latency guarantees.

## Display-board cleanup

The P4 installer failed during its initial read-only verification before reaching the firmware write or OTA-selection steps. Its factory launcher was not replaced. It was restarted into its existing firmware; its complete 32 MiB backup remains local. The display prototype sources were saved on local branch `codex/archive-display-prototypes`; they are outside the active headless firmware.
