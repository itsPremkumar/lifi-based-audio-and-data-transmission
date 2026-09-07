/*
 * SERIAL DEBUGGER for LiFi project (NO BUTTON, NO MIC, NO RESISTOR build)
 * Upload to RX board to verify hardware. Commands via Serial Monitor @9600:
 *   (streams LDR by default)  B = speaker beep test  S = stats  H = help
 * Wiring: LDR leg1->A0 leg2->GND. PAM8403: D10->R, GND->GND, 5V->VCC, R+/R-->speaker.
 */
#include <Arduino.h>
#define SENS A0
#define SPK 10
int threshold = 930; // matches lifi-receiver no-resistor default
int mn = 1023, mx = 0;
void beep() {
  Serial.println(F("[DBG] Beep D10..."));
  tone(SPK, 1000, 500); delay(700);
  tone(SPK, 500, 500); delay(700);
  noTone(SPK);
  Serial.println(F("[DBG] Beep done. Heard = speaker+PAM8403 OK."));
}
void setup() {
  pinMode(SENS, INPUT_PULLUP);
  pinMode(SPK, OUTPUT);
  pinMode(2, INPUT); pinMode(9, INPUT);
  Serial.begin(9600);
  delay(800);
  Serial.println(F("\n=== SERIAL DEBUG (LDR+A0-GND, SPK=D10, NO BTN) ==="));
  Serial.println(F("Streaming raw/light/thr/state 5x/sec. Cover LDR / shine LED 20-30cm."));
  Serial.println(F("Cmds: B=beep  S=stats/reset  H=help"));
}
void loop() {
  if (Serial.available()) {
    char c = toupper(Serial.read());
    while (Serial.available()) Serial.read();
    if (c == 'B') beep();
    else if (c == 'S') {
      Serial.print(F("[DBG] min=")); Serial.print(mn);
      Serial.print(F(" max=")); Serial.print(mx);
      Serial.print(F(" var=")); Serial.println(mx - mn);
      Serial.println((mx - mn) >= 40 ? F("[DBG] OK: variation>=40") : F("[DBG] FAIL: var<40, check wiring/distance/shade"));
      mn = 1023; mx = 0;
    }
    else if (c == 'H') Serial.println(F("B=beep S=stats H=help"));
  }
  int raw = analogRead(SENS);
  int light = 1023 - raw;
  if (light < mn) mn = light;
  if (light > mx) mx = light;
  Serial.print(F("raw=")); Serial.print(raw);
  Serial.print(F(" light=")); Serial.print(light);
  Serial.print(F(" thr=")); Serial.print(threshold);
  Serial.println(light > threshold ? F(" BRIGHT") : F(" DARK"));
  delay(200);
}
