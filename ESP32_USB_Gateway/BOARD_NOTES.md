# Touch LCD 2.8B hardware notes

Target: Waveshare **ESP32-S3-Touch-LCD-2.8B**, ESP32-S3R8, 16 MB quad flash and 8 MB octal PSRAM. This is a different display and pin map from the non-B 2.8-inch board.

Hardware configuration and the numerical panel initialization data in `main/panel_init.inc` are derived from the official [Waveshare example package](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.8B/ESP32-S3-Touch-LCD-2.8B-Demo.zip), specifically the ESP-IDF test project's `LCD_Driver/ST7701S.c`. The gateway's board, UI, storage, and command integration code is implemented separately using ESP-IDF/LVGL APIs. The example's general source was not imported wholesale.

- ST7701: 480×640 RGB565; vendor 30 MHz pixel clock and H/V porch values.
- Parallel RGB data pins, low to high: `5,45,48,47,21,14,13,12,11,10,9,46,3,8,18,17`.
- HSYNC38, VSYNC39, DE40, PCLK41; backlight GPIO6.
- I2C0: SDA15, SCL7, 400 kHz; TCA9554 address `0x20`; GT911 probed at `0x5D` and `0x14` (100 kHz touch transactions).
- TCA9554 P0=LCD reset, P1=touch reset, P2=LCD serial CS, P3=SD D3/CS, P7=buzzer. P4–P6 are peripheral interrupt inputs.
- LCD initialization uses nine-bit SPI on GPIO1/2 with CS through the expander. The SPI device/bus is released before mounting microSD.
- SDMMC one-bit: CMD1, CLK2, D0=42, with expander P3 held high. FAT mounting never formats a card automatically during ordinary startup/remount.
- Native USB uses GPIO19/20. USB host mode and PC serial/JTAG share the physical interface; the device screen selects the mode with a restart. Fresh installations default to PC service mode for initial computer-connected verification.
- The board's `5V` header is raw USB VBUS; `VCC` is after a diode. Battery operation does not establish a 5 V USB host supply. Verify host power before moving the cable to the Nano.

PSRAM XIP, 64-byte cache lines, an external framebuffer and correctly sized LVGL draw buffers are used. RGB uses two internal bounce buffers to reduce PSRAM contention with Wi-Fi and SD activity. Physical combined-load validation remains necessary.

References: [board documentation](https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.8B), [schematic](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.8B/ESP32-S3-Touch-LCD-2.8B_schematic_diagram.pdf), [Espressif RGB driver guidance](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html).
