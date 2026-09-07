#include <Arduino.h>
#define SENS A0
void setup(){ pinMode(SENS, INPUT_PULLUP); Serial.begin(9600); Serial.println("RX MONITOR light = 1023 - raw, every 200ms");}
void loop(){ int raw=analogRead(SENS); int light=1023-raw; Serial.print("raw=");Serial.print(raw);Serial.print(" light=");Serial.print(light); Serial.println(light>500?" BRIGHT":" DARK"); delay(200);}
