#include "gateway.hpp"
#include <atomic>
#include <cstring>
#include <memory>
#include "esp_log.h"
#include "usb/usb_host.h"
#include "usb/cdc_acm_host.h"
#include "usb/vcp.hpp"
#include "usb/vcp_ch34x.hpp"
#include "usb/vcp_cp210x.hpp"
#include "usb/vcp_ftdi.hpp"

namespace rig {
static std::atomic<bool> disconnected{false};
static const char* TAG = "usb_bridge";
static bool receive_data(const uint8_t* data, size_t length, void*) {
    for (size_t offset = 0; offset < length;) {
        RxChunk chunk {};
        chunk.size = std::min(sizeof(chunk.data), length - offset);
        std::memcpy(chunk.data, data + offset, chunk.size);
        if (xQueueSend(rx_queue, &chunk, 0) != pdTRUE) {
            // Drop the backlog and force line resynchronization instead of joining
            // fragments from opposite sides of a missing packet.
            xQueueReset(rx_queue);
            RxChunk reset {};
            xQueueSend(rx_queue, &reset, 0);
            std::lock_guard<std::mutex> lock(state.mutex);
            ++state.dropped;
        }
        offset += chunk.size;
    }
    return true;
}
static void device_event(const cdc_acm_host_dev_event_data_t* event, void*) {
    if (event->type == CDC_ACM_HOST_DEVICE_DISCONNECTED) disconnected = true;
    else if (event->type == CDC_ACM_HOST_ERROR) {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.last_error = "USB serial transfer error: " + std::to_string(event->data.error);
    }
}
static void new_device(usb_device_handle_t device) {
    const usb_device_desc_t* descriptor = nullptr;
    if (usb_host_get_device_descriptor(device, &descriptor) == ESP_OK) {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.vid = descriptor->idVendor;
        state.pid = descriptor->idProduct;
        state.usb_status = "USB device detected; opening supported serial converter";
    }
}
static void host_events(void*) {
    while (true) {
        uint32_t flags = 0;
        const auto result = usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (result != ESP_OK) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
    }
}
static void usb_worker(void*) {
    using namespace esp_usb;
    VCP::register_driver<FT23x>();
    VCP::register_driver<CP210x>();
    VCP::register_driver<CH34x>();
    while (true) {
        disconnected = false;
        cdc_acm_host_device_config_t config {};
        config.connection_timeout_ms = 1500;
        config.in_buffer_size = 512;
        config.out_buffer_size = 512;
        config.data_cb = receive_data;
        config.event_cb = device_event;
        std::unique_ptr<CdcAcmDevice> device(VCP::open(&config));
        if (!device) {
            std::lock_guard<std::mutex> lock(state.mutex);
            state.usb_status = state.vid ? "USB detected, but no supported serial converter opened" : "Waiting for USB: check cable and 5 V host power";
            continue;
        }
        uint32_t baud;
        { std::lock_guard<std::mutex> lock(state.mutex); baud = state.settings.baud; }
        cdc_acm_line_coding_t coding {};
        coding.dwDTERate = baud;
        coding.bDataBits = 8;
        if (device->line_coding_set(&coding) != ESP_OK) {
            std::lock_guard<std::mutex> lock(state.mutex);
            state.last_error = "Could not configure USB serial baud rate.";
            continue;
        }
        // Avoid intentionally pulsing the classic Nano's auto-reset input.
        const auto control = device->set_control_line_state(false, false);
        if (control != ESP_OK) ESP_LOGW(TAG, "Control-line request: %s", esp_err_to_name(control));
        xQueueReset(tx_queue);
        xQueueReset(rx_queue);
        RxChunk reset {};
        xQueueSend(rx_queue, &reset, 0);
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            state.usb_connected = true;
            state.connected_ms = now_ms();
            state.mode = "waiting";
            state.capabilities.clear();
            state.remote_config = "{}";
            state.usb_status = "Serial converter connected at " + std::to_string(baud) + " baud";
            console_locked("[USB connected]");
        }
        // Legacy firmware ignores input. Updated firmware replies to this discovery
        // request with HELLO/capabilities; no settings-changing command is automatic.
        int64_t hello_at = now_ms() + 2500;
        while (!disconnected) {
            bool discovered;
            { std::lock_guard<std::mutex> lock(state.mutex); discovered = state.mode == "protocol_v1"; }
            if (!discovered && now_ms() >= hello_at) {
                uint8_t hello[] = "{\"type\":\"command\",\"protocol\":1,\"request_id\":0,\"command\":\"hello\",\"args\":{}}\n";
                device->tx_blocking(hello, sizeof(hello) - 1, 500);
                // Retry if a startup message or old wrong-baud fragment ate hello.
                hello_at = now_ms() + 5000;
            }
            TxMessage message {};
            if (xQueueReceive(tx_queue, &message, pdMS_TO_TICKS(100))) {
                const auto result = device->tx_blocking(reinterpret_cast<uint8_t*>(message.text), std::strlen(message.text), 1000);
                if (result != ESP_OK) {
                    std::lock_guard<std::mutex> lock(state.mutex);
                    state.command_state = "error";
                    state.command_message = "USB write failed; command was not confirmed.";
                    state.pending_request = 0;
                    state.last_error = esp_err_to_name(result);
                }
            }
        }
        device.reset(); // Closing from the receive callback would deadlock the driver.
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            state.usb_connected = false;
            state.capabilities.clear();
            state.usb_status = "USB disconnected; waiting to reconnect";
            state.mode = "waiting";
            state.vid = state.pid = 0;
            if (state.pending_request) { state.command_state = "error"; state.command_message = "USB disconnected before acknowledgement."; state.pending_request = 0; }
            console_locked("[USB disconnected]");
            // Keep recording across brief reconnects; receive timestamps reveal the gap.
        }
    }
}
void start_usb() {
    usb_host_config_t config {};
    config.intr_flags = ESP_INTR_FLAG_LEVEL1;
    const auto installed = usb_host_install(&config);
    if (installed != ESP_OK) {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.usb_status = "USB host initialization failed";
        state.last_error = esp_err_to_name(installed);
        return;
    }
    xTaskCreate(host_events, "usb_events", 4096, nullptr, 12, nullptr);
    const auto driver = cdc_acm_host_install(nullptr);
    if (driver != ESP_OK) { state.last_error = esp_err_to_name(driver); return; }
    cdc_acm_host_register_new_dev_callback(new_device);
    xTaskCreate(usb_worker, "usb_serial", 6144, nullptr, 6, nullptr);
}
} // namespace rig
