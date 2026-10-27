#include "gateway.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include "esp_timer.h"

namespace rig {
int64_t now_ms() { return esp_timer_get_time() / 1000; }
std::string json_string(cJSON* json) {
    if (!json) return "null";
    char* raw = cJSON_PrintUnformatted(json);
    std::string text = raw ? raw : "null";
    cJSON_free(raw);
    return text;
}
std::string string_field(const cJSON* json, const char* key, const std::string& fallback) {
    const auto* item = cJSON_GetObjectItemCaseSensitive(json, key);
    return cJSON_IsString(item) ? item->valuestring : fallback;
}
double number_field(const cJSON* json, const char* key, double fallback) {
    const auto* item = cJSON_GetObjectItemCaseSensitive(json, key);
    return cJSON_IsNumber(item) && std::isfinite(item->valuedouble) ? item->valuedouble : fallback;
}
void console_locked(const std::string& text) {
    state.console.push_back({++state.console_sequence, text.substr(0, MAX_LINE)});
    while (state.console.size() > 40) state.console.pop_front();
}
bool has_capability_locked(const std::string& name) {
    return state.mode == "protocol_v1" && state.usb_connected &&
        std::find(state.capabilities.begin(), state.capabilities.end(), name) != state.capabilities.end();
}
static void numeric(cJSON* object, const char* key, double value) {
    if (std::isfinite(value)) cJSON_AddNumberToObject(object, key, value);
    else cJSON_AddNullToObject(object, key);
}
cJSON* sample_json(const Sample& sample) {
    auto* json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "seq", static_cast<double>(sample.seq));
    cJSON_AddNumberToObject(json, "gateway_ms", static_cast<double>(sample.gateway_ms));
    if (sample.has_nano_seq) cJSON_AddNumberToObject(json, "nano_seq", sample.nano_seq);
    numeric(json, "time_s", sample.time_s);
    numeric(json, "thrust", sample.thrust);
    numeric(json, "throttle", sample.throttle);
    numeric(json, "voltage", sample.voltage);
    numeric(json, "current", sample.current);
    numeric(json, "rpm", sample.rpm);
    numeric(json, "power", sample.power());
    return json;
}
// Parse labels individually: tabs/spaces, capitalization and optional debug fields
// can vary. A label must start the line or follow whitespace; "I:" cannot match RPM.
static bool labeled_number(const std::string& text, const char* label, double& value) {
    size_t pos = 0;
    while ((pos = text.find(label, pos)) != std::string::npos) {
        if (pos == 0 || std::isspace(static_cast<unsigned char>(text[pos - 1]))) {
            const char* start = text.c_str() + pos + std::strlen(label);
            char* end = nullptr;
            const double parsed = std::strtod(start, &end);
            if (end != start && std::isfinite(parsed) && (*end == '\0' || std::isspace(static_cast<unsigned char>(*end)))) { value = parsed; return true; }
        }
        ++pos;
    }
    return false;
}
bool parse_legacy(const std::string& line, Sample& sample) {
    std::string lower = line;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    return labeled_number(lower, "time:", sample.time_s) &&
        labeled_number(lower, "thrust:", sample.thrust) &&
        labeled_number(lower, "throttle:", sample.throttle) &&
        labeled_number(lower, "vbat:", sample.voltage) &&
        labeled_number(lower, "i:", sample.current) &&
        labeled_number(lower, "rpm:", sample.rpm);
}
static bool parse_csv(const std::string& line, Sample& sample) {
    // Original six-column SD format is also accepted as a live stream.
    double values[6];
    const char* next = line.c_str();
    for (int i = 0; i < 6; ++i) {
        char* end = nullptr;
        values[i] = std::strtod(next, &end);
        if (end == next || !std::isfinite(values[i])) return false;
        while (*end == ' ' || *end == '\t') ++end;
        if (i < 5 && *end != ',') return false;
        if (i == 5 && *end != '\0') return false;
        next = end + (i < 5 ? 1 : 0);
    }
    sample.time_s = values[0]; sample.thrust = values[1]; sample.throttle = values[2];
    sample.voltage = values[3]; sample.current = values[4]; sample.rpm = values[5];
    return true;
}
static bool parse_compact(const std::string& line, Sample& sample) {
    // S1,sequence,nano_time_ms,thrust,throttle,voltage,current,rpm
    // Required fields are finite; only the final four channels may be empty.
    double values[7];
    const char* next = line.c_str() + 3;
    for (int i = 0; i < 7; ++i) {
        if (i >= 3 && (*next == ',' || *next == '\0')) values[i] = NAN;
        else {
            char* end = nullptr;
            values[i] = std::strtod(next, &end);
            if (end == next || !std::isfinite(values[i])) return false;
            next = end;
        }
        if ((i < 6 && *next != ',') || (i == 6 && *next != '\0')) return false;
        if (i < 6) ++next;
    }
    for (int i = 0; i < 2; ++i)
        if (values[i] < 0 || values[i] > UINT32_MAX || std::floor(values[i]) != values[i]) return false;
    sample.nano_seq = static_cast<uint32_t>(values[0]);
    sample.has_nano_seq = true;
    sample.time_s = values[1] / 1000.0;
    sample.thrust = values[2]; sample.throttle = values[3];
    sample.voltage = values[4]; sample.current = values[5]; sample.rpm = values[6];
    return true;
}
void accept_line(const std::string& line) {
    if (line.empty()) return;
    std::lock_guard<std::mutex> lock(state.mutex);
    console_locked(line);
    Sample sample;
    bool valid = false;
    if (line.front() == '{') {
        cJSON* json = cJSON_ParseWithOpts(line.c_str(), nullptr, true);
        if (!json) { ++state.malformed; return; }
        if (number_field(json, "protocol", 0) != 1) { ++state.malformed; cJSON_Delete(json); return; }
        const auto type = string_field(json, "type");
        if (type == "hello" && number_field(json, "protocol", 0) == 1) {
            state.mode = "protocol_v1";
            state.remote_device = string_field(json, "device", "Measurement Nano");
            state.capabilities.clear();
            const cJSON* capability = nullptr;
            cJSON_ArrayForEach(capability, cJSON_GetObjectItemCaseSensitive(json, "capabilities")) {
                if (cJSON_IsString(capability) && state.capabilities.size() < 16)
                    state.capabilities.emplace_back(capability->valuestring);
            }
        } else if (type == "sample") {
            sample.time_s = number_field(json, "time_ms") / 1000.0;
            sample.thrust = number_field(json, "thrust");
            sample.throttle = number_field(json, "throttle");
            sample.voltage = number_field(json, "voltage");
            sample.current = number_field(json, "current");
            sample.rpm = number_field(json, "rpm");
            valid = number_field(json, "protocol", 0) == 1 && std::isfinite(sample.time_s) && std::isfinite(sample.thrust);
            if (!valid) ++state.malformed;
        } else if (type == "config") {
            state.remote_config = json_string(json);
        } else if (type == "ack") {
            const double request = number_field(json, "request_id", -1);
            if (state.pending_request && request == state.pending_request) {
                state.command_state = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "ok")) ? "success" : "error";
                state.command_message = string_field(json, "message", state.command_state);
                state.pending_request = 0;
            }
        } else if (type == "event") {
            state.last_error = string_field(json, "message");
        }
        cJSON_Delete(json);
    } else if (line.compare(0, 3, "S1,") == 0) {
        valid = parse_compact(line, sample);
        if (!valid) ++state.malformed;
    } else if (parse_legacy(line, sample)) {
        valid = true;
        if (state.mode != "protocol_v1") state.mode = "legacy_debug";
    } else if (parse_csv(line, sample)) {
        valid = true;
        if (state.mode != "protocol_v1") state.mode = "legacy_csv";
    }
    if (!valid) return;
    if (sample.has_nano_seq && !state.history.empty() && state.history.back().has_nano_seq) {
        const auto& previous = state.history.back();
        const uint32_t advance = sample.nano_seq - previous.nano_seq;
        // A counter rollover is valid; a reset/backward jump starts a new series.
        if (advance > 1 && advance < 0x80000000UL && sample.time_s >= previous.time_s)
            state.nano_gaps += advance - 1;
    }
    sample.gateway_ms = now_ms();
    sample.seq = ++state.sequence;
    state.history.push_back(sample);
    while (state.history.size() > HISTORY_SIZE) state.history.pop_front();
    append_recording_locked(sample);
}
} // namespace rig
