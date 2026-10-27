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
#include "esp_spiffs.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "lwip/inet.h"

namespace rig {
State state;
QueueHandle_t rx_queue = nullptr, tx_queue = nullptr;
static const char* TAG = "rig";
static std::string read_file(const std::string& path, size_t limit = 4096) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return {};
    std::string value(limit, '\0');
    value.resize(std::fread(value.data(), 1, limit, file));
    std::fclose(file);
    return value;
}
static bool write_json(const std::string& path, const std::string& text) {
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) return false;
    const bool written = std::fwrite(text.data(), 1, text.size(), file) == text.size();
    return std::fclose(file) == 0 && written;
}
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
    const auto raw = json_string(json);
    cJSON_Delete(json);
    nvs_handle_t handle;
    if (nvs_open("gateway", NVS_READWRITE, &handle) != ESP_OK) return false;
    const bool saved = nvs_set_str(handle, "settings", raw.c_str()) == ESP_OK && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return saved;
}
std::string run_path(const std::string& id, const char* suffix) { return "/store/" + id + suffix; }
bool valid_run_id(const std::string& id) {
    return id.size() >= 5 && id.size() <= 36 && id.rfind("run-", 0) == 0 &&
        std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isalnum(c) || c == '-'; });
}
void refresh_storage_locked() {
    if (state.storage_ready) esp_spiffs_info("storage", &state.storage_total, &state.storage_used);
}
cJSON* list_runs_locked() {
    cJSON* array = cJSON_CreateArray();
    DIR* directory = opendir("/store");
    if (!directory) return array;
    dirent* entry;
    size_t count = 0;
    while ((entry = readdir(directory)) && count < 64) {
        const std::string name(entry->d_name);
        if (name.size() < 10 || name.substr(name.size() - 5) != ".json") continue;
        const std::string id = name.substr(0, name.size() - 5);
        if (!valid_run_id(id)) continue;
        cJSON* meta = cJSON_Parse(read_file(run_path(id, ".json")).c_str());
        if (!meta) continue;
        struct stat info {};
        if (stat(run_path(id, ".csv").c_str(), &info) == 0) cJSON_AddNumberToObject(meta, "bytes", info.st_size);
        if (id == state.recording_id && state.recording_file) {
            cJSON_DeleteItemFromObject(meta, "samples");
            cJSON_AddNumberToObject(meta, "samples", state.recorded_samples);
        }
        cJSON_AddItemToArray(array, meta);
        ++count;
    }
    closedir(directory);
    return array;
}
void init_storage() {
    nvs_handle_t handle;
    uint8_t initialized = 0;
    if (nvs_open("gateway", NVS_READWRITE, &handle) != ESP_OK) return;
    nvs_get_u8(handle, "fs_ready", &initialized);
    esp_vfs_spiffs_conf_t config {};
    config.base_path = "/store";
    config.partition_label = "storage";
    config.max_files = 6;
    config.format_if_mount_failed = !initialized;
    const auto err = esp_vfs_spiffs_register(&config);
    state.storage_ready = err == ESP_OK;
    if (state.storage_ready) { nvs_set_u8(handle, "fs_ready", 1); nvs_commit(handle); }
    else state.last_error = "Recording storage did not mount. Existing storage was not automatically erased.";
    nvs_close(handle);
    if (!state.storage_ready) return;
    refresh_storage_locked();
    // Recover recording metadata after interrupted power. Never silently resume a run.
    cJSON* runs = list_runs_locked();
    cJSON* run = nullptr;
    cJSON_ArrayForEach(run, runs) {
        if (string_field(run, "status") == "recording") {
            cJSON_DeleteItemFromObject(run, "status");
            cJSON_AddStringToObject(run, "status", "interrupted");
            write_json(run_path(string_field(run, "id"), ".json"), json_string(run));
        }
    }
    cJSON_Delete(runs);
}
bool start_recording_locked(const std::string& name, const std::string& note, double epoch_ms, std::string& error) {
    if (state.recording_file) { error = "A run is already recording."; return false; }
    if (!state.storage_ready) { error = "Recording storage is unavailable."; return false; }
    if (!state.usb_connected || state.history.empty() || now_ms() - state.history.back().gateway_ms > STALE_MS) {
        error = "Connect the Nano and wait for live measurements before recording."; return false;
    }
    refresh_storage_locked();
    if (state.storage_used > state.storage_total * 0.70) { error = "Storage is nearly full. Download and delete completed runs first."; return false; }
    cJSON* existing_runs = list_runs_locked();
    const int run_count = cJSON_GetArraySize(existing_runs);
    cJSON_Delete(existing_runs);
    if (run_count >= 64) { error = "The 64-run library is full. Download and delete completed runs first."; return false; }
    char id[40];
    std::snprintf(id, sizeof(id), "run-%08lx-%08lx", static_cast<unsigned long>(now_ms()), static_cast<unsigned long>(esp_random()));
    state.recording_id = id;
    cJSON* meta = cJSON_CreateObject();
    cJSON_AddStringToObject(meta, "id", id);
    cJSON_AddStringToObject(meta, "name", name.empty() ? "Untitled test" : name.c_str());
    cJSON_AddStringToObject(meta, "note", note.c_str());
    cJSON_AddStringToObject(meta, "status", "recording");
    cJSON_AddStringToObject(meta, "thrust_unit", state.settings.unit.c_str());
    cJSON_AddStringToObject(meta, "source", state.mode.c_str());
    cJSON_AddNumberToObject(meta, "started_epoch_ms", std::isfinite(epoch_ms) ? epoch_ms : 0);
    cJSON_AddNumberToObject(meta, "started_gateway_ms", now_ms());
    cJSON_AddNumberToObject(meta, "samples", 0);
    cJSON* remote = cJSON_Parse(state.remote_config.c_str());
    if (remote) cJSON_AddItemToObject(meta, "nano_config", remote);
    state.recording_meta = json_string(meta);
    cJSON_Delete(meta);
    if (!write_json(run_path(id, ".json"), state.recording_meta)) { error = "Could not save run metadata."; return false; }
    state.recording_file = std::fopen(run_path(id, ".csv").c_str(), "wb");
    if (!state.recording_file) { std::remove(run_path(id, ".json").c_str()); error = "Could not create run file."; return false; }
    const char* header = "gateway_time_s,nano_time_s,thrust,throttle_percent,voltage_V,current_A,rpm,power_W\n";
    if (std::fputs(header, state.recording_file) < 0) { stop_recording_locked("storage_error"); error = "Could not write CSV header."; return false; }
    std::fflush(state.recording_file);
    state.recorded_samples = 0;
    state.recording_started_ms = now_ms();
    return true;
}
void stop_recording_locked(const char* reason) {
    if (!state.recording_file) return;
    const bool close_ok = std::fclose(state.recording_file) == 0;
    state.recording_file = nullptr;
    cJSON* meta = cJSON_Parse(state.recording_meta.c_str());
    if (meta) {
        cJSON_DeleteItemFromObject(meta, "status");
        cJSON_DeleteItemFromObject(meta, "samples");
        cJSON_AddStringToObject(meta, "status", close_ok ? reason : "storage_error");
        cJSON_AddNumberToObject(meta, "samples", state.recorded_samples);
        cJSON_AddNumberToObject(meta, "duration_s", (now_ms() - state.recording_started_ms) / 1000.0);
        write_json(run_path(state.recording_id, ".json"), json_string(meta));
        cJSON_Delete(meta);
    }
    refresh_storage_locked();
}
void append_recording_locked(const Sample& sample) {
    if (!state.recording_file) return;
    auto cell = [](double value) { char text[40]; if (!std::isfinite(value)) return std::string(); std::snprintf(text, sizeof(text), "%.5f", value); return std::string(text); };
    const std::string line = cell(sample.gateway_ms / 1000.0) + "," + cell(sample.time_s) + "," + cell(sample.thrust) + "," + cell(sample.throttle) + "," + cell(sample.voltage) + "," + cell(sample.current) + "," + cell(sample.rpm) + "," + cell(sample.power()) + "\n";
    if (std::fwrite(line.data(), 1, line.size(), state.recording_file) != line.size()) {
        state.last_error = "Recording stopped: storage write failed."; stop_recording_locked("storage_error"); return;
    }
    ++state.recorded_samples;
    if (state.recorded_samples % 10 == 0) {
        if (std::fflush(state.recording_file) != 0) { state.last_error = "Recording stopped: storage flush failed."; stop_recording_locked("storage_error"); return; }
        refresh_storage_locked();
        if (state.storage_used > state.storage_total * 0.75) { state.last_error = "Recording stopped: storage is full. Download and delete runs."; stop_recording_locked("storage_full"); }
    }
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
    rig::init_storage();
    rig::rx_queue = xQueueCreate(48, sizeof(rig::RxChunk));
    rig::tx_queue = xQueueCreate(8, sizeof(rig::TxMessage));
    assert(rig::rx_queue && rig::tx_queue);
    rig::start_wifi();
    xTaskCreate(rig::parser_task, "telemetry", 6144, nullptr, 5, nullptr);
    rig::start_web();
    rig::start_usb();
}
