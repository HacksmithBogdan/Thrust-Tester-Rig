#include "gateway.hpp"
#include <algorithm>
#include <cstring>
#include <sys/stat.h>
#include "esp_http_server.h"
#include "esp_system.h"
#include "lwip/inet.h"

extern const char html_start[] asm("_binary_index_html_start");
extern const char html_end[] asm("_binary_index_html_end");
extern const char css_start[] asm("_binary_styles_css_start");
extern const char css_end[] asm("_binary_styles_css_end");
extern const char js_start[] asm("_binary_app_js_start");
extern const char js_end[] asm("_binary_app_js_end");
extern const char model_start[] asm("_binary_model_js_start");
extern const char model_end[] asm("_binary_model_js_end");

namespace rig {
static esp_err_t send_json(httpd_req_t* req, cJSON* json, const char* status = "200 OK") {
    const auto text = json_string(json);
    cJSON_Delete(json);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    return httpd_resp_send(req, text.data(), text.size());
}
static esp_err_t error(httpd_req_t* req, const char* message, const char* status = "400 Bad Request") {
    auto* json = cJSON_CreateObject(); cJSON_AddStringToObject(json, "error", message);
    return send_json(req, json, status);
}
static cJSON* body(httpd_req_t* req) {
    char content_type[80] {};
    if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type)) != ESP_OK || std::strncmp(content_type, "application/json", 16) != 0) return nullptr;
    // Reject cross-origin browser commands. The WPA2 password remains the access
    // boundary; this also blocks commands submitted from an unrelated open tab.
    if (httpd_req_get_hdr_value_len(req, "Origin")) {
        char origin[128] {};
        const auto expected = "http://" + state.settings.ip;
        if (httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) != ESP_OK || (origin != expected && origin != expected + ":80")) return nullptr;
    }
    if (req->content_len == 0 || req->content_len > 2048) return nullptr;
    std::string data(req->content_len, '\0');
    size_t received = 0;
    while (received < data.size()) {
        const int bytes = httpd_req_recv(req, data.data() + received, data.size() - received);
        if (bytes <= 0) return nullptr;
        received += bytes;
    }
    return cJSON_ParseWithLength(data.data(), data.size());
}
static std::string query(httpd_req_t* req, const char* key) {
    char raw[256] {}, value[96] {};
    if (httpd_req_get_url_query_str(req, raw, sizeof(raw)) == ESP_OK && httpd_query_key_value(raw, key, value, sizeof(value)) == ESP_OK) return value;
    return {};
}
static esp_err_t status_handler(httpd_req_t* req) {
    std::unique_lock<std::mutex> lock(state.mutex);
    auto* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "firmware", "1.1.0");
    cJSON_AddNumberToObject(json, "uptime_ms", now_ms());
    cJSON_AddStringToObject(json, "mode", state.mode.c_str());
    cJSON_AddBoolToObject(json, "usb_connected", state.usb_connected);
    cJSON_AddStringToObject(json, "usb_status", state.usb_status.c_str());
    cJSON_AddNumberToObject(json, "vid", state.vid);
    cJSON_AddNumberToObject(json, "pid", state.pid);
    const double age = state.history.empty() ? -1 : now_ms() - state.history.back().gateway_ms;
    cJSON_AddNumberToObject(json, "sample_age_ms", age);
    cJSON_AddBoolToObject(json, "live", state.usb_connected && age >= 0 && age < STALE_MS);
    double span = 0, max_gap = 0;
    size_t intervals = 0;
    for (size_t i = state.history.size(); i > 1 && span < 1; --i) {
        const double delta = state.history[i - 1].time_s - state.history[i - 2].time_s;
        if (delta <= 0) break; // Nano restart/uptime rollover.
        span += delta; max_gap = std::max(max_gap, delta); ++intervals;
    }
    cJSON_AddNumberToObject(json, "sample_rate_hz", age >= 0 && age < STALE_MS && span > 0 ? intervals / span : 0);
    cJSON_AddNumberToObject(json, "max_sample_gap_ms", max_gap * 1000);
    cJSON_AddNumberToObject(json, "nano_sequence_gaps", static_cast<double>(state.nano_gaps));
    if (!state.history.empty()) cJSON_AddItemToObject(json, "sample", sample_json(state.history.back()));
    cJSON_AddStringToObject(json, "last_error", state.last_error.c_str());
    cJSON_AddNumberToObject(json, "malformed", state.malformed);
    cJSON_AddNumberToObject(json, "dropped", state.dropped);
    auto* caps = cJSON_AddArrayToObject(json, "capabilities");
    for (const auto& capability : state.capabilities) cJSON_AddItemToArray(caps, cJSON_CreateString(capability.c_str()));
    cJSON_AddItemToObject(json, "nano_config", cJSON_Parse(state.remote_config.c_str()));
    auto* settings = cJSON_AddObjectToObject(json, "settings");
    cJSON_AddStringToObject(settings, "ssid", state.settings.ssid.c_str());
    cJSON_AddStringToObject(settings, "ip", state.settings.ip.c_str());
    cJSON_AddStringToObject(settings, "unit", state.settings.unit.c_str());
    cJSON_AddNumberToObject(settings, "baud", state.settings.baud);
    auto* storage = cJSON_AddObjectToObject(json, "storage");
    cJSON_AddBoolToObject(storage, "ready", state.storage_ready);
    cJSON_AddNumberToObject(storage, "total", state.storage_total);
    cJSON_AddNumberToObject(storage, "used", state.storage_used);
    auto* recording = cJSON_AddObjectToObject(json, "recording");
    cJSON_AddBoolToObject(recording, "active", state.recording_file != nullptr);
    cJSON_AddStringToObject(recording, "id", state.recording_id.c_str());
    cJSON_AddNumberToObject(recording, "samples", state.recorded_samples);
    cJSON_AddNumberToObject(recording, "duration_s", state.recording_file ? (now_ms() - state.recording_started_ms) / 1000.0 : 0);
    auto* command = cJSON_AddObjectToObject(json, "command");
    cJSON_AddNumberToObject(command, "request_id", state.pending_request);
    cJSON_AddStringToObject(command, "state", state.command_state.c_str());
    cJSON_AddStringToObject(command, "message", state.command_message.c_str());
    lock.unlock();
    return send_json(req, json);
}
static esp_err_t samples_handler(httpd_req_t* req) {
    const uint64_t after = std::strtoull(query(req, "after").c_str(), nullptr, 10);
    std::unique_lock<std::mutex> lock(state.mutex);
    auto* json = cJSON_CreateObject();
    auto* samples = cJSON_AddArrayToObject(json, "samples");
    uint64_t next = after;
    size_t count = 0;
    for (const auto& sample : state.history) {
        if (sample.seq <= after) continue;
        cJSON_AddItemToArray(samples, sample_json(sample));
        next = sample.seq;
        if (++count == 256) break;
    }
    cJSON_AddNumberToObject(json, "next_seq", next);
    cJSON_AddBoolToObject(json, "gap", !state.history.empty() && after && after + 1 < state.history.front().seq);
    lock.unlock();
    return send_json(req, json);
}
static esp_err_t console_handler(httpd_req_t* req) {
    const uint64_t after = std::strtoull(query(req, "after").c_str(), nullptr, 10);
    std::unique_lock<std::mutex> lock(state.mutex);
    auto* json = cJSON_CreateObject();
    auto* lines = cJSON_AddArrayToObject(json, "lines");
    for (const auto& line : state.console) if (line.seq > after) {
        auto* item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "seq", line.seq);
        cJSON_AddStringToObject(item, "text", line.text.c_str());
        cJSON_AddItemToArray(lines, item);
    }
    lock.unlock();
    return send_json(req, json);
}
static esp_err_t runs_handler(httpd_req_t* req) {
    std::unique_lock<std::mutex> lock(state.mutex);
    auto* json = cJSON_CreateObject();
    cJSON_AddItemToObject(json, "runs", list_runs_locked());
    lock.unlock();
    return send_json(req, json);
}
static esp_err_t record_handler(httpd_req_t* req) {
    cJSON* json = body(req);
    if (!json) return error(req, "Expected JSON request.");
    const auto action = string_field(json, "action");
    const auto name = string_field(json, "name");
    const auto note = string_field(json, "note");
    const double epoch = number_field(json, "epoch_ms", 0);
    cJSON_Delete(json);
    if (name.size() > 80 || note.size() > 500) return error(req, "Name or notes are too long.");
    std::lock_guard<std::mutex> lock(state.mutex);
    if (action == "start") {
        std::string reason;
        if (!start_recording_locked(name, note, epoch, reason)) return error(req, reason.c_str(), "409 Conflict");
    } else if (action == "stop") stop_recording_locked();
    else return error(req, "Unknown recording action.");
    auto* reply = cJSON_CreateObject(); cJSON_AddBoolToObject(reply, "ok", true);
    return send_json(req, reply);
}
static esp_err_t download_handler(httpd_req_t* req) {
    const auto id = query(req, "id");
    const bool metadata = query(req, "format") == "json";
    if (!valid_run_id(id)) return error(req, "Invalid run identifier.");
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.recording_file && state.recording_id == id) return error(req, "Stop this run before opening or downloading it.", "409 Conflict");
    }
    FILE* file = std::fopen(run_path(id, metadata ? ".json" : ".csv").c_str(), "rb");
    if (!file) return error(req, "Run file not found.", "404 Not Found");
    const auto disposition = "attachment; filename=\"" + id + (metadata ? ".json\"" : ".csv\"");
    httpd_resp_set_type(req, metadata ? "application/json" : "text/csv; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Disposition", disposition.c_str());
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    char buffer[1024];
    size_t bytes;
    esp_err_t result = ESP_OK;
    while ((bytes = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if ((result = httpd_resp_send_chunk(req, buffer, bytes)) != ESP_OK) break;
    }
    std::fclose(file);
    if (result == ESP_OK) result = httpd_resp_send_chunk(req, nullptr, 0);
    return result;
}
static esp_err_t delete_handler(httpd_req_t* req) {
    auto* json = body(req);
    const auto id = string_field(json, "id");
    cJSON_Delete(json);
    if (!valid_run_id(id)) return error(req, "Invalid run identifier.");
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.recording_file && state.recording_id == id) return error(req, "Stop this run before deleting it.", "409 Conflict");
    if (std::remove(run_path(id, ".csv").c_str()) != 0) return error(req, "Run file could not be deleted.", "404 Not Found");
    std::remove(run_path(id, ".json").c_str());
    refresh_storage_locked();
    auto* reply = cJSON_CreateObject(); cJSON_AddBoolToObject(reply, "ok", true);
    return send_json(req, reply);
}
static esp_err_t command_handler(httpd_req_t* req) {
    auto* json = body(req);
    if (!json) return error(req, "Expected JSON request.");
    const auto command = string_field(json, "command");
    const char* allowed[] = {"tare", "calibrate", "set_config", "get_config", "save_config"};
    if (std::find(std::begin(allowed), std::end(allowed), command) == std::end(allowed)) { cJSON_Delete(json); return error(req, "Unknown command."); }
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!has_capability_locked(command)) { cJSON_Delete(json); return error(req, "This Nano firmware has not advertised support for that command.", "409 Conflict"); }
    if (state.recording_file) { cJSON_Delete(json); return error(req, "Stop recording before changing calibration.", "409 Conflict"); }
    if (state.pending_request) { cJSON_Delete(json); return error(req, "Wait for the current command to finish.", "409 Conflict"); }
    auto* args = cJSON_GetObjectItemCaseSensitive(json, "args");
    if (!cJSON_IsObject(args)) { cJSON_Delete(json); return error(req, "Command arguments must be an object."); }
    if (command == "calibrate") {
        const double load = number_field(args, "known_load");
        if (!std::isfinite(load) || load <= 0 || load > 100000) { cJSON_Delete(json); return error(req, "Enter a positive known load."); }
    }
    if (command == "set_config") {
        const double poles = number_field(args, "poles");
        const double factor = number_field(args, "calibration_factor");
        const double shunt = number_field(args, "shunt_ohms");
        const double divider = number_field(args, "divider_ratio");
        if (!std::isfinite(poles) || poles < 1 || poles > 220 || std::floor(poles) != poles || !std::isfinite(factor) || factor == 0 || std::abs(factor) > 10000000 || !std::isfinite(shunt) || shunt <= 0 || shunt > 1 || !std::isfinite(divider) || divider < 1 || divider > 1000) {
            cJSON_Delete(json); return error(req, "Check pole count, calibration factor, shunt resistance, and divider ratio.");
        }
    }
    const uint32_t request = state.next_request++;
    cJSON* packet = cJSON_CreateObject();
    cJSON_AddStringToObject(packet, "type", "command");
    cJSON_AddNumberToObject(packet, "protocol", 1);
    cJSON_AddNumberToObject(packet, "request_id", request);
    cJSON_AddStringToObject(packet, "command", command.c_str());
    cJSON_AddItemToObject(packet, "args", cJSON_Duplicate(args, true));
    const auto wire = json_string(packet) + "\n";
    cJSON_Delete(packet);
    cJSON_Delete(json);
    if (wire.size() >= sizeof(TxMessage::text)) return error(req, "Command is too long.");
    TxMessage message {};
    std::memcpy(message.text, wire.data(), wire.size());
    if (xQueueSend(tx_queue, &message, 0) != pdTRUE) return error(req, "USB command queue is full.", "503 Service Unavailable");
    state.pending_request = request;
    state.pending_since = now_ms();
    state.pending_command = command;
    state.command_state = "pending";
    state.command_message = "Waiting for Nano acknowledgement…";
    console_locked("[TX] " + wire.substr(0, wire.size() - 1));
    auto* reply = cJSON_CreateObject(); cJSON_AddNumberToObject(reply, "request_id", request);
    return send_json(req, reply, "202 Accepted");
}
static void restart_task(void*) { vTaskDelay(pdMS_TO_TICKS(2500)); esp_restart(); }
static esp_err_t settings_handler(httpd_req_t* req) {
    auto* json = body(req);
    if (!json) return error(req, "Expected JSON request.");
    std::lock_guard<std::mutex> lock(state.mutex);
    Settings settings = state.settings;
    settings.ssid = string_field(json, "ssid", settings.ssid);
    const auto password = string_field(json, "password");
    if (!password.empty()) settings.password = password;
    settings.ip = string_field(json, "ip", settings.ip);
    settings.unit = string_field(json, "unit", settings.unit);
    const double baud = number_field(json, "baud", settings.baud);
    cJSON_Delete(json);
    const uint32_t rates[] = {9600, 19200, 38400, 57600, 115200, 230400};
    if (std::find(std::begin(rates), std::end(rates), baud) == std::end(rates)) return error(req, "Unsupported baud rate.");
    settings.baud = static_cast<uint32_t>(baud);
    unsigned a, b, c, d; char trailing;
    const bool valid_ip = std::sscanf(settings.ip.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &trailing) == 4 && a == 192 && b == 168 && c <= 254 && d == 1;
    if (settings.ssid.empty() || settings.ssid.size() > 31 || settings.password.size() < 8 || settings.password.size() > 63 || !valid_ip || (settings.unit != "kgf" && settings.unit != "N" && settings.unit != "lbf")) return error(req, "Use an SSID of 1–31 bytes, password of 8–63 bytes, and IP 192.168.x.1 (x=0…254).");
    if (state.recording_file || state.pending_request) return error(req, "Stop recording and wait for commands before restarting.", "409 Conflict");
    if (!save_settings(settings)) return error(req, "Settings could not be saved.", "500 Internal Server Error");
    auto* reply = cJSON_CreateObject();
    cJSON_AddBoolToObject(reply, "ok", true);
    cJSON_AddBoolToObject(reply, "restarting", true);
    cJSON_AddStringToObject(reply, "ip", settings.ip.c_str());
    xTaskCreate(restart_task, "restart", 2048, nullptr, 2, nullptr);
    return send_json(req, reply);
}
static esp_err_t asset_handler(httpd_req_t* req) {
    const char* begin = html_start;
    const char* end = html_end;
    const char* type = "text/html; charset=utf-8";
    if (std::strcmp(req->uri, "/styles.css") == 0) { begin = css_start; end = css_end; type = "text/css"; }
    else if (std::strcmp(req->uri, "/app.js") == 0) { begin = js_start; end = js_end; type = "text/javascript"; }
    else if (std::strcmp(req->uri, "/model.js") == 0) { begin = model_start; end = model_end; type = "text/javascript"; }
    httpd_resp_set_type(req, type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    return httpd_resp_send(req, begin, end - begin - 1);
}
void start_web() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.stack_size = 12288;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 5;
    config.send_wait_timeout = 5;
    httpd_handle_t server = nullptr;
    ESP_ERROR_CHECK(httpd_start(&server, &config));
    struct Route { const char* path; httpd_method_t method; esp_err_t (*handler)(httpd_req_t*); };
    const Route routes[] = {
        {"/", HTTP_GET, asset_handler}, {"/styles.css", HTTP_GET, asset_handler},
        {"/app.js", HTTP_GET, asset_handler}, {"/model.js", HTTP_GET, asset_handler},
        {"/api/status", HTTP_GET, status_handler}, {"/api/samples", HTTP_GET, samples_handler},
        {"/api/console", HTTP_GET, console_handler}, {"/api/runs", HTTP_GET, runs_handler},
        {"/api/download", HTTP_GET, download_handler}, {"/api/record", HTTP_POST, record_handler},
        {"/api/delete", HTTP_POST, delete_handler}, {"/api/command", HTTP_POST, command_handler},
        {"/api/settings", HTTP_POST, settings_handler}
    };
    for (const auto& route : routes) {
        httpd_uri_t uri {};
        uri.uri = route.path; uri.method = route.method; uri.handler = route.handler;
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri));
    }
}
} // namespace rig
