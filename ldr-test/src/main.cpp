#include <Arduino.h>
// LDR check: LDR between A0 and GND, no resistor, use internal pull-up
// Cover LDR with hand -> value drops. Shine TX LED -> value rises.
void setup() {
  Serial.begin(9600);
  pinMode(A0, INPUT_PULLUP);
  Serial.println("LDR TEST: values every 500ms. Cover/uncover LDR.");
}
void loop() {
  int raw = analogRead(A0);      // raw with pull-up: bright=LOW, dark=HIGH
  int light = 1023 - raw;        // bright=HIGH (same as receiver code)
  Serial.print("raw=");
  Serial.print(raw);
  Serial.print(" light=");
  Serial.println(light);
  delay(500);
}
