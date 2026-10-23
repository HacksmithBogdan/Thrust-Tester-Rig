#include <Wire.h>
#include <Adafruit_ADS1015.h>
#include "HX711.h"
#include <SPI.h>
#include <SD.h>
#define DOUT  3
#define CLK  2

//#include "FrSkySportSensor.h"
////#include "FrSkySportSensorFlvss.h"
//#include "FrSkySportSensorFcs.h"
//#include "FrSkySportSensorRpm.h"
//#include "FrSkySportSensorAss.h"
//#include "FrSkySportSingleWireSerial.h"
//#include "FrSkySportTelemetry.h"
////#include "SoftwareSerial.h"

#define DEBUG 1

//FrSkySportSensorFlvss flvss1;
//FrSkySportSensorFlvss flvss2(FrSkySportSensor::ID15);
////FrSkySportSensorRpm rpmTelem;
////FrSkySportSensorFcs IVTelem;
////FrSkySportSensorAss AirspeedTelem;
////FrSkySportTelemetry telemetry;


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
float calibration_factor = 30312; //39000 seems to work for our cells //38000 for trust tester (large edf) //27463.6 for K-310

float ThrottlePrecentage = 0.0;
float Thrust = 0.0;
float Vbat = 0.0;
float Current = 0.0;
String dataString = "";

int HallPin = 5;
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


void setup(void) {

  Serial.begin(9600);
  #ifdef DEBUG
      while (!Serial) {
          ; // wait for serial port to connect. Needed for native USB port only
        }
        Serial.print("Initializing SD card...");
        // see if the card is present and can be initialized:
        if (!SD.begin(chipSelect)) {
          Serial.println("Card failed, or not present");
          // don't do anything more:
          return;
        }
        Serial.println("card initialized.");
  #endif


//  telemetry.begin(FrSkySportSingleWireSerial::SOFT_SERIAL_PIN_5, &IVTelem, &rpmTelem, &AirspeedTelem); //initialize telemetry outputs!


  ads.begin();

  pinMode(ThrottlePin, INPUT);
  pinMode(HallPin, INPUT);
  
  scale.tare(); //Reset the scale to 0
  long zero_factor = scale.read_average(); //Get a baseline reading
//  Serial.print("Zero factor: "); //This can be used to remove the need to tare the scale. Useful in permanent scale projects.
//  Serial.println(zero_factor);
  scale.set_scale(calibration_factor); //Adjust to this calibration factor

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


    //Interrupts on Pin 8 (INT0) and 9 (INT1)
  PCICR |= (1 << PCIE0);
  PCMSK0 |= (1 << PCINT0);
  PCMSK0 |= (1 << PCINT1);
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
        Serial.println(rpm, 1);
    #endif
    

    if ((ThrottlePrecentage >= 1.0) or (Thrust >= 0.5) or (rpm >= 1)){
      dataString = (String(oldMillis/1000.0) + "," + String(Thrust) + "," + String(ThrottlePrecentage) + "," + String(Vbat) + "," + String(Current) + "," + String(rpm));
      //Create or open a datalog
      File dataFile = SD.open("datalog.txt", FILE_WRITE);
      // if the file is available, write to it:
      if (dataFile) {
        dataFile.println(dataString);
        dataFile.close();

//        #ifndef DEBUG
//          Serial.println("1337");
//        #endif
      
      }
      // if the file isn't open, pop up an error:
      else {
         #ifdef DEBUG
        Serial.println("error opening datalog.txt");
        #endif
      }
      
    }
    
  }

//  IVTelem.setData(Current, Vbat); //Current, Voltage Telemetry
//  rpmTelem.setData(rpm/2, 0, 0); // Rpm, Temp1, Temp2 Telemetry
//  AirspeedTelem.setData(Thrust);
//  telemetry.send(); // Send telemetry

  
}


ISR(PCINT1_vect) {
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

