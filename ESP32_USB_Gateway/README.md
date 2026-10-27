# Touchscreen USB gateway for the thrust tester

This firmware adds local touchscreen controls, a phone dashboard, and microSD recording to the existing thrust rig. It targets the **Waveshare ESP32-S3-Touch-LCD-2.8B** (480 × 640 RGB display, 16 MB flash, 8 MB PSRAM). The measurement Nano stays installed and connects through its USB socket.

A different **ESP32-P4-WIFI6-Touch-LCD-4.3** board is being assessed for retaining its factory app launcher. See [P4_PORT_NOTES.md](P4_PORT_NOTES.md). The firmware package below remains S3-specific.

The archived Arduino firmware is unchanged. Live monitoring works with its debug serial stream. USB calibration needs the planned Nano firmware update described in [PROTOCOL.md](PROTOCOL.md).

## First use

| Setting | Default |
| --- | --- |
| Wi-Fi network | **ThrustTester** |
| Wi-Fi password | **ThrustRig-2026** |
| Dashboard | **http://192.168.4.1** |
| Subnet / DHCP | `255.255.255.0`; client addresses assigned automatically |
| Nano serial | **9600 baud, 8-N-1** |
| USB mode on a fresh installation | **Computer / PC service** |
| Thrust unit label | **kgf**, to be checked against the load-cell calibration |
| Recording location | microSD **`/thrust/`** |

Join the network from a phone, keep the connection when it reports no internet, and open the address directly. All assets are served by the ESP32; there is no cloud, account, CDN, installed app, or internet requirement. There is no captive portal. Change the default password under **Device & connection** before routine use.

The LCD has four pages:

- **Live:** thrust, RPM, volts, amps, watts, throttle, and named start/stop recording with notes.
- **Calibrate:** tare, known-load calibration, load-cell factor, transitions per revolution, shunt resistance, voltage divider, and explicit Nano EEPROM save. Controls require the Nano's advertised capabilities and show its acknowledgement or timeout.
- **Runs:** browse saved run names, notes, status, and sample counts. The phone dashboard provides plots and downloads.
- **Device:** connection details, card usage, microSD remount/read-write check, USB mode, serial speed, and brightness. Swipe to reach lower controls.

The phone dashboard also provides live plots, trace export, recorded-run plots, CSV/JSON downloads, deletion, original six-column log import, raw serial diagnostics, and Wi-Fi/IP/unit settings. Both interfaces share the same state and command validation. Recording continues with the phone disconnected.

The gateway observes measurements and performs calibration; it has no motor, ESC, engine, or emergency-stop commands. The unit setting changes the label and does not convert or recalibrate readings.

## USB mode and power

The board has **one native USB interface**. PC serial/JTAG service and Nano USB host operation use the same port, so they cannot run simultaneously.

For initial flashing and checks, leave the board in **PC service** mode and connect it to the computer. When ready to connect the Nano, choose **Device → Use Nano USB**, confirm, and move the data cable to the Nano using a suitable powered host arrangement. To return, choose **Use PC service USB**, then reconnect the computer. Wi-Fi and the LCD operate in either mode. BOOT/download mode remains available for recovery.

The ESP32 is the USB **host** and the Nano's USB-to-UART converter is its peripheral. Registered CH34x, CP210x, and FT23x converter IDs are supported by pinned Espressif VCP drivers. Unregistered clone IDs and generic native CDC devices may need a driver addition; the phone diagnostics show VID/PID when detected.

**A cable alone generally cannot power this board from an already-powered classic Nano.** The classic Nano's USB diode blocks power flowing back to its USB socket. On the LCD board, the `5V` header is raw USB VBUS and `VCC` is after a diode; its battery supply does not establish a regulated 5 V USB host output. Use a suitable regulated host-power feed or powered adapter, check the actual boards and cable roles, and avoid competing 5 V sources. USB data needs no UART level shifter. Perform initial hookup with propulsion power disconnected.

See [BOARD_NOTES.md](BOARD_NOTES.md), the [LCD board schematic](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.8B/ESP32-S3-Touch-LCD-2.8B_schematic_diagram.pdf), and the [classic Nano schematic](https://content.arduino.cc/assets/NanoV3.3_sch.pdf).

## Nano compatibility

Enable `DEBUG` in the installed Nano sketch. Version 4 uses **9600 baud**; the Schubler variant uses **115200**. Match the gateway setting to that firmware. The gateway accepts the existing labeled debug stream, original six-number CSV, and the proposed JSON v1 protocol.

With the old firmware, monitoring and gateway recording work; calibration controls stay disabled. The ESP32 cannot enable a compile-time Nano debug flag remotely. It does not update the Nano automatically or read files from the Nano's own SD card. The missing receiver's effect on the old measurement loop remains part of the Nano restoration work.

## microSD logging

Use a FAT-formatted card, preferably FAT32. GPIO1/2 are shared between LCD serial initialization and SDMMC: the LCD SPI device is released before the one-bit SD bus is mounted. The card needs no connection to the Nano's SD socket.

Ordinary startup and **Remount microSD** never format a card. A failed mount is reported and the dashboard remains usable. The deliberate PC service command `prepare-fresh-sd` permits formatting **only if the FAT mount fails**; use it only for a card whose contents may be erased.

Each run produces `/thrust/run-....csv` and a matching `.json` metadata file. Files can be read directly from the card or downloaded over Wi-Fi. Stop recording and finish downloads before removing or remounting the card. The library supports 64 runs, requires 1 MiB free to start, and stops near 512 KiB free. Runs are never overwritten to make room.

All incoming samples, including idle samples, are recorded while a run is active. CSV is flushed every ten samples. Power loss can lose recent unflushed data; the next boot marks unfinished metadata as `interrupted`, without resuming the run. Its sample count may be incomplete; the CSV is the primary recovered record. USB loss creates a timestamp gap and recording continues if measurements return. A storage write/flush failure stops recording.

The ESP32 holds 600 recent samples in RAM; each browser retains up to 6000. **Export trace** exports browser history, whereas recording writes received samples to the card independently of the browser.

## Build and flash

This is an **ESP-IDF C++ project**. Use the supplied helper or PlatformIO, rather than uploading it as an Arduino sketch. Requirements: Python 3, Git, PowerShell, and internet for the first dependency installation.

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

The helper installs PlatformIO in a dedicated Python environment, stages sources outside space-containing paths, and produces **`artifacts/thrust-tester-esp32-s3-lcd28b.zip`**. The ZIP contains firmware, bootloader, partition table, checksums, flash script, and instructions. It is for the **2.8B with 16 MB flash**, not the S3 Zero or non-B display.

Disconnect the Nano and external host-power feed, connect the board to the computer with a data cable, then run:

```powershell
powershell -ExecutionPolicy Bypass -File .\flash.ps1 -Port COM3
```

Use the actual COM port. If required, hold **BOOT**, tap **RESET**, then release **BOOT** to enter download mode. The flash helper also works from an extracted ZIP and installs its own esptool environment when needed.

Flash addresses: bootloader `0x0`, partition table `0x8000`, application `0x10000`. The 16 MB layout has a 6 MiB application slot and NVS/PHY partitions; recordings live on microSD. Normal flashing preserves NVS settings and the card. A full flash erase removes settings. The old Zero/SPIFFS layout is a different target and should not be used to update this board.

Pinned versions: PlatformIO Core 6.1.18, espressif32 6.12.0, ESP-IDF 5.5.0, Xtensa GCC 14.2.0, LVGL 8.3.11, CDC host 2.4.1, CH34x 2.2.1, CP210x 2.2.0, FTDI 2.1.1, VCP 1.0.0~6. `dependencies.lock` records resolved hashes.

## Diagnostics and testing

In PC service mode, `tools/service_client.py` provides hardware checks over USB serial. It requires pyserial, included with the build tools:

```powershell
python .\tools\service_client.py --port COM3 status
python .\tools\service_client.py --port COM3 sdtest
python .\tools\service_client.py --port COM3 mount
python .\tools\service_client.py --port COM3 --output screen.ppm screen
python .\tools\service_client.py --port COM3 clock
```

The screen command copies the actual firmware framebuffer. `clock` sets the current session's wall clock from the computer; it does not configure the onboard RTC. LCD-started runs have an unknown wall-clock start (`0`) until a clock is set. Phone-started runs use the phone's timestamp; monotonic gateway time is always present.

For a deliberately disposable fresh card that cannot mount:

```powershell
python .\tools\service_client.py --port COM3 prepare-fresh-sd
```

Firmware generates no simulated measurements. The separate preview server provides explicitly labeled demo data for UI development:

```powershell
python .\tools\preview_server.py --port 8765
node --test .\tests\model.test.cjs
powershell -ExecutionPolicy Bypass -File .\tests\run-telemetry-tests.ps1 -Python python
```

The native tests require `ziglang==0.14.1` and the installed ESP-IDF cJSON source; they exercise production parsing and stream framing. See [HARDWARE_TEST.md](HARDWARE_TEST.md) for what has actually been checked on the connected board. Nano enumeration, real measurements, calibration, and combined LCD/Wi-Fi/USB/SD throughput need a rig-connected systems test after the Nano update.

## Source map

| File | Responsibility |
| --- | --- |
| `main/app_main.cpp` | Startup, NVS settings, Wi-Fi, parser task and USB mode selection. |
| `main/board_lcd28b.cpp`, `panel_init.inc` | Board wiring, ST7701 RGB panel, GT911 touch and backlight. |
| `main/touch_ui.cpp` | LVGL touchscreen pages and recording/calibration controls. |
| `main/storage.cpp` | SDMMC mount, read/write test, CSV/metadata library and recovery. |
| `main/actions.cpp` | Shared Nano command validation and queueing. |
| `main/usb_bridge.cpp` | Host/converter lifecycle, serial queues and reconnection. |
| `main/telemetry.cpp` | Legacy/CSV/JSON parser, capabilities and acknowledgements. |
| `main/web_server.cpp`, `web/` | Embedded dashboard and local HTTP API. |
| `main/pc_service.cpp`, `tools/service_client.py` | Computer-connected hardware diagnostics. |
| `PROTOCOL.md` | Nano update contract and HTTP API. |

The USB implementation follows Espressif's [CC0 VCP host example](https://github.com/espressif/esp-idf/tree/v5.5/examples/peripherals/usb/host/cdc/cdc_acm_vcp). Managed components retain upstream licenses. Board configuration provenance is recorded in [BOARD_NOTES.md](BOARD_NOTES.md). This addition does not assign a blanket license to the original project archive.
