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
    const auto compact_start = state.history.size();
    accept_line("S1,100,3000,0.12500,,48.00000,2.00000,18000.0");
    assert(state.history.size() == compact_start + 1 && state.mode == "protocol_v1");
    assert(state.history.back().has_nano_seq && state.history.back().nano_seq == 100);
    assert(state.history.back().time_s == 3 && state.history.back().thrust == 0.125);
    assert(std::isnan(state.history.back().throttle) && state.history.back().power() == 96);
    accept_line("S1,101,3012,-0.12500,,,,");
    assert(state.history.size() == compact_start + 2 && std::isnan(state.history.back().voltage));
    accept_line("S1,104,3050,1,50,48,2,18000");
    assert(state.nano_gaps == 2);
    accept_line("S1,1,50,1,50,48,2,18000"); // Nano restart must not count billions of gaps.
    assert(state.nano_gaps == 2);
    const auto valid_compact = state.history.size();
    const std::string bad_lines[] = {
        "S1,1,50,,50,48,2,18000", "S1,1,50,nan,50,48,2,18000",
        "S1,1,50,1,50,inf,2,18000", "S1,1,50,1,50,48,2",
        "S1,1,50,1,50,48,2,18000,extra", "S1,1.5,50,1,50,48,2,18000",
        "S1,4294967296,50,1,50,48,2,18000", "S1,1,-1,1,50,48,2,18000",
        "S1,1,50,1,50,48,2,18000tail", "S1,,,,,,,"
    };
    const auto malformed_before = state.malformed;
    for (const auto& invalid : bad_lines) accept_line(invalid);
    assert(state.history.size() == valid_compact && state.malformed == malformed_before + 10);
    accept_line("S1,4294967295,500,1,,,,");
    const auto gaps_before_wrap = state.nano_gaps;
    accept_line("S1,0,513,1,,,,");
    assert(state.nano_gaps == gaps_before_wrap);
    // A full short pulse split at arbitrary USB chunk boundaries retains every
    // acquisition timestamp and sequence, independently of browser refreshes.
    StreamDecoder fast_decoder(MAX_LINE);
    std::string pulse;
    for (unsigned i = 0; i < 48; ++i)
        pulse += "S1," + std::to_string(i + 1) + "," + std::to_string(1000 + i * 25 / 2) + ",0.25,,48,2,0\n";
    const auto before_pulse = state.history.size();
    for (size_t offset = 0; offset < pulse.size(); offset += 17)
        assert(fast_decoder.feed(reinterpret_cast<const uint8_t*>(pulse.data() + offset), std::min(size_t(17), pulse.size() - offset), accept_line) == 0);
    assert(state.history.size() == before_pulse + 48 && state.history.back().nano_seq == 48);
    assert(state.history.back().time_s == 1.587);
    for (int i = 0; i < 650; ++i) accept_line(debug);
    assert(state.history.size() == HISTORY_SIZE && state.console.size() <= 40);
    state.usb_connected = false;
    assert(!has_capability_locked("tare"));
    std::cout << "PASS: legacy/JSON/S1 samples, missing channels, sequence gaps/reset/wrap, fragmented 48-sample pulse, strict framing, capabilities, ACK matching, bounded history\n";
}
