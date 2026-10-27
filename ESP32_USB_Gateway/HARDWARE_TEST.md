# LCD gateway verification record

Status: **buildable test firmware; touchscreen and rig integration awaiting hardware checks**.

## Connected-board checks, 2026-10-27

The original connected device was an ESP32-S3 revision 0.2 with 16 MB flash and 8 MB embedded PSRAM. A complete 16 MB flash backup was saved locally before writing firmware. That backup and device diagnostic artifacts are ignored by Git and are not published.

- Application, bootloader, and partition table flashed on COM3; esptool verified written hashes.
- First application boot responded to the PC service `STATUS` command as `1.1.0-lcd28b`.
- RGB driver and PSRAM framebuffer initialized. A framebuffer capture showed the Live interface at 480 × 640, with no fabricated measurements. This verifies rendering in memory, not the condition of the physical glass or panel.
- The inserted card mounted without formatting. Its reported FAT volume capacity was 1,186,349,056 bytes, with 32,768 bytes allocated. The firmware's temporary-file write/read/delete check passed.
- The Wi-Fi access point was visible as `ThrustTester`. The computer joined it and reached `192.168.4.1` over Wi-Fi while retaining its wired network connection.
- HTTP 200 responses received for `/`, `/styles.css`, `/app.js`, `/model.js`, `/api/status`, `/api/runs`, and `/api/samples?after=0`. Status correctly reported no Nano, no live samples, and an empty library.
- **Touch was not detected on the first boot.** The user subsequently reported possible physical screen damage and began arranging a replacement. A candidate probing both GT911 addresses was flashed with verified hashes; application startup and touch response after that flash were not confirmed before hardware tests were stopped for replacement.

No card formatting was needed or performed. The current package also includes small source cleanup after that candidate, so the packaged binary must be flashed and verified on the replacement board. Hardware success above applies to the first boot described here, not to an asserted complete system qualification.

## Software checks

The ESP-IDF/PlatformIO build passes for the custom LCD 2.8B target. Six browser-model tests pass. Native tests against the production C++ decoder/parser pass for legacy debug/CSV, JSON measurements, unavailable channels, strict framing, capabilities, acknowledgement matching, configuration, and bounded history. JavaScript syntax checking passes.

The preview server uses simulated samples and is separate from firmware. It does not establish hardware performance.

## Replacement-board and rig checks still required

1. Confirm the replacement is the **Touch LCD 2.8B**, then flash the packaged 16 MB target in PC service mode.
2. Confirm boot status, physical image quality, touch detection, tap counts, page navigation, scrolling, numeric/text keyboard behavior, and brightness.
3. Remount/test the card; verify a short real recording, stop, CSV/JSON download, and direct card readback.
4. Verify a suitable powered USB host arrangement, switch to Nano mode, and confirm the actual converter enumerates at the selected baud.
5. Verify real measurements, disconnect/reconnect behavior, and simultaneous LCD/Wi-Fi/USB/microSD operation at the intended rate.
6. After the separate Nano protocol update, verify tare, known-load calibration, every configuration field, acknowledgements/timeouts, and EEPROM persistence against known bench inputs.

The original Nano sketches have not been changed in this port. The absent receiver and old Nano control paths still need to be addressed during that second step.
