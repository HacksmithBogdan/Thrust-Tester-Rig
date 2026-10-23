#include "FrSkySportSensor.h"
//#include "FrSkySportSensorFlvss.h"
#include "FrSkySportSensorFcs.h"
#include "FrSkySportSensorRpm.h"
#include "FrSkySportSensorAss.h"
#include "FrSkySportSingleWireSerial.h"
#include "FrSkySportTelemetry.h"
//#include "SoftwareSerial.h"

#include <Wire.h>

FrSkySportSensorRpm rpmTelem;
FrSkySportSensorFcs IVTelem;
FrSkySportSensorFcs ThrustTelem(FrSkySportSensor::ID15);
FrSkySportTelemetry telemetry;

double Thrust;
double Vbat;
double Current;
int RPM;
byte nPoles;

void setup() {
  telemetry.begin(FrSkySportSingleWireSerial::SOFT_SERIAL_PIN_5, &IVTelem, &rpmTelem, &ThrustTelem); //initialize telemetry outputs!
  
  Wire.begin(8);                // join i2c bus with address #8
  Wire.onReceive(receiveEvent); // register event
}

void loop() {
  IVTelem.setData(Current/10.00, Vbat/100.00); //Current, Voltage Telemetry
  rpmTelem.setData(RPM, nPoles, 0); // Rpm, Temp1, Temp2 Telemetry
  ThrustTelem.setData(0, Thrust/100.00); // Load Cell Telemetry (Voltage Sensor is 2 Decimal point)
  telemetry.send(); // Send telemetry
}

// function that executes whenever data is received from master
// this function is registered as an event, see setup()
void receiveEvent(int howMany) {
  Thrust = (Wire.read() | Wire.read() << 8);    // receive byte as an integer
  Vbat = (Wire.read() | Wire.read() << 8);    // receive byte as an integer
  Current = (Wire.read() | Wire.read() << 8);    // receive byte as an integer
  RPM = (Wire.read() | Wire.read() << 8);    // receive byte as an integer
  nPoles = Wire.read();
}
