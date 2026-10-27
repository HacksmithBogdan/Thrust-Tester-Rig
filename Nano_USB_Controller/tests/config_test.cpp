#include "config.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cmath>
using namespace rig_nano;
int main(){
    Config c;assert(valid_config(c));
    c=import_legacy(0x32,0x27,6);assert(c.factor==100340&&c.poles==6);
    c=import_legacy(255,255,255);assert(c.factor==30312&&c.poles==20);
    c.factor=-50000;assert(valid_config(c));
    c.factor=0;assert(!valid_config(c));c.factor=NAN;assert(!valid_config(c));
    c=Config{};c.poles=0;assert(!valid_config(c));c.poles=221;assert(!valid_config(c));
    c=Config{};c.shunt=0;assert(!valid_config(c));c.shunt=0.000192f;assert(valid_config(c));
    c.divider=0.5f;assert(!valid_config(c));c.divider=INFINITY;assert(!valid_config(c));
    assert(crc16(reinterpret_cast<const uint8_t*>("123456789"),9)==0x4b37);
    uint8_t record[]={1,2,3,4,5};const auto before=crc16(record,5);record[3]^=0x80;assert(crc16(record,5)!=before);
    assert(ads_config(0)==0xc1a3&&ads_config(1)==0xd1a3);
    assert(std::fabs(rpm_from_edges(500,500000,6)-10000)<0.01);
    assert(rpm_from_edges(0,100000,6)==0&&rpm_from_edges(3,0,6)==0);
    assert(throttle_valid(1500,99999)&&!throttle_valid(1500,100001));
    assert(!throttle_valid(0,0)&&!throttle_valid(4000,0));
    const uint32_t wrapped_age=uint32_t(50)-uint32_t(0xfffffff0UL);assert(wrapped_age==66&&throttle_valid(1000,wrapped_age));
    std::puts("PASS: legacy calibration import, bounds, EEPROM CRC, ADS1115 configuration, real-window RPM, stale PWM and timer wrap");
}
