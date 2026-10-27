#include "gateway.hpp"
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <sys/time.h>
#include "driver/usb_serial_jtag.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"

namespace rig {
static void status() {
    std::lock_guard<std::mutex> lock(state.mutex);
    cJSON* json=cJSON_CreateObject();
    cJSON_AddStringToObject(json,"board","ESP32-S3-Touch-LCD-2.8B");
    cJSON_AddStringToObject(json,"firmware","1.1.0-lcd28b");
    cJSON_AddBoolToObject(json,"display",state.display_ready);
    cJSON_AddBoolToObject(json,"touch",state.touch_ready);
    cJSON_AddNumberToObject(json,"touch_events",state.touch_events);
    cJSON_AddBoolToObject(json,"sd_mounted",state.storage_ready);
    cJSON_AddBoolToObject(json,"sd_read_write",state.sd_selftest_ok);
    cJSON_AddStringToObject(json,"sd_status",state.storage_status.c_str());
    cJSON_AddStringToObject(json,"card",state.card_name.c_str());
    cJSON_AddNumberToObject(json,"sd_total",static_cast<double>(state.storage_total));
    cJSON_AddNumberToObject(json,"sd_used",static_cast<double>(state.storage_used));
    cJSON_AddBoolToObject(json,"usb_host",state.settings.usb_host);
    cJSON_AddBoolToObject(json,"nano_connected",state.usb_connected);
    cJSON_AddStringToObject(json,"ssid",state.settings.ssid.c_str());
    cJSON_AddStringToObject(json,"ip",state.settings.ip.c_str());
    cJSON_AddNumberToObject(json,"free_heap",esp_get_free_heap_size());
    cJSON_AddNumberToObject(json,"free_psram",heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    const auto raw=json_string(json);cJSON_Delete(json);
    std::printf("RIG_STATUS %s\n",raw.c_str());std::fflush(stdout);
}
static void frame() {
    auto* snapshot=board_snapshot();
    if(!snapshot){std::puts("RIG_ERROR Framebuffer unavailable");return;}
    esp_log_level_set("*",ESP_LOG_NONE);
    std::printf("RIG_FRAME 480 640 921600\n");std::fflush(stdout);
    uint8_t row[480*3];
    for(unsigned y=0;y<640;++y) {
        for(unsigned x=0;x<480;++x) {
            const uint16_t value=snapshot[y*480+x];
            row[x*3]=((value>>11)&31)*255/31;row[x*3+1]=((value>>5)&63)*255/63;row[x*3+2]=(value&31)*255/31;
        }
        size_t offset=0;
        while(offset<sizeof(row)) {const int count=usb_serial_jtag_write_bytes(row+offset,sizeof(row)-offset,pdMS_TO_TICKS(1000));if(count<=0){heap_caps_free(snapshot);esp_log_level_set("*",ESP_LOG_INFO);return;}offset+=count;}
    }
    usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(2000));
    heap_caps_free(snapshot);esp_log_level_set("*",ESP_LOG_INFO);
}
static void service_task(void*) {
    std::string line;
    while(true) {
        uint8_t byte;
        if(usb_serial_jtag_read_bytes(&byte,1,pdMS_TO_TICKS(100))<=0)continue;
        if(byte=='\n') {
            if(line=="STATUS")status();
            else if(line=="SCREEN")frame();
            else if(line=="SDTEST"||line=="MOUNT_SD"||line=="PREPARE_FRESH_SD") {
                std::string error;
                const bool ok=line=="SDTEST"?storage_selftest(error):line=="MOUNT_SD"?remount_storage(error):prepare_fresh_storage(error);
                std::printf("RIG_SD %s %s\n",ok?"OK":"ERROR",error.c_str());std::fflush(stdout);status();
            } else if(line.rfind("CLOCK ",0)==0) {
                const int64_t epoch=std::strtoll(line.c_str()+6,nullptr,10);
                if(epoch>=1700000000&&epoch<4102444800LL) {timeval now {};now.tv_sec=epoch;settimeofday(&now,nullptr);std::puts("RIG_CLOCK OK");}
                else std::puts("RIG_CLOCK ERROR");
                std::fflush(stdout);
            } else if(line=="HELP") {std::puts("RIG_SERVICE STATUS | SDTEST | MOUNT_SD | PREPARE_FRESH_SD (format only if mount fails) | SCREEN | CLOCK unix_seconds");std::fflush(stdout);}
            line.clear();
        } else if(byte!='\r') {if(line.size()<100)line.push_back(static_cast<char>(byte));else line.clear();}
    }
}
void start_pc_service() {
    if(!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t config {};config.tx_buffer_size=8192;config.rx_buffer_size=1024;
        ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));
    }
    xTaskCreate(service_task,"pc_service",6144,nullptr,3,nullptr);
    std::puts("RIG_READY Touch LCD 2.8B / PC service mode. Send STATUS, SDTEST, or SCREEN.");std::fflush(stdout);
}
}
