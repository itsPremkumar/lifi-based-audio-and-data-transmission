#include <Arduino.h>
// SPEAKER-ONLY TEST: Uno D10 -> PAM8403 R -> Speaker R+/R-
// No LDR, no LED needed. Upload to RX board COM18.
#define SPK 10
void setup() {
  pinMode(SPK, OUTPUT);
  pinMode(9, INPUT); // D9 free
  Serial.begin(9600);
  Serial.println("SPEAKER TEST: 1kHz beep on D10 -> PAM8403 R");
}
void loop() {
  Serial.println("Beep 1000Hz");
  tone(SPK, 1000, 800);
  delay(1000);
  Serial.println("Beep 500Hz");
  tone(SPK, 500, 800);
  delay(1000);
  noTone(SPK);
  delay(500);
}
