// Runs the real production telemetry.cpp with only clock/storage/RTOS boundaries stubbed.
#include "gateway.hpp"
#include "stream_decoder.hpp"
#include <cassert>
#include <iostream>

namespace rig {
State state;
QueueHandle_t rx_queue = nullptr, tx_queue = nullptr;
unsigned appended = 0;
void append_recording_locked(const Sample&) { ++appended; }
}
extern "C" int64_t esp_timer_get_time() { return 12345000; }

int main() {
    using namespace rig;
    StreamDecoder decoder(32);
    std::vector<std::string> lines;
    auto feed = [&](const std::string& chunk) { return decoder.feed(reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size(), [&](const std::string& line) { lines.push_back(line); }); };
    feed("first\r"); feed("\nsecond\nthi"); feed("rd\n");
    assert((lines == std::vector<std::string>{"first", "second", "third"}));
    assert(feed(std::string(40, 'x') + "\nvalid\n") == 1);
    assert(lines.back() == "valid" && lines.size() == 4);
    feed("partial"); decoder.resync(); feed("tail\nnew\n");
    assert(lines.back() == "new" && lines.size() == 5);
    assert(feed(std::string("bad\0data\n", 9)) == 1);
    Sample sample;
    const std::string debug = "Time: 12.30\tThrust: 4.56\tThrottle: 70.0\tVBat: 46.80\tI: 85.20\tRPM: 18000.0\tTrim: 0\tTrainerSW: 1500\tnPoles: 20";
    assert(parse_legacy(debug, sample));
    assert(sample.time_s == 12.3 && sample.thrust == 4.56 && sample.voltage == 46.8 && sample.current == 85.2 && sample.rpm == 18000);
    accept_line(debug);
    assert(state.history.size() == 1 && state.mode == "legacy_debug" && appended == 1);
    assert(state.history.back().gateway_ms == 12345);
    accept_line("Initializing SD card...");
    accept_line("Time: 1 Thrust: nan Throttle: 0 VBat: 24 I: 0 RPM: 0");
    accept_line("Time: 1 Thrust: 3 Throttle: 0 VBat: 24 I: 0");
    accept_line("Time: 1 Thrust: 3x Throttle: 0 VBat: 24 I: 0 RPM: 0");
    assert(state.history.size() == 1);
    accept_line("1.2,-3.4,0,24,12,5000");
    assert(state.history.size() == 2 && state.mode == "legacy_csv" && state.history.back().thrust == -3.4);
    accept_line("Time, Thrust, Throttle, Vbat, Current, RPM");
    assert(state.history.size() == 2);
    state.usb_connected = true;
    accept_line(R"({"type":"hello","protocol":2,"capabilities":["tare"]})");
    assert(!has_capability_locked("tare"));
    accept_line(R"({"type":"sample","protocol":1,"time_ms":2500,"thrust":0.25,"throttle":null,"voltage":48,"current":2,"rpm":null})");
    assert(!has_capability_locked("tare"));
    assert(state.history.back().time_s == 2.5 && std::isnan(state.history.back().rpm));
    assert(state.history.back().power() == 96);
    cJSON* json = sample_json(state.history.back());
    assert(cJSON_IsNull(cJSON_GetObjectItem(json, "rpm")));
    assert(cJSON_IsNull(cJSON_GetObjectItem(json, "throttle")));
    cJSON_Delete(json);
    const auto before = state.history.size();
    accept_line(R"({"type":"sample","protocol":1,"time_ms":2,"thrust":3}garbage)");
    accept_line(R"({"type":"sample","protocol":1,"time_ms":2,"thrust":null})");
    accept_line(R"({"type":"sample","protocol":2,"time_ms":2,"thrust":3})");
    assert(state.history.size() == before);
    accept_line(R"({"type":"hello","protocol":1,"device":"Test Nano","capabilities":["tare","get_config"]})");
    assert(has_capability_locked("tare") && !has_capability_locked("save_config"));
    state.pending_request = 17;
    state.command_state = "pending";
    accept_line(R"({"type":"ack","protocol":1,"request_id":16,"ok":true,"message":"wrong request"})");
    assert(state.pending_request == 17 && state.command_state == "pending");
    accept_line(R"({"type":"ack","protocol":1,"request_id":17,"ok":false,"message":"sensor unavailable"})");
    assert(state.pending_request == 0 && state.command_state == "error" && state.command_message == "sensor unavailable");
    accept_line(R"({"type":"config","protocol":1,"calibration_factor":30310,"poles":20})");
    assert(state.remote_config.find("30310") != std::string::npos);
    state.pending_request = 18;
    accept_line(R"({"type":"ack","protocol":1,"request_id":18,"ok":true,"message":"Tared"})");
    assert(state.command_state == "success" && state.pending_request == 0);
    for (int i = 0; i < 650; ++i) accept_line(debug);
    assert(state.history.size() == HISTORY_SIZE && state.console.size() <= 40);
    state.usb_connected = false;
    assert(!has_capability_locked("tare"));
    std::cout << "PASS: legacy debug/CSV, JSON samples, unavailable channels, strict framing, capabilities, ACK matching, config, bounded history\n";
}
