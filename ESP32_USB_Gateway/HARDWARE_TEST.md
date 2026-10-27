# Headless ESP32-S3 Zero verification

The active restoration target is the **Waveshare ESP32-S3 Zero**, using USB to the measurement Nano and a Wi-Fi phone dashboard. No display or microSD is required; recordings use the Zero's SPIFFS flash storage.

## Checks on 2026-10-27

- The small board was identified separately from the P4: ESP32-S3 revision 0.2, 4 MB embedded flash, 2 MB embedded PSRAM, native USB VID/PID `303A:1001`, COM5 on the test computer.
- A complete 4 MiB original flash backup was saved locally before writing. Backups and device diagnostics are ignored by Git.
- The 4 MB Zero firmware built successfully: about 1.05 MB application binary, 10.9% static RAM use, 50.3% of the 2 MiB application slot.
- Bootloader, partition table, and application were written only to the confirmed S3 COM5 device. Esptool verified all written hashes.
- Six dashboard-model tests, JavaScript syntax checking, and native production-parser/stream tests passed.
- Normal application boot and Wi-Fi/HTTP checks are pending leaving download mode. USB host mode intentionally removes the computer COM port after application startup.

The Nano was not connected for these checks. Actual converter enumeration, incoming measurements, real recording/downloads and calibration still require a rig-connected test. USB calibration additionally requires the separately planned Nano protocol update.

## Display-board cleanup

The P4 installer failed during its initial read-only verification before reaching the firmware write or OTA-selection steps. Its factory launcher was not replaced. It was restarted into its existing firmware; its complete 32 MiB backup remains local. The display prototype sources were saved on local branch `codex/archive-display-prototypes`; they are outside the active headless firmware.
