#include <EEPROM.h>
#include <Wire.h>
#include <Adafruit_ADS1015.h>
#include "HX711.h"
#include <SPI.h>
#include <SD.h>
#define DOUT  3
#define CLK  2

//#define DEBUG 1

// ** MOSI - pin 11
// ** MISO - pin 12
// ** CLK - pin 13
// ** CS - pin 4 (for MKRZero SD: SDCARD_SS_PIN)
// SD Card Chip Select (CS)
const int chipSelect = 4;

//Interrupts
unsigned long timer[5];
byte last_channel[4];
int input[4];

//Load Cell Amp
HX711 scale(DOUT, CLK);
float calibration_factor = 30312; // WILL BE OVERWRITTEN BY EEPROM!!! (//39000 seems to work for our cells //38000 for trust tester (large edf) //27463.6 for K-310 // 30312 for Schubler)
bool EEPROMUPDATEQUEQE = false;

float ThrottlePrecentage = 0.0;
float TrimIN = 0.0;
float Thrust = 0.0;
float Vbat = 0.0;
float Current = 0.0;
String dataString = "";
float TrainerPos = 1000;

int TrimPin = 5;
int TrainerPin = 6;
//int HallPin = 5;
int ThrottlePin = 9;
bool OldState = LOW;
int StateSwapCount = 0;
long oldMillis = 0;
float rpm = 0;

float ShuntResistance = 0.000192; //0.0001875 Nominal? (Reads High)
float VoltageDividerRatio = 34.13; //34.13 Origional //34.245 Fluke Clamp
int MagnetPoles = 20; //Magnets on Bell
int RefreshRate = 10; //Cycles Per Second


//ADC
Adafruit_ADS1115 ads(0x48);
float Voltage = 0.0;
float Voltage2 = 0.0;

int TxThrust;
int TxVbat;
int TxCurrent;
int TxRPM;
byte TxNPoles;

void setup(void) {
  
  //Interrupts on Pin 8 (INT0) and 9 (INT1)
  PCICR |= (1 << PCIE0);
  PCICR |= (1 << PCIE2);
  PCMSK0 |= (1 << PCINT0);
  PCMSK0 |= (1 << PCINT1);
  PCMSK0 |= (1 << PCINT2);
  PCMSK2 |= (1 << PCINT21);
  PCMSK2 |= (1 << PCINT22);
  
  Serial.begin(9600);
  while (!Serial) {
      ; // wait for serial port to connect. Needed for native USB port only
    }
    #ifdef DEBUG
      Serial.print("Initializing SD card...");
    #endif
    // see if the card is present and can be initialized:
    if (!SD.begin(chipSelect)) {
      #ifdef DEBUG
        Serial.println("Card failed, or not present");
      #endif
      // don't do anything more:
      return;
    }
    #ifdef DEBUG
      Serial.println("card initialized.");
    #endif


  ads.begin();
  Wire.begin(); // join i2c bus (address optional for master)

  pinMode(ThrottlePin, INPUT);
  //pinMode(HallPin, INPUT);
  pinMode(TrimPin, INPUT);
  pinMode(TrainerPin, INPUT);
  
  scale.tare(); //Reset the scale to 0
  long zero_factor = scale.read_average(); //Get a baseline reading
//  Serial.print("Zero factor: "); //This can be used to remove the need to tare the scale. Useful in permanent scale projects.
//  Serial.println(zero_factor);

  calibration_factor = (EEPROM.read(0) | EEPROM.read(1) << 8);
  calibration_factor *= 10;
  scale.set_scale(calibration_factor); //Adjust to this calibration factor

  MagnetPoles = EEPROM.read(2); //

  dataString = ("Time, Thrust, Throttle, Vbat, Current, RPM");
  //Create or open a datalog
  File dataFile = SD.open("datalog.txt", FILE_WRITE);
  // if the file is available, write to it:
  if (dataFile) {
    dataFile.println(dataString);
    dataFile.close();
  }
  // if the file isn't open, pop up an error:
  else {
    #ifdef DEBUG
      Serial.println("error opening datalog.txt");
    #endif
  }
}

void loop(void) 
{
  if (millis()-oldMillis >= (1000/RefreshRate)){
    oldMillis = millis();

    int16_t adc0;  // we read from the ADC, we have a sixteen bit integer as a result
    int16_t adc1;
  
    adc0 = ads.readADC_SingleEnded(0);
    adc1 = ads.readADC_SingleEnded(1);
    Voltage = (adc0 * 0.1875)/1000;
    Voltage2 = (adc1 * 0.1875)/1000;
    
    rpm = ((StateSwapCount / (MagnetPoles * 1.0)) * (60.0 * RefreshRate));
    StateSwapCount = 0;
    Thrust = scale.get_units();
    ThrottlePrecentage = map(input[1], 1000, 2000, 0, 1000)/ 10.0;
    Vbat = (Voltage2*VoltageDividerRatio);
    Current = (Voltage/ShuntResistance);

    TrimIN = map(input[2], 1000, 2000, -1000, 1000)/ 2.0;
    TrainerPos = input[3];

    if (TrainerPos < 1200 and TrainerPos > 500 and (calibration_factor - TrimIN > 2000) and (calibration_factor - TrimIN < 140000)){
      calibration_factor -= TrimIN;
      scale.set_scale(calibration_factor);
      #ifdef DEBUG
        Serial.println(calibration_factor);
      #endif
      EEPROMUPDATEQUEQE = true;
    }
    else if (TrainerPos > 1800 and (MagnetPoles + TrimIN/100 > 0) and (MagnetPoles + TrimIN/100 < 221) ){
      MagnetPoles += int(TrimIN/100);
      #ifdef DEBUG
        Serial.println(MagnetPoles);
      #endif
      EEPROMUPDATEQUEQE = true;
    } 
    else if (EEPROMUPDATEQUEQE and TrainerPos > 1200 and TrainerPos < 1800){ 
      EEPROM.write(0, int(calibration_factor/10.0));
      EEPROM.write(1, int(calibration_factor/10.0) >> 8);
      EEPROM.write(2, byte(MagnetPoles));
      #ifdef DEBUG
        Serial.print("New calibration factor of ");
        Serial.print(calibration_factor);
        Serial.print(" and nPoles of ");
        Serial.print(MagnetPoles);
        Serial.println(" written to EEPROM!");
      #endif
      EEPROMUPDATEQUEQE = false;
    }

 
    #ifdef DEBUG
      Serial.print("Time: ");
      Serial.print(oldMillis/1000.0);
      Serial.print("\tThrust: ");
      Serial.print(Thrust, 2);
      Serial.print("\tThrottle: ");
      Serial.print(ThrottlePrecentage , 1);
      Serial.print("\tVBat: ");
      Serial.print(Vbat, 2);
      Serial.print("\tI: ");
      Serial.print(Current, 2);
      Serial.print(String(" ") + String("\tRPM: "));
      Serial.print(rpm, 1);
      
      Serial.print("\tTrim: ");
      Serial.print(TrimIN);
      Serial.print("\tTrainerSW: ");
      Serial.print(TrainerPos);
      Serial.print("\tnPoles: ");
      Serial.println(MagnetPoles);
    #endif

    TxThrust = int(Thrust * 100.00);
    TxVbat = int(Vbat * 100.00);
    TxCurrent = int(Current * 10.00);
    TxRPM = int(rpm/2);
    TxNPoles = MagnetPoles;

    Wire.beginTransmission(8); // transmit to device #8
    Wire.write(TxThrust);           // send the lower 8 bits
    Wire.write((TxThrust >> 8));    // send the upper 8 bits
    Wire.write(TxVbat);             // send the lower 8 bits
    Wire.write((TxVbat >> 8));      // send the upper 8 bits
    Wire.write(TxCurrent);          // send the lower 8 bits
    Wire.write((TxCurrent >> 8));   // send the upper 8 bits
    Wire.write(TxRPM);              // send the lower 8 bits
    Wire.write((TxRPM >> 8));       // send the upper 8 bits
    Wire.write(TxNPoles);           // send the byte
    Wire.endTransmission();    // stop transmitting

    if ((ThrottlePrecentage >= 1.0) or (Thrust >= 0.5) or (rpm >= 1)){
      dataString = (String(oldMillis/1000.0) + "," + String(Thrust) + "," + String(ThrottlePrecentage) + "," + String(Vbat) + "," + String(Current) + "," + String(rpm));
      //Create or open a datalog
      File dataFile = SD.open("datalog.txt", FILE_WRITE);
      // if the file is available, write to it:
      if (dataFile) {
        dataFile.println(dataString);
        dataFile.close();
      }
      // if the file isn't open, pop up an error:
      else {
        #ifdef DEBUG
          Serial.println("error opening datalog.txt");
        #endif
      }
    }
    
  }
}

ISR(PCINT0_vect) {
  timer[0] = micros();
  // HALL EFFECT RPM
  if ( (last_channel[0] == 0 && PINB & B00000001) or (last_channel[0] == 1 && !(PINB & B00000001)) ) {
    last_channel[0] = !(last_channel[0]);
    StateSwapCount +=1;
  }

  // RECIEVER PWM
  if (last_channel[1] == 0 && PINB & B00000010 ) {
    last_channel[1] = 1;
    timer[2] = timer[0];
  }
  else if (last_channel[1] == 1 && !(PINB & B00000010) ) {
    last_channel[1] = 0;
    input[1] = timer[0] - timer[2];
  }
}

ISR(PCINT2_vect){
  timer[0] = micros();
  
  // Trim PWM
  if(last_channel[2] == 0 && (PIND & B00100000)) {
    last_channel[2] = 1;
    timer[3] = timer[0];
  }
  else if(last_channel[2] == 1 && ! (PIND & B00100000)) {
    last_channel[2] = 0;
    input[2] = timer[0] - timer[3];
  }

  //Trainer SW PWM
  // channel 4 ---------------
  if(last_channel[3] == 0 && (PIND & B01000000)) {
    last_channel[3] = 1;
    timer[4] = timer[0];
  }
  else if(last_channel[3] == 1 && ! (PIND & B01000000) ) {
    last_channel[3] = 0;
    input[3] = timer[0] - timer[4];
  }
}
