#include "gateway.hpp"
#include <algorithm>
#include <cstring>

namespace rig {
bool submit_command(const std::string& command, const cJSON* args, uint32_t& request, std::string& error) {
    const char* allowed[] = {"tare", "calibrate", "set_config", "get_config", "save_config"};
    if (std::find(std::begin(allowed), std::end(allowed), command) == std::end(allowed)) { error = "Unknown command."; return false; }
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!has_capability_locked(command)) { error = "The Nano has not advertised support for this USB command."; return false; }
    if (state.recording_file) { error = "Stop recording before changing calibration."; return false; }
    if (state.pending_request) { error = "Wait for the current Nano command to finish."; return false; }
    if (!cJSON_IsObject(args)) { error = "Command arguments must be an object."; return false; }
    if (command == "calibrate") {
        const double load = number_field(args, "known_load");
        if (!std::isfinite(load) || load <= 0 || load > 100000) { error = "Enter a positive known load."; return false; }
    }
    if (command == "set_config") {
        const double poles = number_field(args, "poles"), factor = number_field(args, "calibration_factor");
        const double shunt = number_field(args, "shunt_ohms"), divider = number_field(args, "divider_ratio");
        if (!std::isfinite(poles) || poles < 1 || poles > 220 || std::floor(poles) != poles || !std::isfinite(factor) || factor == 0 || std::abs(factor) > 10000000 || !std::isfinite(shunt) || shunt <= 0 || shunt > 1 || !std::isfinite(divider) || divider < 1 || divider > 1000) {
            error = "Check poles, calibration factor, shunt resistance, and divider ratio."; return false;
        }
    }
    request = state.next_request++;
    cJSON* packet = cJSON_CreateObject();
    cJSON_AddStringToObject(packet, "type", "command");
    cJSON_AddNumberToObject(packet, "protocol", 1);
    cJSON_AddNumberToObject(packet, "request_id", request);
    cJSON_AddStringToObject(packet, "command", command.c_str());
    cJSON_AddItemToObject(packet, "args", cJSON_Duplicate(args, true));
    const auto wire = json_string(packet) + "\n";
    cJSON_Delete(packet);
    if (wire.size() >= sizeof(TxMessage::text)) { error = "Command is too long."; return false; }
    TxMessage message {};
    std::memcpy(message.text, wire.data(), wire.size());
    if (xQueueSend(tx_queue, &message, 0) != pdTRUE) { error = "USB command queue is full."; return false; }
    state.pending_request = request;
    state.pending_since = now_ms();
    state.pending_command = command;
    state.command_state = "pending";
    state.command_message = "Waiting for Nano acknowledgement...";
    console_locked("[TX] " + wire.substr(0, wire.size() - 1));
    return true;
}
}
