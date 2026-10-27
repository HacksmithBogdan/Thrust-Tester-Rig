# ESP32-S3 USB gateway and local dashboard

This firmware adds a phone-accessible interface to the existing thrust rig without rewiring its measurement Nano. The ESP32-S3 hosts the Nano over USB, creates its own Wi-Fi network, serves all dashboard files locally, and records received measurements to its own flash.

The original Arduino sketches remain unchanged. Use the matching [measurement Nano USB firmware](../Nano_USB_Controller/README.md) for phone calibration and configuration. The ESP32 is an additional interface; the previous FrSky telemetry hardware does not need to be used.

## First connection

| Setting | Default |
| --- | --- |
| Wi-Fi network | **ThrustTester** |
| Wi-Fi password | **ThrustRig-2026** |
| Web address | **http://192.168.4.1** |
| Gateway / subnet | `192.168.4.1` / `255.255.255.0` |
| DHCP | Enabled for phone/client addresses |
| Nano serial | **9600 baud, 8-N-1** initially; set **115200** for the replacement Nano firmware |
| Thrust display label | **kgf**; confirm against the actual load-cell calibration |

Wi-Fi name, password, IP, baud, and unit label can be changed under **Device & connection → Nano serial speed**. On a phone, swipe the top navigation tabs left to reveal **Device & connection** at the right end. Leaving the password field empty keeps it unchanged. Saving restarts the gateway. The AP password controls access to the dashboard; choose your own password before routine use.

Join this Wi-Fi network from the phone and keep the connection when it reports no internet. Open the HTTP address directly. No internet, cloud service, CDN, account, or installed phone app is needed. This implementation uses an explicit address rather than a captive portal.

## Hardware and USB power

Target: **Waveshare ESP32-S3 Zero with 4 MB flash**. The firmware does not depend on PSRAM. Other boards require checking their flash configuration, USB routing, and power circuit.

The ESP32 runs as the **USB host**, and the Nano's USB-to-serial chip is its peripheral. The included Espressif drivers support registered CH34x, CP210x and FT23x converter IDs. Generic native USB CDC devices and every possible clone converter are not covered by that registration. The dashboard displays a detected VID/PID when available to help identify unsupported hardware.

**A cable alone generally will not power the ESP32 from an already-powered Nano.** The classic Nano schematic has a diode from USB VBUS to its 5 V rail that blocks power in the reverse direction. A clone may differ. The Waveshare Zero has its USB VBUS tied to its VBUS power input and regulator, and fixed USB-C sink CC resistors; it is not a complete switched USB host power port.

Use an independently powered, regulated 5 V feed to the Zero's VBUS/5 V input, or a suitable externally powered host adapter/injection arrangement. Verify that the actual adapter, connector roles and Nano wiring provide the intended USB power/data path without competing 5 V sources. A charge-only cable will not work. There is no UART level shifter in the USB data path.

Check the [Waveshare schematic](https://files.waveshare.com/wiki/ESP32-S3-Zero/ESP32-S3-Zero-Sch.pdf) and [classic Nano schematic](https://content.arduino.cc/assets/NanoV3.3_sch.pdf) against the installed boards. Perform initial hookup with propulsion power disconnected.

## Features

- Live thrust, RPM, voltage, current, calculated electrical power, and throttle.
- Selectable performance traces and time windows, with inspection tooltip and CSV export of browser history.
- ESP32-side start/stop recording with names and test notes. Recording continues when the browser disconnects.
- Persistent run library, completed-run plots, CSV download, metadata download, and explicit deletion.
- Local opening of original six-column SD logs or gateway CSV files on the phone/browser. Files stay on that device.
- USB status, VID/PID, stale-data indication, raw serial monitor, malformed-line and queue-drop counters.
- Nano calibration UI: tare, known-load calibration, pole count, load-cell factor, shunt resistance, divider ratio, and explicit EEPROM save. These controls are enabled only after updated Nano firmware advertises the corresponding commands and acknowledges them.
- Persistent Wi-Fi, IP, serial-speed and unit-label settings.

**With the replacement Nano firmware:** monitoring, recording, calibration and configuration use protocol v1 at **115200 baud**. Flash only the measurement Nano using [Nano_USB_Controller](../Nano_USB_Controller/README.md), then change the gateway's serial-speed setting to match. That firmware also writes continuous parallel backups to the Nano's existing SD module when available.

The fast Nano firmware requires **gateway 1.1.0+**. Measurements use compact S1 lines, while commands/configuration remain JSON v1. Each frame contains a fresh load-cell conversion. The dashboard shows measured samples/second and requests updates with a 100 ms delay between completed polls (up to roughly ten refreshes/second, depending on the phone/network). Recording retains every received frame independently of browser updates. One- and five-second plot windows and millisecond tooltip timestamps help inspect short thrust pulses. See the [fast acquisition notes](../Nano_USB_Controller/README.md#fast-acquisition).

**Current hardware limitation:** use one phone/computer viewer at a time. Simultaneous viewers caused intermittent connection trouble in the paired test; the cause is not yet confirmed. The single-client test successfully recorded/downloaded 1,256 samples at 98.83 Hz with no sequence gaps. See the [hardware record](HARDWARE_TEST.md).

**With an archived Nano sketch:** monitoring/recording require its full debug stream. Enable `DEBUG` in the chosen sketch. Version 4 uses 9600 baud; the Schubler variant uses 115200. Calibration controls require the replacement firmware. The complete wire contract is in [PROTOCOL.md](PROTOCOL.md). The ESP32 does not update the Nano automatically or expose files already on its SD card.

The gateway does not control propulsion. Its thrust unit setting labels measurements; it does not perform a kgf/newton/pound-force conversion.

## Build and flash

This is an **ESP-IDF C++ project**, rather than an Arduino IDE sketch. It uses the official USB host/VCP driver components with exact dependency pins. The supplied PowerShell helper installs PlatformIO into a dedicated Python virtual environment and builds outside paths containing spaces.

Requirements: Python 3, Git, internet access for the first dependency installation, and PowerShell. From this directory:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

The result is `artifacts/thrust-tester-esp32-s3-zero.zip`, containing application, bootloader, partition table, SHA-256 checksums, a flash script, and hookup instructions.

1. Disconnect the Nano and external host-power feed from the ESP32 before connecting it to the computer; avoid joining independent 5 V sources.
2. Connect the ESP32 to the computer with a USB data cable.
3. Hold **BOOT**, tap **RESET**, then release **BOOT** to enter USB download mode.
4. Find the ESP32 COM port and run:

```powershell
powershell -ExecutionPolicy Bypass -File .\flash.ps1 -Port COM7
```

Replace `COM7` with the actual port. The helper can also be run from the extracted firmware ZIP; no build is required when using that package. It installs esptool in a dedicated environment if needed.

If the hotspot does not appear after flashing, release BOOT and press RESET, or unplug/reconnect USB without holding BOOT, to leave download mode.

After application startup, the native USB port changes to host mode. It no longer appears as a computer serial monitor. Re-enter BOOT/download mode for the next USB flash. Firmware diagnostics use UART0 internally; the web diagnostics panel shows runtime USB/stream information.

Flashing writes bootloader at `0x0`, partitions at `0x8000`, and application at `0x10000`. It leaves NVS and recording storage alone. An explicit full flash erase removes settings and recordings. Keep this partition layout for updates that should preserve files.

For an existing PlatformIO installation, `pio run` also works when the project path contains no spaces. Native ESP-IDF 5.5 builds can use the supplied CMake project, manifest, partitions and defaults.

### Pinned environment

- PlatformIO Core `6.1.18` (helper installation).
- Espressif32 platform `6.12.0`, ESP-IDF `5.5.0`, Xtensa GCC `14.2.0`.
- CDC host `2.4.1`, CH34x `2.2.1`, CP210x `2.2.0`, FTDI `2.1.1`, VCP `1.0.0~6`.
- `dependencies.lock` records the resolved component hashes.

## Recording behavior and limits

The 4 MB flash is partitioned into a 2 MB application slot, NVS/PHY data, and approximately **1.94 MiB of SPIFFS recording storage** before filesystem overhead (**1.77 MiB usable** measured on the Zero). This is for short tests, not unlimited logging. Capacity depends on sample length and rate; watch the storage meter and export completed runs.

The library permits at most 64 runs. A new run is refused above 70% filesystem usage, and an active recording stops near 75%, leaving space for metadata and filesystem maintenance. Files are not overwritten to make room. Any storage error stops recording and appears in the dashboard.

Each run contains a CSV and metadata JSON. The CSV contains all received measurements while recording, including idle measurements. The browser's Export trace action exports only the history it currently holds; it is not a substitute for gateway recording. Downloads and saved-run viewing require stopping that run first.

The Zero has no microSD slot. Its flash library and phone downloads are the primary recording workflow. The replacement Nano's SD logger is independent: one new six-column CSV per Nano boot, including idle samples. Match `nano_time_s` in the gateway CSV to `Time` in the SD CSV; the Nano's `sd_file` is retained in the run's configuration metadata when available. Missing Nano SD does not prevent ESP32 recording. Retrieve Nano SD files by removing the card; no USB file-copy or backup synchronization is implemented.

The gateway retains 600 recent samples in RAM for reconnecting browsers (about six seconds at 100 Hz). Each browser retains up to 60,000 live samples (about ten minutes at 100 Hz), and can fetch 256 samples per request. Wi-Fi loss does not stop gateway recording. USB loss leaves a timestamp gap, and logging resumes when samples return. A power interruption does not automatically restart a recording; interrupted runs are identified at boot. CSV data is flushed approximately once per second and on explicit stop, so up to about one second of buffered data can be lost on power failure. Metadata sample count may be incomplete after interruption; the CSV is the primary recovered measurement record.

The downloaded CSV includes Nano sequence numbers as a ninth column, alongside the separate acquisition and receipt timestamps. The connection page reports recent maximum acquisition interval, missing Nano sequences and the SD backup filename. Faster sampling uses recording space faster; these flash recordings are for short tests, so watch the storage meter and export completed runs.

The filesystem is formatted automatically only on its first initialization. A later mount failure is reported without silently erasing recordings. CSV transfer, SPI flash endurance, sustained recording rate, converter resets and power behavior still need evaluation on the actual hardware.

## Verification and local preview

See [HARDWARE_TEST.md](HARDWARE_TEST.md) for the connected-board test record.

The preview server emulates the HTTP API with clearly marked simulated measurements. It is not a hardware test, and simulation code is never embedded in the firmware.

```powershell
python .\tools\preview_server.py --port 8765
```

Open `http://127.0.0.1:8765`. Test modes are selected only on this preview server using `/api/demo?mode=legacy_debug`, `protocol_v1`, `stale`, or `disconnected`. They are not production firmware routes.

Run browser-model tests:

```powershell
node --test .\tests\model.test.cjs
```

Native tests compile and exercise the production C++ parser and stream decoder, using the installed ESP-IDF cJSON source and stubs only for clock, recording, and opaque RTOS declarations. Install `ziglang==0.14.1` in a Python environment, then run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\run-telemetry-tests.ps1 -Python python
```

With a physically connected, idle fast-rate rig, this optional check creates a clearly named real bench recording, stops it, and verifies its downloaded CSV, metadata, rate and sequence continuity. It does not alter calibration and refuses to interrupt an existing run:

```powershell
python .\tools\hardware_check.py --seconds 15 --output .\artifacts\hardware-check
```

Before a physical test: confirm the actual ESP board and Nano converter, verify the USB power arrangement, flash both controllers using their separate guides, set the gateway to 115200, check incoming measurements, record a short bench run, disconnect/reconnect the phone, stop the run, and download its CSV and metadata. Exercise calibration against known inputs with propulsion power disconnected. Direct Nano USB tests and end-to-end USB-host tests are recorded separately; see the [Nano test record](../Nano_USB_Controller/HARDWARE_TEST.md).

## Source map

| File | Responsibility |
| --- | --- |
| `main/usb_bridge.cpp` | USB host lifecycle, converter registration, baud setup, RX/TX queues and reconnection. |
| `main/telemetry.cpp` | Legacy/CSV/JSON decoding, capabilities, acknowledgements and sample JSON. |
| `main/app_main.cpp` | Startup, Wi-Fi, NVS settings, SPIFFS recording and stream assembly. |
| `main/web_server.cpp` | Embedded assets and local HTTP API. |
| `web/` | Self-contained responsive dashboard and CSV/plot model. |
| `PROTOCOL.md` | Nano update contract and HTTP API. |
| `tools/preview_server.py` | Explicit simulated API for local interface verification. |

The USB implementation follows Espressif's [CC0 VCP host example](https://github.com/espressif/esp-idf/tree/v5.5/examples/peripherals/usb/host/cdc/cdc_acm_vcp). Managed components retain their upstream licenses and are downloaded during build. This addition does not assign a blanket license to the original project archive.
