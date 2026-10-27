#pragma once
#include <stdint.h>
#include <math.h>
#include <stddef.h>

namespace rig_nano {
struct Config {
    float factor=30312.0f;
    float shunt=0.000192f;
    float divider=34.13f;
    uint16_t poles=20;
};
inline bool valid_config(const Config& c) {
    return isfinite(c.factor)&&fabs(c.factor)>=1.0f&&fabs(c.factor)<=10000000.0f&&
        isfinite(c.shunt)&&c.shunt>=0.000000001f&&c.shunt<=1.0f&&
        isfinite(c.divider)&&c.divider>=1.0f&&c.divider<=1000.0f&&c.poles>=1&&c.poles<=220;
}
inline Config import_legacy(uint8_t low,uint8_t high,uint8_t poles) {
    Config result;
    const uint16_t legacy=low|(static_cast<uint16_t>(high)<<8);
    if(legacy>=200&&legacy<=14000)result.factor=legacy*10.0f;
    if(poles>=1&&poles<=220)result.poles=poles;
    return result;
}
// Single-shot, +/-6.144 V, 250 SPS; channels are converted sequentially.
inline uint16_t ads_config(uint8_t channel) {return 0x81a3|(static_cast<uint16_t>(4+channel)<<12);}
inline uint16_t crc16(const uint8_t* bytes,size_t length) {
    uint16_t crc=0xffff;
    while(length--){crc^=*bytes++;for(uint8_t bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1)?0xa001:0);}
    return crc;
}
inline float rpm_from_edges(uint32_t edges,uint32_t elapsed_us,uint16_t poles) {
    return elapsed_us&&poles?static_cast<float>(edges)*60000000.0f/elapsed_us/poles:0.0f;
}
inline bool throttle_valid(uint16_t width,uint32_t age_us) {return width>=750&&width<=2250&&age_us<=100000;}
}
