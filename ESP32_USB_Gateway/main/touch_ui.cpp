#include "gateway.hpp"
#include <algorithm>
#include <ctime>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <memory>
#include "lvgl.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_system.h"
#include "esp_timer.h"

namespace rig {
static lv_obj_t *tabs,*live_page,*cal_page,*runs_page,*device_page,*keyboard;
static lv_obj_t *thrust_label,*unit_label,*link_label,*record_button,*record_label,*record_info,*live_note;
static lv_obj_t *values[5],*cal_notice,*cal_live,*command_result,*known_load,*config_inputs[4];
static lv_obj_t *command_buttons[6],*sd_label,*device_label,*mode_button,*mode_label,*baud_select,*run_select,*run_detail;
static lv_disp_draw_buf_t draw_buffer;
static lv_disp_drv_t display_driver;
static lv_indev_drv_t touch_driver;
static std::function<void()> confirm_callback;
static std::string last_config;
static std::vector<std::string> run_ids;
static std::vector<std::string> run_descriptions;
static bool editing_config=false;
static constexpr uint32_t BG=0x11151B,PANEL=0x202833,ORANGE=0xFF7847,TEXT=0xEDF0F5,MUTED=0x9BA8B8;
static QueueHandle_t result_queue;
struct UiResult { std::string title,message; std::vector<int32_t> points; };

static void text(lv_obj_t* label,const std::string& value) { if(std::strcmp(lv_label_get_text(label),value.c_str())) lv_label_set_text(label,value.c_str()); }
static lv_obj_t* label(lv_obj_t* parent,const char* caption,int x,int y,int width,const lv_font_t* font=&lv_font_montserrat_16,uint32_t color=TEXT) {
    auto* obj=lv_label_create(parent); lv_label_set_text(obj,caption); lv_obj_set_pos(obj,x,y); lv_obj_set_width(obj,width);
    lv_obj_set_style_text_font(obj,font,0); lv_obj_set_style_text_color(obj,lv_color_hex(color),0); return obj;
}
static lv_obj_t* button(lv_obj_t* parent,const char* caption,int x,int y,int width,int height,lv_event_cb_t callback,bool primary=false) {
    auto* obj=lv_btn_create(parent); lv_obj_set_pos(obj,x,y); lv_obj_set_size(obj,width,height);
    lv_obj_set_style_bg_color(obj,lv_color_hex(primary?ORANGE:0x303C4B),0); lv_obj_set_style_radius(obj,8,0);
    lv_obj_set_style_shadow_width(obj,0,0);
    auto* title=lv_label_create(obj); lv_label_set_text(title,caption); lv_obj_set_style_text_font(title,&lv_font_montserrat_16,0);
    lv_obj_set_style_text_color(title,lv_color_hex(primary?BG:TEXT),0); lv_obj_center(title);
    if(callback) lv_obj_add_event_cb(obj,callback,LV_EVENT_CLICKED,nullptr);
    return obj;
}
static void hide_keyboard() { lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN); lv_keyboard_set_textarea(keyboard,nullptr); }
static void focus_input(lv_event_t* event) {
    auto* input=lv_event_get_target(event);
    lv_keyboard_set_mode(keyboard,lv_event_get_user_data(event)?LV_KEYBOARD_MODE_NUMBER:LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(keyboard,input); lv_obj_clear_flag(keyboard,LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(keyboard);
    lv_obj_scroll_to_view(input,LV_ANIM_ON);
}
static lv_obj_t* input(lv_obj_t* parent,const char* caption,const char* initial,int x,int y,int width,bool numeric=true) {
    label(parent,caption,x,y,width,&lv_font_montserrat_16,MUTED);
    auto* obj=lv_textarea_create(parent); lv_obj_set_pos(obj,x,y+25); lv_obj_set_size(obj,width,46);
    lv_textarea_set_one_line(obj,true); lv_textarea_set_text(obj,initial); lv_obj_set_style_text_font(obj,&lv_font_montserrat_20,0);
    if(numeric) lv_textarea_set_accepted_chars(obj,"0123456789.-");
    lv_obj_add_event_cb(obj,focus_input,LV_EVENT_FOCUSED,numeric?reinterpret_cast<void*>(1):nullptr);
    lv_obj_add_event_cb(obj,focus_input,LV_EVENT_CLICKED,numeric?reinterpret_cast<void*>(1):nullptr);
    return obj;
}
static void message(const std::string& title,const std::string& caption) {
    hide_keyboard(); static const char* buttons[]={"OK",""};
    auto* box=lv_msgbox_create(nullptr,title.c_str(),caption.c_str(),buttons,true);
    lv_obj_set_width(box,430); lv_obj_center(box);
    lv_obj_add_event_cb(box,[](lv_event_t* event){lv_msgbox_close(lv_event_get_current_target(event));},LV_EVENT_VALUE_CHANGED,nullptr);
}
static void confirm(const char* title,const char* caption,std::function<void()> callback) {
    hide_keyboard(); confirm_callback=std::move(callback); static const char* buttons[]={"Cancel","Continue",""};
    auto* box=lv_msgbox_create(nullptr,title,caption,buttons,true); lv_obj_set_width(box,430); lv_obj_center(box);
    lv_obj_add_event_cb(box,[](lv_event_t* event){
        auto* box=lv_event_get_current_target(event); const auto choice=lv_msgbox_get_active_btn(box);
        lv_msgbox_close(box); if(choice==1&&confirm_callback) { auto action=std::move(confirm_callback); action(); }
    },LV_EVENT_VALUE_CHANGED,nullptr);
}
static void async_job(std::function<UiResult*()> work) {
    auto* task=new std::function<UiResult*()>(std::move(work));
    if(xTaskCreate([](void* arg){std::unique_ptr<std::function<UiResult*()>> fn(static_cast<std::function<UiResult*()>*>(arg)); UiResult* result=(*fn)(); if(xQueueSend(result_queue,&result,pdMS_TO_TICKS(200))!=pdTRUE) delete result; fn.reset(); vTaskDelete(nullptr);},"sd_ui_job",6144,task,3,nullptr)!=pdPASS) {delete task;message("Busy","Could not start the storage operation.");}
}
static void send_command(const std::string& command,cJSON* args=nullptr) {
    if(!args) args=cJSON_CreateObject();
    uint32_t request=0; std::string error;
    const bool ok=submit_command(command,args,request,error); cJSON_Delete(args);
    if(!ok) message("Nano command",error); else text(command_result,"Waiting for Nano acknowledgement...");
}
static void refresh_runs() {
    std::lock_guard<std::mutex> lock(state.mutex);
    cJSON* runs=list_runs_locked(); cJSON* run=nullptr; std::string options;
    run_ids.clear(); run_descriptions.clear();
    cJSON_ArrayForEach(run,runs) {
        const auto id=string_field(run,"id"); std::string name=string_field(run,"name",id);
        std::replace(name.begin(),name.end(),'\n',' '); std::replace(name.begin(),name.end(),'\r',' ');
        if(!options.empty()) options+='\n';
        options+=name;
        run_ids.push_back(id);
        run_descriptions.push_back(name+"\n\n"+string_field(run,"status")+" | "+std::to_string(static_cast<unsigned>(number_field(run,"samples",0)))+" samples\n"+string_field(run,"note")+"\n\nCSV and metadata are in /thrust/ on the card. Use the phone dashboard to download them.");
    }
    cJSON_Delete(runs);
    lv_dropdown_set_options(run_select,options.empty()?"No saved runs":options.c_str());
    text(run_detail,run_descriptions.empty()?"Insert a FAT32 card, connect the Nano, and record a test to build your library.":run_descriptions[0]);
}
static void start_record_dialog() {
    auto* overlay=lv_obj_create(lv_scr_act()); lv_obj_set_size(overlay,480,640); lv_obj_set_pos(overlay,0,0);
    lv_obj_set_style_bg_color(overlay,lv_color_hex(BG),0); lv_obj_set_style_pad_all(overlay,16,0); lv_obj_clear_flag(overlay,LV_OBJ_FLAG_SCROLLABLE);
    label(overlay,"NEW TEST RUN",0,0,440,&lv_font_montserrat_24);
    auto* name=input(overlay,"Run name","Bench run",0,48,440,false);
    lv_textarea_set_max_length(name,80);
    auto* note=input(overlay,"Notes (optional)","",0,138,440,false); lv_textarea_set_max_length(note,500);
    auto* cancel=button(overlay,"Cancel",0,245,210,48,[](lv_event_t* e){hide_keyboard();lv_obj_del(lv_obj_get_parent(lv_event_get_target(e)));});
    (void)cancel;
    auto* start=button(overlay,"Start recording",230,245,210,48,nullptr,true);
    struct Fields {lv_obj_t *overlay,*name,*note;}; auto* fields=new Fields{overlay,name,note};
    lv_obj_add_event_cb(overlay,[](lv_event_t* e){delete static_cast<Fields*>(lv_event_get_user_data(e));},LV_EVENT_DELETE,fields);
    lv_obj_add_event_cb(start,[](lv_event_t* e){auto* f=static_cast<Fields*>(lv_event_get_user_data(e)); std::string error; bool ok;
        {std::lock_guard<std::mutex> lock(state.mutex); const auto epoch=std::time(nullptr); ok=start_recording_locked(lv_textarea_get_text(f->name),lv_textarea_get_text(f->note),epoch>1700000000?epoch*1000.0:0,error);}
        if(ok){hide_keyboard();lv_obj_del(f->overlay);}else message("Recording",error);
    },LV_EVENT_CLICKED,fields);
}
static void restart_with(Settings settings) {
    std::lock_guard<std::mutex> lock(state.mutex);
    if(state.recording_file||state.pending_request||state.active_downloads) {message("Finish the test","Stop recording, finish downloads, and wait for Nano commands before restarting.");return;}
    if(!save_settings(settings)){message("Settings","Could not save settings.");return;}
    xTaskCreate([](void*){vTaskDelay(pdMS_TO_TICKS(1500));esp_restart();},"restart",2048,nullptr,2,nullptr);
    message("Restarting",settings.usb_host?"Switching USB to the Nano. Use the correct powered host arrangement when connecting the Nano.":"Switching USB to PC service mode. Connect to the computer for serial diagnostics.");
}
static void build_live() {
    label(live_page,"HACKSMITH / THRUST TESTER",0,0,448,&lv_font_montserrat_16,MUTED);
    link_label=label(live_page,"Waiting for Nano",0,26,448,&lv_font_montserrat_16,0xFFBD8C);
    auto* hero=lv_obj_create(live_page); lv_obj_set_pos(hero,0,58); lv_obj_set_size(hero,448,128);
    lv_obj_set_style_bg_color(hero,lv_color_hex(0x35271F),0); lv_obj_set_style_border_color(hero,lv_color_hex(0x76503B),0);
    lv_obj_clear_flag(hero,LV_OBJ_FLAG_SCROLLABLE); label(hero,"THRUST",0,0,400,&lv_font_montserrat_16,MUTED);
    thrust_label=label(hero,"--",0,32,320,&lv_font_montserrat_48,ORANGE); unit_label=label(hero,"kgf",335,62,70,&lv_font_montserrat_20,MUTED);
    const char* names[]={"RPM","VOLTAGE / V","CURRENT / A","POWER / W","THROTTLE / %"};
    for(int i=0;i<4;++i) {
        auto* tile=lv_obj_create(live_page);lv_obj_set_pos(tile,(i%2)*230,198+(i/2)*79);lv_obj_set_size(tile,218,70);
        lv_obj_set_style_bg_color(tile,lv_color_hex(PANEL),0);lv_obj_set_style_border_width(tile,0,0);lv_obj_clear_flag(tile,LV_OBJ_FLAG_SCROLLABLE);
        label(tile,names[i],0,-5,200,&lv_font_montserrat_16,MUTED);values[i]=label(tile,"--",0,20,200,&lv_font_montserrat_24);
    }
    label(live_page,names[4],0,365,240,&lv_font_montserrat_16,MUTED); values[4]=label(live_page,"--",270,359,170,&lv_font_montserrat_24);
    record_button=button(live_page,"Start recording",0,400,448,55,[](lv_event_t*){
        bool recording;{std::lock_guard<std::mutex> lock(state.mutex);recording=state.recording_file!=nullptr;}
        if(recording)confirm("Finish recording?","The CSV and metadata will stay on the microSD card.",[]{std::lock_guard<std::mutex> lock(state.mutex);stop_recording_locked();});
        else start_record_dialog();
    },true); record_label=lv_obj_get_child(record_button,0);
    record_info=label(live_page,"No active recording",0,466,448,&lv_font_montserrat_16,MUTED);
    live_note=label(live_page,"",0,495,448,&lv_font_montserrat_16,MUTED);
}
static void build_calibration() {
    cal_notice=label(cal_page,"Connect the Nano to use calibration.",0,0,448,&lv_font_montserrat_16,0xFFBD8C);
    command_buttons[0]=button(cal_page,"Tare / zero",0,67,210,48,[](lv_event_t*){confirm("Tare load cell?","Remove external test force from the load cell before continuing.",[]{send_command("tare");});},true);
    cal_live=label(cal_page,"-- kgf",235,76,210,&lv_font_montserrat_24);
    known_load=input(cal_page,"Known load (selected unit)","",0,139,220);
    command_buttons[1]=button(cal_page,"Calibrate",238,164,210,46,[](lv_event_t*){
        const double value=std::strtod(lv_textarea_get_text(known_load),nullptr);
        if(!std::isfinite(value)||value<=0){message("Known load","Enter a positive known load first.");return;}
        confirm("Apply known-load calibration?","Tare first, then apply the known load and wait for a steady reading. Save to EEPROM separately.",[value]{auto* args=cJSON_CreateObject();cJSON_AddNumberToObject(args,"known_load",value);send_command("calibrate",args);});
    });
    command_buttons[2]=button(cal_page,"Read Nano settings",0,238,448,46,[](lv_event_t*){editing_config=false;last_config.clear();send_command("get_config");});
    const char* labels[]={"Load-cell factor","Transitions / revolution","Shunt resistance / ohm","Voltage-divider ratio"};
    for(int i=0;i<4;++i)config_inputs[i]=input(cal_page,labels[i],"",0,310+i*90,448);
    command_buttons[3]=button(cal_page,"Apply settings",0,684,448,48,[](lv_event_t*){
        const char* keys[]={"calibration_factor","poles","shunt_ohms","divider_ratio"};
        auto* args=cJSON_CreateObject();for(int i=0;i<4;++i)cJSON_AddNumberToObject(args,keys[i],std::strtod(lv_textarea_get_text(config_inputs[i]),nullptr));
        const auto raw=json_string(args);cJSON_Delete(args);
        confirm("Apply measurement settings?","These settings change Nano readings immediately. Verify the result before saving.",[raw]{editing_config=false;send_command("set_config",cJSON_Parse(raw.c_str()));});
    });
    command_buttons[4]=button(cal_page,"Save to Nano EEPROM",0,746,448,48,[](lv_event_t*){confirm("Save Nano settings?","The current calibration becomes the calibration used after the Nano restarts.",[]{send_command("save_config");});},true);
    command_result=label(cal_page,"No command sent.",0,815,448,&lv_font_montserrat_16,MUTED);
    label(cal_page,"",0,880,1);
    for(auto* obj:config_inputs)lv_obj_add_event_cb(obj,[](lv_event_t*){editing_config=true;},LV_EVENT_VALUE_CHANGED,nullptr);
}
static void build_runs() {
    label(runs_page,"TEST LIBRARY / microSD",0,0,448,&lv_font_montserrat_24);
    button(runs_page,"Refresh library",0,48,448,46,[](lv_event_t*){refresh_runs();});
    run_select=lv_dropdown_create(runs_page);lv_obj_set_pos(run_select,0,113);lv_obj_set_size(run_select,448,48);
    lv_obj_add_event_cb(run_select,[](lv_event_t*){const size_t index=lv_dropdown_get_selected(run_select);if(index<run_descriptions.size())text(run_detail,run_descriptions[index]);},LV_EVENT_VALUE_CHANGED,nullptr);
    run_detail=label(runs_page,"No saved runs",0,180,448,&lv_font_montserrat_20);
    label(runs_page,"Recordings continue without a phone. Stop the recording before removing the card. Use the phone dashboard for plots and CSV downloads.",0,390,448,&lv_font_montserrat_16,MUTED);
    refresh_runs();
}
static void build_device() {
    label(device_page,"DEVICE & CONNECTION",0,0,448,&lv_font_montserrat_24);
    device_label=label(device_page,"",0,44,448,&lv_font_montserrat_16);
    sd_label=label(device_page,"",0,157,448,&lv_font_montserrat_16,MUTED);
    button(device_page,"Remount microSD",0,229,218,46,[](lv_event_t*){async_job([]{auto* result=new UiResult;result->title="microSD";std::string error;result->message=remount_storage(error)?"Mounted; read/write check passed.":error;return result;});});
    button(device_page,"Test card",230,229,218,46,[](lv_event_t*){async_job([]{auto* result=new UiResult;result->title="microSD check";std::string error;result->message=storage_selftest(error)?"Read/write verification passed.":error;return result;});});
    mode_label=label(device_page,"",0,300,448,&lv_font_montserrat_16,MUTED);
    mode_button=button(device_page,"Use Nano USB",0,340,448,48,[](lv_event_t*){
        Settings settings;{std::lock_guard<std::mutex> lock(state.mutex);settings=state.settings;}settings.usb_host=!settings.usb_host;
        confirm("Change USB connection?",settings.usb_host?"After restart the USB port will talk to the Nano. Use a powered USB host connection; a classic Nano does not back-power its USB socket.":"After restart the USB port will be available to the computer for diagnostics and flashing.",[settings]{restart_with(settings);});
    },true);
    label(device_page,"Nano serial speed",0,414,448,&lv_font_montserrat_16,MUTED);
    baud_select=lv_dropdown_create(device_page);lv_obj_set_pos(baud_select,0,443);lv_obj_set_size(baud_select,218,46);
    lv_dropdown_set_options(baud_select,"9600\n19200\n38400\n57600\n115200\n230400");
    const unsigned rates[]={9600,19200,38400,57600,115200,230400};for(unsigned i=0;i<6;++i)if(state.settings.baud==rates[i])lv_dropdown_set_selected(baud_select,i);
    button(device_page,"Save & restart",230,443,218,46,[](lv_event_t*){const unsigned rates[]={9600,19200,38400,57600,115200,230400};Settings settings;{std::lock_guard<std::mutex> lock(state.mutex);settings=state.settings;}settings.baud=rates[lv_dropdown_get_selected(baud_select)];confirm("Change serial speed?","The selected speed must match the Nano. Restart to apply it.",[settings]{restart_with(settings);});});
    label(device_page,"Screen brightness",0,514,448,&lv_font_montserrat_16,MUTED);
    auto* slider=lv_slider_create(device_page);lv_obj_set_pos(slider,10,557);lv_obj_set_size(slider,425,18);lv_slider_set_range(slider,15,100);lv_slider_set_value(slider,65,LV_ANIM_OFF);
    lv_obj_add_event_cb(slider,[](lv_event_t* e){board_backlight(lv_slider_get_value(lv_event_get_target(e)));},LV_EVENT_VALUE_CHANGED,nullptr);
    label(device_page,"Both interfaces use the same Nano configuration. Calibration remains unavailable until the Nano firmware supports the USB command protocol.",0,608,448,&lv_font_montserrat_16,MUTED);
}
static std::string number(double value,int digits=2) { if(!std::isfinite(value))return "--";char text[40];std::snprintf(text,sizeof(text),"%.*f",digits,value);return text; }
static void update_ui() {
    std::unique_lock<std::mutex> lock(state.mutex,std::try_to_lock);if(!lock.owns_lock())return;
    const bool live=state.usb_connected&&!state.history.empty()&&now_ms()-state.history.back().gateway_ms<STALE_MS;
    const Sample sample=state.history.empty()?Sample{}:state.history.back();
    text(thrust_label,number(sample.thrust));text(unit_label,state.settings.unit);
    text(values[0],number(sample.rpm,0));text(values[1],number(sample.voltage));text(values[2],number(sample.current));text(values[3],number(sample.power(),0));text(values[4],number(sample.throttle,1));
    text(link_label,live?"LIVE / USB measurements":!state.settings.usb_host?"PC SERVICE / select Nano USB in Device":state.usb_connected?"USB connected / waiting for data":"NANO NOT CONNECTED");
    lv_obj_set_style_text_color(thrust_label,lv_color_hex(live?ORANGE:MUTED),0);
    const bool recording=state.recording_file!=nullptr;
    text(record_label,recording?"Stop recording":"Start recording");
    if(recording||(live&&state.storage_ready))lv_obj_clear_state(record_button,LV_STATE_DISABLED);else lv_obj_add_state(record_button,LV_STATE_DISABLED);
    text(record_info,recording?"REC  "+std::to_string((now_ms()-state.recording_started_ms)/1000)+" s / "+std::to_string(state.recorded_samples)+" samples":"No active recording");
    text(live_note,!state.storage_ready?"No writable microSD. Insert a FAT32 card and remount.":state.last_error.empty()?"Logging target: microSD /thrust/":state.last_error);
    const char* capabilities[]={"tare","calibrate","get_config","set_config","save_config"};
    for(int i=0;i<5;++i) {if(has_capability_locked(capabilities[i])&&!recording&&!state.pending_request)lv_obj_clear_state(command_buttons[i],LV_STATE_DISABLED);else lv_obj_add_state(command_buttons[i],LV_STATE_DISABLED);}
    text(cal_notice,state.mode=="protocol_v1"?recording?"Stop recording before changing calibration.":"Nano protocol ready. Save EEPROM explicitly.":"Nano firmware update required for USB calibration.");
    text(cal_live,number(sample.thrust)+" "+state.settings.unit);text(command_result,state.command_message.empty()?"No command sent.":state.command_message);
    if(!editing_config&&state.remote_config!=last_config) {
        cJSON* config=cJSON_Parse(state.remote_config.c_str());const char* keys[]={"calibration_factor","poles","shunt_ohms","divider_ratio"};
        for(int i=0;i<4;++i){const double value=number_field(config,keys[i]);if(std::isfinite(value)){char raw[40];std::snprintf(raw,sizeof(raw),"%.10g",value);lv_textarea_set_text(config_inputs[i],raw);}}
        cJSON_Delete(config);last_config=state.remote_config;editing_config=false;
    }
    text(device_label,"Wi-Fi: "+state.settings.ssid+"\nPassword: "+state.settings.password+"\nOpen: http://"+state.settings.ip+"\nTouch taps: "+std::to_string(state.touch_events));
    text(sd_label,state.storage_status+(state.storage_ready?"\n"+std::to_string(state.storage_used/(1024*1024))+" / "+std::to_string(state.storage_total/(1024*1024))+" MB used":""));
    text(mode_label,state.settings.usb_host?"USB target: Nano (host mode)":"USB target: Computer (service mode)");
    text(lv_obj_get_child(mode_button,0),state.settings.usb_host?"Use PC service USB":"Use Nano USB");
}
static void flush(lv_disp_drv_t* driver,const lv_area_t* area,lv_color_t* pixels) {
    esp_lcd_panel_draw_bitmap(static_cast<esp_lcd_panel_handle_t>(driver->user_data),area->x1,area->y1,area->x2+1,area->y2+1,pixels);lv_disp_flush_ready(driver);
}
static void read_touch(lv_indev_drv_t*,lv_indev_data_t* data) {int x=0,y=0;data->state=board_touch(x,y)?LV_INDEV_STATE_PR:LV_INDEV_STATE_REL;data->point.x=x;data->point.y=y;}
static void gui_task(void*) {
    lv_init();
    constexpr size_t pixels=480*24;
    auto* buffer1=heap_caps_malloc(pixels*sizeof(lv_color_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    auto* buffer2=heap_caps_malloc(pixels*sizeof(lv_color_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    assert(buffer1&&buffer2);
    lv_disp_draw_buf_init(&draw_buffer,buffer1,buffer2,pixels);
    lv_disp_drv_init(&display_driver);display_driver.hor_res=480;display_driver.ver_res=640;display_driver.flush_cb=flush;display_driver.draw_buf=&draw_buffer;display_driver.user_data=board_panel();
    auto* display=lv_disp_drv_register(&display_driver);
    lv_theme_default_init(display,lv_color_hex(ORANGE),lv_color_hex(0x8BBAFF),true,&lv_font_montserrat_16);
    lv_obj_set_style_bg_color(lv_scr_act(),lv_color_hex(BG),0);
    lv_indev_drv_init(&touch_driver);touch_driver.type=LV_INDEV_TYPE_POINTER;touch_driver.read_cb=read_touch;lv_indev_drv_register(&touch_driver);
    esp_timer_handle_t tick;esp_timer_create_args_t timer {};timer.callback=[](void*){lv_tick_inc(2);};timer.name="lv_tick";ESP_ERROR_CHECK(esp_timer_create(&timer,&tick));ESP_ERROR_CHECK(esp_timer_start_periodic(tick,2000));
    tabs=lv_tabview_create(lv_scr_act(),LV_DIR_BOTTOM,56);
    live_page=lv_tabview_add_tab(tabs,"Live");cal_page=lv_tabview_add_tab(tabs,"Calibrate");runs_page=lv_tabview_add_tab(tabs,"Runs");device_page=lv_tabview_add_tab(tabs,"Device");
    for(auto* page:{live_page,cal_page,runs_page,device_page}){lv_obj_set_style_pad_all(page,16,0);lv_obj_set_style_bg_color(page,lv_color_hex(BG),0);}
    build_live();build_calibration();build_runs();build_device();
    keyboard=lv_keyboard_create(lv_scr_act());lv_obj_set_size(keyboard,480,220);lv_obj_align(keyboard,LV_ALIGN_BOTTOM_MID,0,0);lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(keyboard,[](lv_event_t*){hide_keyboard();},LV_EVENT_READY,nullptr);lv_obj_add_event_cb(keyboard,[](lv_event_t*){hide_keyboard();},LV_EVENT_CANCEL,nullptr);
    int64_t last_update=0;
    while(true){
        if(now_ms()-last_update>=200){update_ui();last_update=now_ms();}
        UiResult* result=nullptr;if(xQueueReceive(result_queue,&result,0)==pdTRUE){message(result->title,result->message);delete result;}
        lv_timer_handler();vTaskDelay(pdMS_TO_TICKS(10));
    }
}
void start_touch_ui() {
    if(!state.display_ready)return;
    result_queue=xQueueCreate(4,sizeof(UiResult*));assert(result_queue);
    xTaskCreatePinnedToCore(gui_task,"touch_gui",12288,nullptr,4,nullptr,1);
}
}
