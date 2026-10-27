#pragma once

#include <array>
#include <cmath>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace rig {
constexpr size_t HISTORY_SIZE = 600;
constexpr size_t MAX_LINE = 768;
constexpr uint32_t STALE_MS = 3000;
struct Sample {
    uint64_t seq = 0;
    int64_t gateway_ms = 0;
    double time_s = NAN, thrust = NAN, throttle = NAN, voltage = NAN, current = NAN, rpm = NAN;
    double power() const { return voltage * current; }
};
struct Settings {
    std::string ssid = "ThrustTester";
    std::string password = "ThrustRig-2026";
    std::string ip = "192.168.4.1";
    std::string unit = "kgf";
    uint32_t baud = 9600;
    bool usb_host = false;
};
struct ConsoleLine { uint64_t seq; std::string text; };
struct State {
    std::mutex mutex;
    Settings settings;
    std::string mode = "waiting", usb_status = "Waiting for a USB serial device", last_error;
    uint16_t vid = 0, pid = 0;
    bool usb_connected = false, storage_ready = false;
    uint64_t sequence = 0, console_sequence = 0, malformed = 0, dropped = 0;
    int64_t connected_ms = 0;
    std::deque<Sample> history;
    std::deque<ConsoleLine> console;
    std::vector<std::string> capabilities;
    std::string remote_config = "{}", remote_device;
    uint32_t next_request = 1, pending_request = 0;
    int64_t pending_since = 0;
    std::string command_state = "idle", command_message, pending_command;
    FILE* recording_file = nullptr;
    std::string recording_id, recording_meta;
    uint64_t recorded_samples = 0;
    int64_t recording_started_ms = 0;
    size_t storage_total = 0, storage_used = 0;
    std::string storage_status = "No microSD card mounted";
    std::string card_name;
    bool display_ready = false, touch_ready = false, sd_selftest_ok = false;
    uint32_t touch_events = 0;
    unsigned active_downloads = 0;
};
extern State state;
extern QueueHandle_t rx_queue, tx_queue;
struct RxChunk { size_t size; uint8_t data[192]; };
struct TxMessage { char text[512]; };
int64_t now_ms();
void console_locked(const std::string& text);
bool has_capability_locked(const std::string& name);
void accept_line(const std::string& line);
bool parse_legacy(const std::string& line, Sample& sample);
cJSON* sample_json(const Sample& sample);
std::string json_string(cJSON* json);
std::string string_field(const cJSON* json, const char* key, const std::string& fallback = "");
double number_field(const cJSON* json, const char* key, double fallback = NAN);
void load_settings();
bool save_settings(const Settings& settings);
void init_storage();
bool remount_storage(std::string& error);
bool storage_selftest(std::string& error);
bool prepare_fresh_storage(std::string& error);
void refresh_storage_locked();
bool valid_run_id(const std::string& id);
std::string run_path(const std::string& id, const char* suffix);
cJSON* list_runs_locked();
bool start_recording_locked(const std::string& name, const std::string& note, double epoch_ms, std::string& error);
void stop_recording_locked(const char* reason = "complete");
void append_recording_locked(const Sample& sample);
void start_usb();
void start_web();
void start_wifi();
bool submit_command(const std::string& command, const cJSON* args, uint32_t& request, std::string& error);
void start_touch_ui();
void start_pc_service();
bool board_init();
bool board_touch(int& x, int& y);
void* board_panel();
void board_backlight(unsigned percent);
uint16_t* board_snapshot();
} // namespace rig
