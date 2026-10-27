idf_component_register(
    SRCS "app_main.cpp" "telemetry.cpp" "usb_bridge.cpp" "web_server.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_http_server spiffs json esp_timer usb
    EMBED_TXTFILES "../web/index.html" "../web/styles.css" "../web/app.js" "../web/model.js"
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
