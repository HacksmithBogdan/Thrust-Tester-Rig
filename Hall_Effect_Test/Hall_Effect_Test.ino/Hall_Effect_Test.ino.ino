int HallPin = 5;
bool OldState = LOW;
int StateSwapCount = 0;
long oldMillis = 0;
float rpm = 0;

int MagnetPoles = 14;

void setup() {
  Serial.begin(9600);
  pinMode(HallPin, INPUT);
}

void loop() {

  if ((OldState and not digitalRead(HallPin)) or (digitalRead(HallPin) and not OldState)){
    OldState = digitalRead(HallPin);
    //Serial.println("State Swapped");
    StateSwapCount += 1;
  }

  if (millis()-oldMillis >= 1000){
    oldMillis = millis();
    rpm = (StateSwapCount / (MagnetPoles * 1.0)) * 60.0;
    Serial.println(rpm);
    StateSwapCount = 0;
  }
}
