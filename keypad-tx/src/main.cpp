/*
 * SIMPLE WIRED KEYPAD -> RECEIVER   (TRANSMITTER)
 * =================================================
 * Press a key on the 4x4 keypad -> that character is sent out of D11 as
 * one short line ("5\n"). That is ALL this sketch does.
 *
 * WIRES TO THE RECEIVER (only two!)
 *      this D11  ------------------>  receiver D3
 *      this GND  ------------------>  receiver GND      (mandatory)
 *
 * KEYPAD (4x4 matrix, no diodes)
 *      rows  INPUT_PULLUP : D2 D3 D4 D5
 *      cols  OUTPUT       : D6 D7 D8 D9
 *      pad:      C1  C2  C3  C4      <- columns, top header
 *                1  2  3  A
 *                4  5  6  B
 *                7  8  9  C
 *                *  0  #  D
 *              R1  R2  R3  R4        <- rows, bottom header
 *
 * '*' sends "clr" so the receiver can wipe its number.
 * '*' press = clear, other keys send themselves.
 */

#include <Arduino.h>
#include <SoftwareSerial.h>

// ---- pins -----------------------------------------------------------------
const byte ROWS[4] = { 2, 3, 4, 5 };
const byte COLS[4] = { 6, 7, 8, 9 };
const byte WIRE_TX = 11;          // data out -> receiver D3
const byte WIRE_RX = 10;          // unused
const byte LED     = 12;          // blinks on every keypress

SoftwareSerial link(WIRE_RX, WIRE_TX);

// row*4 + col -> character. The pad order is: 1 2 3 A / 4 5 6 B / 7 8 9 C / * 0 # D
const char KEYS[16] = {
  '1','2','3','A',
  '4','5','6','B',
  '7','8','9','C',
  '*','0','#','D'
};

unsigned long lastScan = 0;
byte lastKey = 255;               // 255 = nothing held
unsigned long pressTime = 0;

void setup() {
  Serial.begin(9600);
  for (byte r = 0; r < 4; r++) pinMode(ROWS[r], INPUT_PULLUP);
  for (byte c = 0; c < 4; c++) { pinMode(COLS[c], OUTPUT); digitalWrite(COLS[c], LOW); }
  pinMode(LED, OUTPUT);

  link.begin(9600);

  Serial.println(F("TX ready - press a key."));
  Serial.println(F("Wire: D11 -> RX D3, GND -> GND."));
}

// Returns 0..15 for the pressed key, or 255 when nothing is pressed.
byte scanKeypad() {
  for (byte c = 0; c < 4; c++) digitalWrite(COLS[c], HIGH);   // idle all HIGH
  for (byte c = 0; c < 4; c++) {
    digitalWrite(COLS[c], LOW);                               // one column LOW at a time
    for (byte r = 0; r < 4; r++) {
      if (digitalRead(ROWS[r]) == LOW) return (byte)(r * 4 + c);
    }
  }
  for (byte c = 0; c < 4; c++) digitalWrite(COLS[c], LOW);
  return 255;
}

void loop() {
  if (millis() - lastScan < 20) return;   // scan every 20 ms
  lastScan = millis();

  byte k = scanKeypad();

  // Send once, on the press edge, only after the key has been down 60 ms
  // (simple debounce - no need for anything fancier here).
  if (k != 255 && k != lastKey) {
    pressTime = millis();
    lastKey = k;
    return;
  }
  if (k != 255 && k == lastKey && millis() - pressTime >= 60) {
    pressTime = millis() + 1000;         // block re-send for 1 s
    char ch = KEYS[k];

    if (ch == '*') {
      link.print(F("clr"));
    } else {
      link.print(ch);
    }
    link.print('\n');

    digitalWrite(LED, HIGH); delay(30); digitalWrite(LED, LOW);

    Serial.print(F("sent: "));
    Serial.println(ch);
    return;
  }
  if (k == 255) lastKey = 255;           // released -> ready for the next press
}