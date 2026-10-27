// Measurement Nano replacement. Original sketches remain archived unchanged.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <SPI.h>
#include <SD.h>
#include <util/atomic.h>
#include "config.hpp"

using rig_nano::Config;
static Config config;
static bool persisted=false,tared=false,sd_ok=false;
static int32_t zero_offset=0,last_raw=0;
static uint32_t raw_ms=0,adc_ms=0,sample_ms=0,sample_us=0,sequence=0;
static int16_t adc_raw[2]={0,0};
static uint8_t adc_channel=0;
static bool adc_waiting=false,adc_healthy=false;
static uint32_t adc_started=0,adc_retry=0;
static File sd_log;
static char sd_name[13]="";
static uint8_t flush_count=0;
static volatile uint32_t edges=0,pwm_rise=0,pwm_seen=0;
static volatile uint16_t pwm_width=0;
static volatile uint8_t previous_port=0;
static char rx_line[224];
static uint16_t rx_length=0;
static bool rx_ready=false,rx_overflow=false;
// Input is deserialized in-place, so JSON strings need no extra allocation.
static StaticJsonDocument<JSON_OBJECT_SIZE(5)+JSON_OBJECT_SIZE(4)> request;
static uint32_t last_request=0;
static bool last_ok=false;
static const __FlashStringHelper* last_message=nullptr;
enum class Phase:uint8_t {None,BootTare,Tare,Calibrate};
static Phase phase=Phase::BootTare;
static uint32_t phase_request=0,phase_started=0;
static uint8_t phase_count=0;
static int32_t phase_sum=0,phase_min=0,phase_max=0;
static float known_load=0;
struct SavedConfig {uint32_t magic;uint8_t version,reserved;Config value;uint16_t crc;};
static_assert(sizeof(SavedConfig)==22,"EEPROM record requires AVR byte alignment");
static constexpr uint32_t CONFIG_MAGIC=0x31544752;
static constexpr int EEPROM_ADDRESS=32;

static void pump_rx(){
    while(Serial.available()&&!rx_ready){
        const char c=static_cast<char>(Serial.read());
        if(c=='\n'){rx_line[rx_length]=0;rx_ready=true;}
        else if(c!='\r'&&!rx_overflow){if(rx_length<sizeof(rx_line)-1)rx_line[rx_length++]=c;else rx_overflow=true;}
    }
}
// Drain RX while transmitting long JSON lines; command execution waits for LF.
static void out(const __FlashStringHelper* value){
    const char* p=reinterpret_cast<const char*>(value);
    char c;while((c=pgm_read_byte(p++))){Serial.write(c);pump_rx();}
}
template<class T>static void out(T value){Serial.print(value);pump_rx();}
static void number(float value,uint8_t digits){if(isfinite(value))Serial.print(value,digits);else out(F("null"));pump_rx();}
static void end_line(){Serial.println();pump_rx();}
static void event(const __FlashStringHelper* message){out(F("{\"type\":\"event\",\"protocol\":1,\"message\":\""));out(message);out(F("\"}"));end_line();}
static void ack(uint32_t id,bool ok,const __FlashStringHelper* message,bool remember=true){
    if(remember){last_request=id;last_ok=ok;last_message=message;}
    out(F("{\"type\":\"ack\",\"protocol\":1,\"request_id\":"));out(id);out(F(",\"ok\":"));out(ok?F("true"):F("false"));
    out(F(",\"message\":\""));out(message);out(F("\"}"));end_line();
}
static void send_config(){
    out(F("{\"type\":\"config\",\"protocol\":1,\"calibration_factor\":"));number(config.factor,5);
    out(F(",\"poles\":"));out(config.poles);out(F(",\"shunt_ohms\":"));number(config.shunt,9);
    out(F(",\"divider_ratio\":"));number(config.divider,6);
    out(F(",\"thrust_unit\":\"kgf\",\"persisted\":"));out(persisted?F("true"):F("false"));
    out(F(",\"tare_ready\":"));out(tared?F("true"):F("false"));out(F(",\"sd_ok\":"));out(sd_ok?F("true"):F("false"));
    out(F(",\"sd_file\":\""));out(sd_name);out(F("\"}"));end_line();
}
static void hello(){
    out(F("{\"type\":\"hello\",\"protocol\":1,\"device\":\"Thrust measurement Nano\",\"capabilities\":[\"tare\",\"calibrate\",\"set_config\",\"get_config\",\"save_config\"]}"));
    end_line();send_config();
}
static void load_config(){
    SavedConfig saved;EEPROM.get(EEPROM_ADDRESS,saved);
    const bool valid=saved.magic==CONFIG_MAGIC&&saved.version==1&&
        saved.crc==rig_nano::crc16(reinterpret_cast<const uint8_t*>(&saved),offsetof(SavedConfig,crc))&&rig_nano::valid_config(saved.value);
    if(valid){config=saved.value;persisted=true;return;}
    // Legacy v4 stores an unsigned factor/10 in bytes 0..1 and poles in byte 2.
    // Never overwrite these archived settings when migrating to the new record.
    config=rig_nano::import_legacy(EEPROM.read(0),EEPROM.read(1),EEPROM.read(2));
}
static bool save_config(){
    SavedConfig saved{};saved.magic=CONFIG_MAGIC;saved.version=1;saved.value=config;
    saved.crc=rig_nano::crc16(reinterpret_cast<const uint8_t*>(&saved),offsetof(SavedConfig,crc));
    EEPROM.put(EEPROM_ADDRESS,saved);
    SavedConfig verify;EEPROM.get(EEPROM_ADDRESS,verify);
    persisted=memcmp(&saved,&verify,sizeof(saved))==0;return persisted;
}
// Bounded master-only TWI keeps the 328P's scarce SRAM free of slave buffers.
static bool twi_wait(){
    const uint32_t start=micros();
    while(!(TWCR&_BV(TWINT))){pump_rx();if(micros()-start>25000){TWCR=0;TWCR=_BV(TWEN);return false;}}
    return true;
}
static void twi_stop(){
    TWCR=_BV(TWINT)|_BV(TWEN)|_BV(TWSTO);
    const uint32_t start=micros();while(TWCR&_BV(TWSTO)){pump_rx();if(micros()-start>1000){TWCR=0;TWCR=_BV(TWEN);break;}}
}
static bool twi_start(uint8_t address,bool read){
    TWCR=_BV(TWINT)|_BV(TWEN)|_BV(TWSTA);
    if(!twi_wait()||((TWSR&0xf8)!=0x08&&(TWSR&0xf8)!=0x10))return false;
    TWDR=(address<<1)|(read?1:0);TWCR=_BV(TWINT)|_BV(TWEN);
    return twi_wait()&&(TWSR&0xf8)==(read?0x40:0x18);
}
static bool twi_write(uint8_t value){TWDR=value;TWCR=_BV(TWINT)|_BV(TWEN);return twi_wait()&&(TWSR&0xf8)==0x28;}
static bool twi_read(uint8_t& value,bool last){
    TWCR=_BV(TWINT)|_BV(TWEN)|(last?0:_BV(TWEA));
    if(!twi_wait()||(TWSR&0xf8)!=(last?0x58:0x50))return false;
    value=TWDR;return true;
}
static bool ads_start(uint8_t channel){
    // Single-shot, AIN0/1 to GND, +/-6.144 V PGA, 128 SPS, comparator disabled.
    const uint16_t bits=rig_nano::ads_config(channel);
    const bool ok=twi_start(0x48,false)&&twi_write(1)&&twi_write(bits>>8)&&twi_write(bits);
    twi_stop();return ok;
}
static bool ads_read(uint8_t reg,int16_t& value){
    uint8_t high=0,low=0;
    const bool ok=twi_start(0x48,false)&&twi_write(reg)&&twi_start(0x48,true)&&twi_read(high,false)&&twi_read(low,true);
    twi_stop();value=static_cast<int16_t>((static_cast<uint16_t>(high)<<8)|low);return ok;
}
static void update_adc(uint32_t now){
    if(!adc_waiting){
        if(static_cast<int32_t>(now-adc_retry)<0)return;
        adc_channel=0;adc_waiting=ads_start(0);adc_started=now;
        if(!adc_waiting){adc_healthy=false;adc_retry=now+1000;}
        return;
    }
    if(now-adc_started<9)return;
    int16_t ready=0,value=0;
    if(!ads_read(1,ready)||now-adc_started>40){adc_healthy=false;adc_waiting=false;adc_retry=now+1000;return;}
    if(!(static_cast<uint16_t>(ready)&0x8000))return;
    if(!ads_read(0,value)){adc_healthy=false;adc_waiting=false;adc_retry=now+1000;return;}
    adc_raw[adc_channel]=value;
    if(adc_channel==0){adc_channel=1;adc_waiting=ads_start(1);adc_started=now;if(!adc_waiting){adc_healthy=false;adc_retry=now+1000;}}
    else{adc_healthy=true;adc_ms=now;adc_waiting=false;adc_retry=now+50;}
}
static bool hx_read(int32_t& result){
    if(PIND&_BV(PD3))return false;
    uint32_t value=0;
    for(uint8_t bit=0;bit<24;++bit){
        // Protect only the short HIGH phase; service Hall/serial IRQs between bits.
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE){PORTD|=_BV(PD2);delayMicroseconds(1);value=(value<<1)|((PIND&_BV(PD3))?1:0);PORTD&=~_BV(PD2);}
        delayMicroseconds(1);
    }
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE){PORTD|=_BV(PD2);delayMicroseconds(1);PORTD&=~_BV(PD2);}
    result=static_cast<int32_t>((value&0x800000)?value|0xff000000UL:value);return true;
}
static void begin_phase(Phase next,uint32_t id,float load=0){
    phase=next;phase_request=id;known_load=load;phase_started=millis();phase_count=0;phase_sum=0;
}
static void finish_phase(bool ok,const __FlashStringHelper* message){
    const bool startup=phase==Phase::BootTare;phase=Phase::None;
    send_config();if(startup){if(!ok)event(message);}else ack(phase_request,ok,message);
}
static void accumulate(int32_t value){
    if(phase==Phase::None)return;
    if(!phase_count)phase_min=phase_max=value;
    if(value<phase_min)phase_min=value;
    if(value>phase_max)phase_max=value;
    phase_sum+=value;
    if(++phase_count<20)return;
    const int32_t mean=phase_sum/phase_count;
    if(phase==Phase::Tare||phase==Phase::BootTare){zero_offset=mean;tared=true;finish_phase(true,F("Tare complete; EEPROM unchanged"));}
    else{
        const float delta=static_cast<float>(mean)-zero_offset;
        if(fabs(delta)<100||phase_max-phase_min>fabs(delta)*0.03f+40){finish_phase(false,F("Reference is too small or unstable"));return;}
        Config proposed=config;proposed.factor=delta/known_load;
        if(!rig_nano::valid_config(proposed)){finish_phase(false,F("Calculated factor outside supported range"));return;}
        config=proposed;persisted=false;finish_phase(true,F("Calibration applied; save EEPROM after verification"));
    }
}
static void setup_sd(){
    if(!SD.begin(4)){event(F("Nano SD unavailable; USB measurements continue"));return;}
    uint32_t next=1;
    File root=SD.open("/");
    while(root){File entry=root.openNextFile();if(!entry)break;
        const char* name=entry.name();
        if(!entry.isDirectory()&&strlen(name)==12&&!strncmp_P(name,PSTR("RIG"),3)&&!strcmp_P(name+8,PSTR(".CSV"))){
            uint32_t id=0;bool valid=true;
            for(uint8_t i=3;i<8;++i){if(name[i]<'0'||name[i]>'9'){valid=false;break;}id=id*10+name[i]-'0';}
            if(valid&&id>=next)next=id+1;
        }
        entry.close();pump_rx();
    }
    root.close();
    if(next>99999){event(F("Nano SD run filename limit reached"));return;}
    snprintf_P(sd_name,sizeof(sd_name),PSTR("RIG%05lu.CSV"),static_cast<unsigned long>(next));
    if(SD.exists(sd_name)){event(F("Nano SD file already exists; preserving it"));sd_name[0]=0;return;}
    sd_log=SD.open(sd_name,FILE_WRITE);
    if(!sd_log){event(F("Nano SD file could not be created"));sd_name[0]=0;return;}
    sd_log.println(F("Time,Thrust,Throttle,Vbat,Current,RPM"));sd_log.flush();
    sd_ok=!sd_log.getWriteError();
    if(!sd_ok){sd_log.close();event(F("Nano SD header write failed"));}
}
static void csv_cell(float value,uint8_t digits){if(isfinite(value))sd_log.print(value,digits);}
static void write_sd(uint32_t now,float thrust,float throttle,float voltage,float current,float rpm){
    if(!sd_ok)return;
    // Same Nano uptime as time_ms in the USB stream, with millisecond precision.
    sd_log.print(now/1000);sd_log.write('.');uint16_t fraction=now%1000;
    if(fraction<100)sd_log.write('0');
    if(fraction<10)sd_log.write('0');
    sd_log.print(fraction);sd_log.write(',');
    csv_cell(thrust,5);sd_log.write(',');csv_cell(throttle,3);sd_log.write(',');csv_cell(voltage,5);sd_log.write(',');
    csv_cell(current,5);sd_log.write(',');csv_cell(rpm,3);sd_log.println();pump_rx();
    if(++flush_count>=10){sd_log.flush();flush_count=0;}
    if(sd_log.getWriteError()){sd_ok=false;sd_log.close();event(F("Nano SD write failed; USB measurements continue"));send_config();}
}
static void send_sample(uint32_t now,float thrust,float throttle,float voltage,float current,float rpm){
    out(F("{\"type\":\"sample\",\"protocol\":1,\"seq\":"));out(++sequence);out(F(",\"time_ms\":"));out(now);
    out(F(",\"thrust\":"));number(thrust,5);out(F(",\"throttle\":"));number(throttle,3);
    out(F(",\"voltage\":"));number(voltage,5);out(F(",\"current\":"));number(current,5);out(F(",\"rpm\":"));number(rpm,3);out(F("}"));end_line();
}
static int16_t telemetry_value(float v){if(!isfinite(v))return 0;return static_cast<int16_t>(constrain(v,-32768.0f,32767.0f));}
static void legacy_telemetry(float thrust,float voltage,float current,float rpm){
    // Preserve the optional, soldered I2C telemetry Nano; absence is nonfatal.
    const int16_t values[]={telemetry_value(thrust*100),telemetry_value(voltage*100),telemetry_value(current*10),telemetry_value(rpm/2)};
    bool ok=twi_start(8,false);
    for(uint8_t i=0;i<4&&ok;++i)ok=twi_write(static_cast<uint8_t>(values[i]))&&twi_write(static_cast<uint8_t>(values[i]>>8));
    if(ok)twi_write(static_cast<uint8_t>(config.poles));
    twi_stop();
}
static void process_request(){
    if(rx_overflow){event(F("USB command too long; discarded"));return;}
    const auto error=deserializeJson(request,rx_line,DeserializationOption::NestingLimit(3));
    if(error){event(F("Invalid USB command JSON"));return;}
    const JsonObject object=request.as<JsonObject>();
    if(!object[F("request_id")].is<uint32_t>()){event(F("Invalid USB request ID"));return;}
    const uint32_t id=object[F("request_id")].as<uint32_t>();
    const char* type=object[F("type")]|"";const char* command=object[F("command")]|"";
    if(strcmp_P(type,PSTR("command"))||!object[F("protocol")].is<uint8_t>()||object[F("protocol")].as<uint8_t>()!=1||!object[F("args")].is<JsonObject>()){
        ack(id,false,F("Expected command protocol 1 and args object"));return;
    }
    if(!strcmp_P(command,PSTR("hello"))){
        if(phase==Phase::Tare||phase==Phase::Calibrate)finish_phase(false,F("USB session restarted; operation canceled"));
        last_request=0;hello();return;
    }
    if(!id){ack(id,false,F("Request zero is reserved for hello"),false);return;}
    if(id==last_request&&last_message){ack(id,last_ok,last_message,false);return;}
    if(phase!=Phase::None){if(id!=phase_request)ack(id,false,F("Load-cell operation is busy"));return;}
    if(!strcmp_P(command,PSTR("get_config"))){send_config();ack(id,true,F("Configuration read"));return;}
    if(!strcmp_P(command,PSTR("tare"))){begin_phase(Phase::Tare,id);return;}
    if(!strcmp_P(command,PSTR("calibrate"))){
        JsonVariant value=object[F("args")][F("known_load")];
        const float load=value.as<float>();
        if(!value.is<float>()||!isfinite(load)||load<=0||load>100000){ack(id,false,F("Known load must be positive"));return;}
        if(!tared){ack(id,false,F("Tare before calibrating"));return;}
        begin_phase(Phase::Calibrate,id,load);return;
    }
    if(!strcmp_P(command,PSTR("set_config"))){
        JsonObject args=object[F("args")];Config next;
        if(!args[F("calibration_factor")].is<float>()||!args[F("shunt_ohms")].is<float>()||!args[F("divider_ratio")].is<float>()||!args[F("poles")].is<uint16_t>()){
            ack(id,false,F("Configuration requires four numeric fields"));return;
        }
        next.factor=args[F("calibration_factor")].as<float>();next.shunt=args[F("shunt_ohms")].as<float>();next.divider=args[F("divider_ratio")].as<float>();next.poles=args[F("poles")].as<uint16_t>();
        if(!rig_nano::valid_config(next)){ack(id,false,F("Configuration outside supported bounds"));return;}
        config=next;persisted=false;send_config();ack(id,true,F("Settings applied; EEPROM unchanged"));return;
    }
    if(!strcmp_P(command,PSTR("save_config"))){const bool ok=save_config();send_config();ack(id,ok,ok?F("Configuration saved to EEPROM"):F("EEPROM verification failed"));return;}
    ack(id,false,F("Unsupported command"));
}
ISR(PCINT0_vect){
    const uint8_t current=PINB,changed=current^previous_port;previous_port=current;
    if(changed&_BV(PB0))++edges;
    if(changed&_BV(PB1)){const uint32_t now=micros();if(current&_BV(PB1))pwm_rise=now;
        else{const uint32_t width=now-pwm_rise;if(width>=750&&width<=2250){pwm_width=width;pwm_seen=now;}}}
}
void setup(){
    Serial.begin(115200);load_config();
    pinMode(2,OUTPUT);digitalWrite(2,LOW);pinMode(3,INPUT);
    pinMode(8,INPUT_PULLUP);pinMode(9,INPUT_PULLUP);pinMode(5,INPUT);pinMode(6,INPUT);
    previous_port=PINB;PCMSK0=_BV(PCINT0)|_BV(PCINT1);PCICR|=_BV(PCIE0);
    pinMode(A4,INPUT_PULLUP);pinMode(A5,INPUT_PULLUP);TWSR=0;TWBR=(F_CPU/100000UL-16)/2;TWCR=_BV(TWEN);
    hello();
    setup_sd();send_config();begin_phase(Phase::BootTare,0);
    sample_us=micros();sample_ms=millis();
}
void loop(){
    pump_rx();
    if(rx_ready){process_request();rx_length=0;rx_ready=false;rx_overflow=false;request.clear();}
    const uint32_t now=millis();
    update_adc(now);pump_rx();
    int32_t raw;
    if(hx_read(raw)){last_raw=raw;raw_ms=now;accumulate(raw);}
    if(phase!=Phase::None&&now-phase_started>3500)finish_phase(false,F("Load cell timeout; check HX711 wiring/power"));
    if(now-sample_ms<100)return;
    sample_ms=now;
    uint32_t count,seen;uint16_t width;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE){count=edges;edges=0;width=pwm_width;seen=pwm_seen;}
    const uint32_t tick=micros(),elapsed=tick-sample_us;sample_us=tick;
    if(!tared||!raw_ms||now-raw_ms>400)return;
    const float thrust=(static_cast<float>(last_raw)-zero_offset)/config.factor;
    if(!isfinite(thrust))return;
    const float throttle=rig_nano::throttle_valid(width,tick-seen)?constrain((width-1000)/10.0f,0.0f,100.0f):NAN;
    const bool fresh=adc_healthy&&now-adc_ms<=400;
    const float voltage=fresh?adc_raw[1]*0.0001875f*config.divider:NAN;
    const float current=fresh?adc_raw[0]*0.0001875f/config.shunt:NAN;
    const float rpm=rig_nano::rpm_from_edges(count,elapsed,config.poles);
    send_sample(now,thrust,throttle,voltage,current,rpm);
    write_sd(now,thrust,throttle,voltage,current,rpm);
    legacy_telemetry(thrust,voltage,current,rpm);
}
