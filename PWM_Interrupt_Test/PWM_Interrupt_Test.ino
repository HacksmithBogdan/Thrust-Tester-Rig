#include <Wire.h>
unsigned long timer[5];
byte last_channel[4];
int input[4];

int VEL = 0;



void setup() {
  *digitalPinToPCMSK(A0) |= bit (digitalPinToPCMSKbit(A0));
  PCIFR |= bit (digitalPinToPCICRbit(A0));
  PCICR |= bit (digitalPinToPCICRbit(A0));
  //Serial.begin(9600);
  Wire.begin();
}

void loop() {
  VEL = map(input[1], 1000, 2000, -1500, 1500);
  //Serial.println(input[1]);
  Wire.beginTransmission(8); // transmit to device #8
  Wire.write(VEL);           // send the lower 8 bits
  Wire.write((VEL >> 8));    // send the upper 8 bits
  Wire.endTransmission();    // stop transmitting
}





ISR(PCINT1_vect) {
  timer[0] = micros();
  // RECIEVER PWM
  if (last_channel[1] == 0 && digitalRead(A0) ) {
    last_channel[1] = 1;
    timer[2] = timer[0];
  }
  else if (last_channel[1] == 1 && !(digitalRead(A0)) ) {
    last_channel[1] = 0;
    input[1] = timer[0] - timer[2];
  }
}
