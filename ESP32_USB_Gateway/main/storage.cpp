#include "gateway.hpp"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_random.h"

namespace rig {
static sdmmc_card_t* mounted_card = nullptr;
static bool card_selftest_locked(std::string& error) {
    char path[100]; std::snprintf(path,sizeof(path),"/sdcard/thrust/check-%08lx.tmp",static_cast<unsigned long>(esp_random()));
    const char* payload="Thrust Tester microSD read/write verification\n";
    FILE* file=std::fopen(path,"wb");
    if(!file) { error="microSD is not writable."; return false; }
    const bool written=std::fwrite(payload,1,std::strlen(payload),file)==std::strlen(payload);
    const bool closed=std::fclose(file)==0;
    char buffer[100] {};
    file=std::fopen(path,"rb");
    bool read=false;
    if(file) { const size_t count=std::fread(buffer,1,sizeof(buffer)-1,file); read=count==std::strlen(payload)&&std::strcmp(buffer,payload)==0; std::fclose(file); }
    std::remove(path);
    if(!written||!closed||!read) { error="microSD write/read verification failed."; return false; }
    return true;
}
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
std::string run_path(const std::string& id, const char* suffix) { return "/sdcard/thrust/" + id + suffix; }
bool valid_run_id(const std::string& id) {
    return id.size() >= 5 && id.size() <= 36 && id.rfind("run-", 0) == 0 &&
        std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isalnum(c) || c == '-'; });
}
void refresh_storage_locked() {
    if (state.storage_ready) { uint64_t total=0, free=0; if (esp_vfs_fat_info("/sdcard", &total, &free)==ESP_OK) { state.storage_total=total; state.storage_used=total-free; } }
}
cJSON* list_runs_locked() {
    cJSON* array = cJSON_CreateArray();
    DIR* directory = opendir("/sdcard/thrust");
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
static bool mount_card_locked(std::string& error,bool allow_format=false) {
    state.storage_ready=false; state.sd_selftest_ok=false;
    state.storage_total=state.storage_used=0;
    if(mounted_card) { esp_vfs_fat_sdcard_unmount("/sdcard",mounted_card); mounted_card=nullptr; }
    sdmmc_host_t host=SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot=SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width=1; slot.clk=GPIO_NUM_2; slot.cmd=GPIO_NUM_1; slot.d0=GPIO_NUM_42;
    slot.d1=slot.d2=slot.d3=GPIO_NUM_NC;
    slot.flags|=SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_vfs_fat_sdmmc_mount_config_t config {};
    config.format_if_mount_failed=allow_format;
    config.max_files=10;
    config.allocation_unit_size=16*1024;
    const auto result=esp_vfs_fat_sdmmc_mount("/sdcard",&host,&slot,&config,&mounted_card);
    if(result!=ESP_OK) { mounted_card=nullptr; error="No readable FAT microSD: "+std::string(esp_err_to_name(result))+". Insert a FAT32 card and tap Remount."; state.storage_status=error; return false; }
    if(mkdir("/sdcard/thrust",0775)!=0&&errno!=EEXIST) { error="Could not create microSD thrust directory."; state.storage_status=error; return false; }
    state.card_name=mounted_card->cid.name;
    if(!card_selftest_locked(error)) { state.storage_status=error; return false; }
    state.storage_ready=true; state.sd_selftest_ok=true;
    state.storage_status="microSD mounted; read/write check passed";
    refresh_storage_locked();
    // Preserve CSV data from an interrupted run, marking only its metadata.
    cJSON* runs=list_runs_locked(); cJSON* run=nullptr;
    cJSON_ArrayForEach(run,runs) if(string_field(run,"status")=="recording") {
        cJSON_DeleteItemFromObject(run,"status"); cJSON_AddStringToObject(run,"status","interrupted");
        write_json(run_path(string_field(run,"id"),".json"),json_string(run));
    }
    cJSON_Delete(runs);
    return true;
}
void init_storage() {
    std::lock_guard<std::mutex> lock(state.mutex);
    std::string error;
    mount_card_locked(error);
    ESP_LOGI("storage","%s",state.storage_status.c_str());
}
bool remount_storage(std::string& error) {
    std::lock_guard<std::mutex> lock(state.mutex);
    if(state.recording_file||state.active_downloads) { error="Stop recording and finish downloads before remounting."; return false; }
    return mount_card_locked(error);
}
bool storage_selftest(std::string& error) {
    std::lock_guard<std::mutex> lock(state.mutex);
    if(!state.storage_ready||state.recording_file) { error="Mount the card and stop recording before the SD check."; return false; }
    state.sd_selftest_ok=card_selftest_locked(error);
    if(!state.sd_selftest_ok) { state.storage_ready=false; state.storage_status=error; }
    return state.sd_selftest_ok;
}
bool prepare_fresh_storage(std::string& error) {
    std::lock_guard<std::mutex> lock(state.mutex);
    if(state.recording_file||state.active_downloads) {error="Stop recording and downloads before preparing a fresh card.";return false;}
    return mount_card_locked(error,true);
}
bool start_recording_locked(const std::string& name, const std::string& note, double epoch_ms, std::string& error) {
    if (state.recording_file) { error = "A run is already recording."; return false; }
    if (!state.storage_ready) { error = "Recording storage is unavailable."; return false; }
    if (!state.usb_connected || state.history.empty() || now_ms() - state.history.back().gateway_ms > STALE_MS) {
        error = "Connect the Nano and wait for live measurements before recording."; return false;
    }
    refresh_storage_locked();
    if (state.storage_total - state.storage_used < 1024 * 1024) { error = "Storage is nearly full. Download and delete completed runs first."; return false; }
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
        if (state.storage_total - state.storage_used < 512 * 1024) { state.last_error = "Recording stopped: storage is full. Download and delete runs."; stop_recording_locked("storage_full"); }
    }
}

} // namespace rig
