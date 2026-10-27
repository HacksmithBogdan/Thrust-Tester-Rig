#include "gateway.hpp"
#include "stream_decoder.hpp"
#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <sys/stat.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "lwip/inet.h"

namespace rig {
State state;
QueueHandle_t rx_queue = nullptr, tx_queue = nullptr;
static const char* TAG = "rig";
void load_settings() {
    nvs_handle_t handle;
    if (nvs_open("gateway", NVS_READONLY, &handle) != ESP_OK) return;
    size_t length = 0;
    if (nvs_get_str(handle, "settings", nullptr, &length) == ESP_OK && length < 2048) {
        std::string raw(length, '\0');
        if (nvs_get_str(handle, "settings", raw.data(), &length) == ESP_OK) {
            cJSON* json = cJSON_Parse(raw.c_str());
            if (json) {
                state.settings.ssid = string_field(json, "ssid", state.settings.ssid);
                state.settings.password = string_field(json, "password", state.settings.password);
                state.settings.ip = string_field(json, "ip", state.settings.ip);
                state.settings.unit = string_field(json, "unit", state.settings.unit);
                state.settings.baud = static_cast<uint32_t>(number_field(json, "baud", 9600));
                state.settings.usb_host = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "usb_host"));
                cJSON_Delete(json);
            }
        }
    }
    nvs_close(handle);
}
bool save_settings(const Settings& settings) {
    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "ssid", settings.ssid.c_str());
    cJSON_AddStringToObject(json, "password", settings.password.c_str());
    cJSON_AddStringToObject(json, "ip", settings.ip.c_str());
    cJSON_AddStringToObject(json, "unit", settings.unit.c_str());
    cJSON_AddNumberToObject(json, "baud", settings.baud);
    cJSON_AddBoolToObject(json, "usb_host", settings.usb_host);
    const auto raw = json_string(json);
    cJSON_Delete(json);
    nvs_handle_t handle;
    if (nvs_open("gateway", NVS_READWRITE, &handle) != ESP_OK) return false;
    const bool saved = nvs_set_str(handle, "settings", raw.c_str()) == ESP_OK && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return saved;
}
void start_wifi() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t* netif = esp_netif_create_default_wifi_ap();
    esp_netif_ip_info_t ip {};
    ip.ip.addr = ipaddr_addr(state.settings.ip.c_str());
    ip.gw.addr = ip.ip.addr;
    ip.netmask.addr = ipaddr_addr("255.255.255.0");
    const auto stopped = esp_netif_dhcps_stop(netif);
    if (stopped != ESP_OK && stopped != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) ESP_ERROR_CHECK(stopped);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &ip));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(netif));
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    wifi_config_t config {};
    std::snprintf(reinterpret_cast<char*>(config.ap.ssid), sizeof(config.ap.ssid), "%s", state.settings.ssid.c_str());
    std::snprintf(reinterpret_cast<char*>(config.ap.password), sizeof(config.ap.password), "%s", state.settings.password.c_str());
    config.ap.ssid_len = state.settings.ssid.size();
    config.ap.channel = 6;
    config.ap.max_connection = 4;
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    config.ap.pmf_cfg.required = false;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Connect to %s, then open http://%s", state.settings.ssid.c_str(), state.settings.ip.c_str());
}
static void parser_task(void*) {
    RxChunk chunk {};
    StreamDecoder decoder(MAX_LINE);
    while (true) {
        if (xQueueReceive(rx_queue, &chunk, pdMS_TO_TICKS(250))) {
            if (!chunk.size) decoder.resync();
            const size_t errors = decoder.feed(chunk.data, chunk.size, accept_line);
            if (errors) { std::lock_guard<std::mutex> lock(state.mutex); state.malformed += errors; }
        }
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.pending_request && now_ms() - state.pending_since > 5000) {
            state.command_state = "timeout";
            state.command_message = "Nano did not acknowledge the command. Settings have not been confirmed.";
            state.pending_request = 0;
        }
    }
}
} // namespace rig
extern "C" void app_main() {
    // Preserve an existing NVS partition on failure instead of silently erasing it.
    ESP_ERROR_CHECK(nvs_flash_init());
    rig::load_settings();
    if (!rig::board_init()) ESP_LOGE("rig", "LCD board initialization failed; web diagnostics remain available");
    rig::init_storage();
    rig::rx_queue = xQueueCreate(48, sizeof(rig::RxChunk));
    rig::tx_queue = xQueueCreate(8, sizeof(rig::TxMessage));
    assert(rig::rx_queue && rig::tx_queue);
    rig::start_wifi();
    xTaskCreate(rig::parser_task, "telemetry", 6144, nullptr, 5, nullptr);
    rig::start_web();
    rig::start_touch_ui();
    if (rig::state.settings.usb_host) rig::start_usb();
    else { rig::state.usb_status = "PC service mode: select Nano USB on Device screen when connecting the Nano"; rig::start_pc_service(); }
}
