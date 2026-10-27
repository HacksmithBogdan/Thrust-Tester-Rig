// Board-specific wiring and panel register data: Waveshare Touch LCD 2.8B.
// Implementation uses ESP-IDF APIs; see BOARD_NOTES.md for hardware provenance.
#include "gateway.hpp"
#include <algorithm>
#include <cstring>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

namespace rig {
static constexpr int WIDTH=480, HEIGHT=640;
static esp_lcd_panel_handle_t panel=nullptr;
static i2c_master_bus_handle_t i2c_bus=nullptr;
static i2c_master_dev_handle_t expander=nullptr, touch=nullptr;
static uint8_t expander_output=0x0F;
static bool pressed=false;
static int last_x=0,last_y=0;
static int64_t last_touch_ms=0;
static unsigned touch_width=WIDTH,touch_height=HEIGHT;
struct PanelCommand { uint8_t command; uint8_t data[16]; uint8_t length; uint16_t delay_ms; };
#include "panel_init.inc"

static esp_err_t exp_write(uint8_t reg,uint8_t value) {
    const uint8_t bytes[]={reg,value}; return i2c_master_transmit(expander,bytes,sizeof(bytes),200);
}
static esp_err_t exp_set(unsigned bit,bool high) {
    if(high) expander_output|=1U<<bit; else expander_output&=~(1U<<bit);
    return exp_write(1,expander_output);
}
static esp_err_t touch_read(uint16_t reg,uint8_t* value,size_t length) {
    uint8_t address[]={static_cast<uint8_t>(reg>>8),static_cast<uint8_t>(reg)};
    return i2c_master_transmit_receive(touch,address,2,value,length,50);
}
static esp_err_t touch_clear() {
    uint8_t bytes[]={0x81,0x4E,0}; return i2c_master_transmit(touch,bytes,3,50);
}
void* board_panel() { return panel; }
uint16_t* board_snapshot() {
    if(!panel)return nullptr;
    void* framebuffer=nullptr;
    if(esp_lcd_rgb_panel_get_frame_buffer(panel,1,&framebuffer)!=ESP_OK)return nullptr;
    auto* copy=static_cast<uint16_t*>(heap_caps_malloc(WIDTH*HEIGHT*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(copy)std::memcpy(copy,framebuffer,WIDTH*HEIGHT*2);
    return copy;
}
void board_backlight(unsigned percent) {
    percent=std::min(percent,100U);
    ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0,4095U*percent/100U);
    ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0);
}
bool board_init() {
    gpio_set_direction(GPIO_NUM_6,GPIO_MODE_OUTPUT); gpio_set_level(GPIO_NUM_6,0);
    i2c_master_bus_config_t bus {};
    bus.i2c_port=I2C_NUM_0; bus.sda_io_num=GPIO_NUM_15; bus.scl_io_num=GPIO_NUM_7;
    bus.clk_source=I2C_CLK_SRC_DEFAULT; bus.glitch_ignore_cnt=7; bus.flags.enable_internal_pullup=true;
    if(i2c_new_master_bus(&bus,&i2c_bus)!=ESP_OK) return false;
    i2c_device_config_t dev {}; dev.dev_addr_length=I2C_ADDR_BIT_LEN_7; dev.device_address=0x20; dev.scl_speed_hz=400000;
    if(i2c_master_bus_add_device(i2c_bus,&dev,&expander)!=ESP_OK) return false;
    // P0 reset, P1 touch reset, P2 LCD CS, P3 SD D3 high; P7 buzzer low.
    // IMU/RTC interrupt pins P4..P6 are inputs, not driven by the gateway.
    if(exp_write(1,expander_output)!=ESP_OK||exp_write(2,0)!=ESP_OK||exp_write(3,0x70)!=ESP_OK) return false;
    exp_set(0,false); vTaskDelay(pdMS_TO_TICKS(10)); exp_set(0,true); vTaskDelay(pdMS_TO_TICKS(110));
    spi_bus_config_t spi {}; spi.mosi_io_num=1; spi.miso_io_num=-1; spi.sclk_io_num=2;
    spi.quadwp_io_num=-1; spi.quadhd_io_num=-1; spi.max_transfer_sz=64;
    if(spi_bus_initialize(SPI2_HOST,&spi,SPI_DMA_CH_AUTO)!=ESP_OK) return false;
    spi_device_interface_config_t serial {}; serial.command_bits=1; serial.address_bits=8;
    serial.clock_speed_hz=4000000; serial.spics_io_num=-1; serial.queue_size=1;
    spi_device_handle_t lcd_serial=nullptr;
    if(spi_bus_add_device(SPI2_HOST,&serial,&lcd_serial)!=ESP_OK) { spi_bus_free(SPI2_HOST); return false; }
    exp_set(2,false);
    esp_err_t result=ESP_OK;
    for(const auto& command:panel_commands) {
        spi_transaction_t transaction {}; transaction.cmd=0; transaction.addr=command.command;
        if((result=spi_device_transmit(lcd_serial,&transaction))!=ESP_OK) break;
        for(unsigned i=0;i<command.length;++i) { transaction={}; transaction.cmd=1; transaction.addr=command.data[i]; if((result=spi_device_transmit(lcd_serial,&transaction))!=ESP_OK) break; }
        if(result!=ESP_OK) break;
        if(command.delay_ms) vTaskDelay(pdMS_TO_TICKS(command.delay_ms));
    }
    exp_set(2,true);
    // GPIO1/2 now belong to SDMMC. Do not retain a SPI device on these pins.
    spi_bus_remove_device(lcd_serial); spi_bus_free(SPI2_HOST);
    if(result!=ESP_OK) return false;
    esp_lcd_rgb_panel_config_t rgb {};
    rgb.clk_src=LCD_CLK_SRC_DEFAULT; rgb.data_width=16; rgb.bits_per_pixel=16;
    rgb.num_fbs=1; rgb.bounce_buffer_size_px=WIDTH*10; rgb.dma_burst_size=64;
    rgb.hsync_gpio_num=38; rgb.vsync_gpio_num=39; rgb.de_gpio_num=40; rgb.pclk_gpio_num=41; rgb.disp_gpio_num=-1;
    const int data[]={5,45,48,47,21,14,13,12,11,10,9,46,3,8,18,17};
    std::copy(std::begin(data),std::end(data),rgb.data_gpio_nums);
    rgb.timings.pclk_hz=30000000; rgb.timings.h_res=WIDTH; rgb.timings.v_res=HEIGHT;
    rgb.timings.hsync_back_porch=10; rgb.timings.hsync_front_porch=50; rgb.timings.hsync_pulse_width=8;
    rgb.timings.vsync_back_porch=18; rgb.timings.vsync_front_porch=8; rgb.timings.vsync_pulse_width=2;
    rgb.timings.flags.pclk_active_neg=false; rgb.flags.fb_in_psram=true;
    if(esp_lcd_new_rgb_panel(&rgb,&panel)!=ESP_OK||esp_lcd_panel_reset(panel)!=ESP_OK||esp_lcd_panel_init(panel)!=ESP_OK) return false;
    state.display_ready=true;
    ledc_timer_config_t timer {}; timer.speed_mode=LEDC_LOW_SPEED_MODE; timer.duty_resolution=LEDC_TIMER_12_BIT;
    timer.timer_num=LEDC_TIMER_0; timer.freq_hz=4000; timer.clk_cfg=LEDC_AUTO_CLK;
    ledc_timer_config(&timer);
    ledc_channel_config_t channel {}; channel.gpio_num=6; channel.speed_mode=LEDC_LOW_SPEED_MODE;
    channel.channel=LEDC_CHANNEL_0; channel.timer_sel=LEDC_TIMER_0;
    ledc_channel_config(&channel); board_backlight(65);
    // GT911 address selection/reset follows the board's factory sequence.
    gpio_set_direction(GPIO_NUM_16,GPIO_MODE_OUTPUT); gpio_set_level(GPIO_NUM_16,0);
    vTaskDelay(pdMS_TO_TICKS(150)); exp_set(1,false); vTaskDelay(pdMS_TO_TICKS(150));
    exp_set(1,true); vTaskDelay(pdMS_TO_TICKS(50)); gpio_set_level(GPIO_NUM_16,1);
    gpio_set_direction(GPIO_NUM_16,GPIO_MODE_INPUT); vTaskDelay(pdMS_TO_TICKS(100));
    // GT911 can use either address, depending on INT at reset release.
    // Probe both because panel revisions need not route INT identically.
    dev.scl_speed_hz=100000;
    for(const uint8_t address:{0x5D,0x14}) {
        if(i2c_master_probe(i2c_bus,address,100)!=ESP_OK) continue;
        dev.device_address=address;
        if(i2c_master_bus_add_device(i2c_bus,&dev,&touch)!=ESP_OK) continue;
        uint8_t product[5] {}, bounds[4] {};
        if(touch_read(0x8140,product,4)==ESP_OK) {
            state.touch_ready=true;
            if(touch_read(0x8048,bounds,4)==ESP_OK) {
                const unsigned w=bounds[0]|(bounds[1]<<8),h=bounds[2]|(bounds[3]<<8);
                if(w>=100&&w<=2000&&h>=100&&h<=2000) { touch_width=w; touch_height=h; }
            }
            ESP_LOGI("lcd28b","GT911 %s at 0x%02x, touch bounds %ux%u",product,address,touch_width,touch_height);
            touch_clear();
            break;
        }
        i2c_master_bus_rm_device(touch); touch=nullptr;
    }
    ESP_LOGI("lcd28b","RGB display ready 480x640; touch %s; SD serial pins released",state.touch_ready?"ready":"not found");
    return true;
}
bool board_touch(int& x,int& y) {
    if(!state.touch_ready) return false;
    uint8_t status=0;
    if(touch_read(0x814E,&status,1)!=ESP_OK) { pressed=false; return false; }
    if(status&0x80) {
        const bool was_pressed=pressed;
        pressed=(status&0x0F)>0;
        if(pressed) {
            uint8_t point[8] {};
            if(touch_read(0x814F,point,sizeof(point))==ESP_OK) {
                const unsigned raw_x=point[1]|(point[2]<<8),raw_y=point[3]|(point[4]<<8);
                last_x=std::min(WIDTH-1,static_cast<int>(raw_x*WIDTH/touch_width));
                last_y=std::min(HEIGHT-1,static_cast<int>(raw_y*HEIGHT/touch_height));
            } else pressed=false;
        }
        last_touch_ms=now_ms(); touch_clear();
        if(pressed&&!was_pressed) { std::unique_lock<std::mutex> lock(state.mutex,std::try_to_lock); if(lock.owns_lock()) ++state.touch_events; }
    }
    if(now_ms()-last_touch_ms>1000) pressed=false;
    x=last_x; y=last_y; return pressed;
}
}
