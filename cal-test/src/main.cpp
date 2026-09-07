#include <Arduino.h>
#define SENS A0
#define NO_RESISTOR_LDR 1
inline int readSensor(){ return 1023 - analogRead(SENS); }
void setup(){ pinMode(SENS, INPUT_PULLUP); Serial.begin(9600); delay(500); Serial.println("CAL TEST start");
  int mn=1023,mx=0; unsigned long t0=millis();
  Serial.println("loop start");
  while(millis()-t0<2000){ int v=readSensor(); if(v<mn)mn=v; if(v>mx)mx=v; delay(5); }
  Serial.print("done mn=");Serial.print(mn);Serial.print(" mx=");Serial.println(mx);
  Serial.print("threshold=");Serial.println((mn+mx)/2);
}
void loop(){ int v=readSensor(); Serial.print("v=");Serial.println(v); delay(500);}
