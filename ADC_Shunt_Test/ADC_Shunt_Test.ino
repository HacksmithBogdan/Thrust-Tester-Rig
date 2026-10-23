#include <Wire.h>
#include <Adafruit_ADS1015.h>

Adafruit_ADS1115 ads(0x48);
float Voltage = 0.0;
float Voltage2 = 0.0;


void setup(void) 
{
  Serial.begin(9600);  
  ads.begin();
}

void loop(void) 
{
  int16_t adc0;  // we read from the ADC, we have a sixteen bit integer as a result
  int16_t adc1;

  adc0 = ads.readADC_SingleEnded(0);
  adc1 = ads.readADC_SingleEnded(1);
  Voltage = (adc0 * 0.1875)/1000;
  Voltage2 = (adc1 * 0.1875)/1000;
  
  Serial.print("AIN0: "); 
  Serial.print(adc0);
  Serial.print("\tVr: ");
  Serial.print(Voltage, 7);
  Serial.print("\tI: ");
  Serial.print(Voltage/0.0001875, 4);
  Serial.print("\tVBat: ");
  Serial.println(Voltage2*34.13, 2); 
  //Serial.println();
  
  delay(50);
}
