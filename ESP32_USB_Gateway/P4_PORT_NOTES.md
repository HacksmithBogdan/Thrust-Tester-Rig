# ESP32-P4 Wi-Fi 6 Touch LCD 4.3 assessment

The P4 is a candidate replacement for the damaged S3 display. **The current flashable ZIP still targets the S3 Touch LCD 2.8B. There is no P4 thrust-tester firmware package yet.** Keep the existing P4 launcher until a compatible integrated app has been built and checked.

## Hardware identified on 2026-10-27

The board connected through USB TO UART as COM4 (WCH CH343, VID/PID `1A86:55D3`). Read-only esptool identification reported ESP32-P4 silicon revision **1.3** and **32 MB flash**. COM numbering is specific to this computer.

| Connection | Intended use |
| --- | --- |
| USB TO UART | Computer: power, programming, serial boot logs and diagnostics through the onboard CH343 bridge. |
| USB / USB OTG | Nano USB-to-serial converter: the P4 runs as USB host once the thrust app's host driver is active. |
| microSD | Local CSV/JSON test library under `/thrust/`, alongside existing files. |

The separate UART bridge means Nano host operation can coexist with computer diagnostics; the S3's shared native-USB mode switch is unnecessary on this board. The actual host VBUS supply and cable arrangement still need a physical check. A classic Nano generally does not back-feed 5 V from its own rail to its USB socket.

## Read-only backup and factory boot

A full **33,554,432-byte P4 flash backup** completed before any firmware changes. Its SHA-256 and decoded partition/app metadata are recorded in an ignored local JSON beside the backup under `artifacts/backups/`. No firmware, partition table, OTA selection, C6 flash or SD contents were written by the inspection commands. This is a P4 flash backup, not an image of the SD card or a separate C6 flash dump.

After reset the factory application booted as `phone_p4_function_ev_board`, version `v0.4.2-113-ge0e613f-dirty`, built February 26, 2026 using ESP-IDF `v5.5.2`. Its serial log reported display initialization, GT911 at **0x5D**, touch input registration, and a successful SD-card mount. Physical tap response, Nano USB operation, and thrust logging have not been tested on this P4.

The actual installed partition map is different from the newer vendor Rev3.x source:

| Partition | Offset | Size | Contents |
| --- | --- | --- | --- |
| `factory` | `0x110000` | `0x900000` (9 MiB) | Existing Brookesia launcher. |
| `ota_0` | `0xA10000` | `0x3F0000` (3.9375 MiB) | Xiaozhi 2.1.0 application. |
| `ota_1` | `0xE00000` | `0x3F0000` (3.9375 MiB) | Entire slot verified erased in the backup. |
| `assets` | `0x11F0000` | `0x900000` | Existing assets; preserve. |
| `storage` | `0x1AF0000` | `0x500000` | Existing SPIFFS storage; preserve. |

This creates two possible integration paths: rebuild the launcher to include a new icon, or place a standalone P4 test application into the unused `ota_1` slot and select it through OTA boot metadata. The second path can preserve the exact factory launcher image and other apps; it needs a deliberate way to reboot back to `factory`. Neither path has been implemented or flashed yet. The partition layout, bootloader, occupied `ota_0`, assets and settings must be retained for a separate-slot test.

## Keep the launcher and add a native app

Waveshare publishes an ESP-Brookesia phone launcher and bundled native C++ applications. The published factory main constructs app objects and registers them with `phone->installApp(...)`; the framework provides a `systems::phone::App` base class with `run`, `back`, `close`, `init`, and `deinit` hooks.

This provides an integration path for a **Thrust Tester** icon while retaining the launcher and other applications in source. It still requires compiling and flashing an updated application firmware image. The inspected factory source does not provide an independent thrust app that can be installed simply by copying a file onto the SD card. Matching the installed factory version matters; retaining the exact installed behavior cannot be assumed from the latest vendor source.

The vendor factory source currently documented under `firmware/brookesia` targets Rev3.x with ESP-IDF 5.5.5. This connected Rev1.3 board needs the explicit pre-v3 silicon profile, compatible 200 MHz PSRAM settings, and the revision-appropriate MIPI PHY clock. Do not flash the newer Rev3.x image onto it.

## Port requirements

- Reuse telemetry parsing, USB VCP drivers, command validation, CSV/JSON formats, and the offline phone dashboard from the S3 gateway.
- Use the P4 board support package for its 480 x 800 MIPI-DSI panel, touch, SDMMC and backlight. The S3's RGB pin map and panel initialization are incompatible.
- Adapt the LVGL 8 standalone UI to the launcher's LVGL/Brookesia lifecycle. Create UI when the app opens; release UI objects/timers when it closes. Measurement and recording services need an explicit lifetime independent of the foreground app.
- Reuse the ESP32-C6 wireless coprocessor through ESP-Hosted / esp_wifi_remote. Coordinate Wi-Fi ownership with the existing Settings app rather than starting a second network stack. The P4 itself has no Wi-Fi radio.
- Mount/reuse microSD without formatting; write only within the thrust directory. Coordinate existing media apps and active recordings/downloads before remounting.
- Install the USB host library once, use the OTG port for the Nano, and retain UART diagnostics. Confirm host power and actual Nano converter enumeration before a systems test.
- Keep the P4 flash backup local. Preserve the C6 firmware, original bootloader/partition/assets and SD contents unless a reviewed compatible update requires a specific change.

## References

- [Waveshare board and connector guide](https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-4.3)
- [Official board schematic](https://files.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-4.3/ESP32-P4-WIFI6-Touch-LCD-4.3-schematic.pdf)
- [Factory source and requirements](https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4.3/tree/main/firmware/brookesia)
- [Brookesia app development](https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4.3/blob/main/firmware/brookesia/components/brookesia_core/docs/how_to_use.md)
- [Vendor Rev1.3 compatibility overlay](https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4.3/blob/main/config/ci/rev1_3.defaults)

Vendor source reviewed at commit `dc1b2085381c9cc3a2badf8d317673cca9079c1f`. Software support and published build settings establish feasibility; they do not constitute Nano-link, SD, touchscreen, or Wi-Fi tests on this P4.
