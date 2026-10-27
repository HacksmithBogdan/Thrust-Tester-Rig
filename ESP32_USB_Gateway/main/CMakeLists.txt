idf_component_register(
    SRCS "app_main.cpp" "telemetry.cpp" "usb_bridge.cpp" "web_server.cpp" "storage.cpp" "actions.cpp" "board_lcd28b.cpp" "touch_ui.cpp" "pc_service.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_http_server fatfs sdmmc json esp_timer usb esp_lcd esp_driver_i2c esp_driver_spi esp_driver_gpio esp_driver_usb_serial_jtag
    EMBED_TXTFILES "../web/index.html" "../web/styles.css" "../web/app.js" "../web/model.js"
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
