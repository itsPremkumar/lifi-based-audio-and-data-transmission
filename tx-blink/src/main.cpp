#include <Arduino.h>
#define LED 8
void setup(){ pinMode(LED,OUTPUT); pinMode(9,INPUT); Serial.begin(9600); Serial.println("TX BLINK 500ms ON/OFF on D8");}
void loop(){ digitalWrite(LED,HIGH); Serial.println("LED ON"); delay(500); digitalWrite(LED,LOW); Serial.println("LED OFF"); delay(500);}
