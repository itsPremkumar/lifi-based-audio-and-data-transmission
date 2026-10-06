/*
 * SIMPLE WIRED KEYPAD -> RECEIVER   (RECEIVER)
 * =================================================
 * Press a key on the transmitter keypad -> the character arrives on D3 and is
 * shown on a 16x2 I2C LCD.
 *
 * WIRES FROM THE TRANSMITTER (only two!)
 *      TX D11  ------------------>  this D3
 *      TX GND  ------------------>  this GND      (mandatory)
 *
 * LCD (16x2 with PCF8574 backpack)
 *      VCC -> 5V, GND -> GND, SDA -> A4, SCL -> A5
 *
 * SERIAL COMMANDS (9600 baud) - use these if the screen stays dark:
 *      T = contrast test (wide pattern, 5 s)
 *      N = next PCF8574 pin mapping (some modules wire RS/EN/BL differently)
 *      S = scan the I2C bus and report the LCD address
 *      K = redraw the keypad screen
 *      B = backlight on/off
 *
 * DISPLAY
 *      line 0:  KEY: 5           <- the key just pressed
 *      line 1:  NUMBER: 1234     <- digits typed so far (max 10)
 *      '*' on the keypad clears the number.
 *
 * The transmitter sends "<key>\n" for a normal key and "clr\n" for '*'.
 */

#include <Arduino.h>
#include <Wire.h>
#include <SoftwareSerial.h>

// ---- pins -----------------------------------------------------------------
const byte WIRE_RX = 3;           // <- transmitter D11
const byte WIRE_TX = 4;           // unused

const byte LCD_COLS = 16;

// rolling number built from the digits pressed so far
char number[11];
byte numberLen = 0;
char lastKey = ' ';

// line assembled from the wire, e.g. "5" or "clr"
char line[8];
byte lineLen = 0;

// ============================================================================
// I2C LCD DRIVER (HD44780 behind a PCF8574 backpack), no external library
// ============================================================================

// PCF8574 bit assignment presets. Preset 0 is the most common module
// (P0=RS, P1=RW, P2=EN, P3=backlight, P4..P7=D4..D7). Others exist, and a wrong
// mapping looks exactly like "backlight on, one black box, no text".
struct LcdMap { byte rsBit, enBit, blBit; bool blHigh; };
const LcdMap LCD_MAPS[] = {
  { 0, 2, 3, true  },     // 0: standard YWRobot / LCD1602 I2C
  { 0, 2, 3, false },     // 1: same pins, backlight active LOW
  { 1, 0, 3, true  },     // 2: RS on P1, EN on P0 (some clones)
};
const byte LCD_MAP_N = sizeof(LCD_MAPS) / sizeof(LCD_MAPS[0]);

byte lcdAddr = 0x27;      // replaced by the boot-time scan
byte lcdMapIdx = 0;
bool lcdFound = false;
bool lcdLamp = true;
bool lcdHold = false;     // true while the contrast test owns the screen

LcdMap map() { return LCD_MAPS[lcdMapIdx]; }

// Look for the LCD on the PCF8574 address window (0x20..0x3F).
bool lcdScan() {
  for (byte a = 0x20; a <= 0x3F; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      lcdAddr = a;
      return true;
    }
  }
  return false;
}

void lcdExpander(byte data) {
  LcdMap m = map();
  byte d = (byte)(data & (byte)~(1 << m.blBit));
  if (m.blHigh) d |= (byte)(1 << m.blBit);
  Wire.beginTransmission(lcdAddr);
  Wire.write(d);
  Wire.endTransmission();
}

void lcdNibble(byte v, bool isData) {     // EN strobe; nibble already in P4..P7
  LcdMap m = map();
  byte d = (byte)((v & 0xF0) | (isData ? (byte)(1 << m.rsBit) : 0));
  lcdExpander((byte)(d | (1 << m.enBit)));
  delayMicroseconds(1);
  lcdExpander(d);
  delayMicroseconds(50);
}

void lcdByte(byte v, bool isData) {
  lcdNibble(v, isData);
  lcdNibble((byte)(v << 4), isData);
}
void lcdCmd(byte v)  { lcdByte(v, false); }
void lcdData(byte v) { lcdByte(v, true); }

void lcdInit() {
  lcdExpander(0);
  delay(50);
  lcdNibble(0x30, false); delayMicroseconds(4500);   // 8-bit -> 4-bit handshake
  lcdNibble(0x30, false); delayMicroseconds(4500);
  lcdNibble(0x30, false); delayMicroseconds(150);
  lcdNibble(0x20, false); delayMicroseconds(100);
  lcdCmd(0x28);   // 4-bit, 2 lines, 5x8 font
  delay(5);
  lcdCmd(0x0C);   // display on, cursor off, no blink
  lcdCmd(0x06);   // advance
  lcdCmd(0x01);   // clear
  delay(5);
}

void lcdAt(byte col, byte row) {
  const byte offs[2] = { 0x00, 0x40 };
  if (row > 1) row = 1;
  if (col >= LCD_COLS) col = LCD_COLS - 1;
  lcdCmd((byte)(0x80 | (col + offs[row])));
}

void lcdText(byte col, byte row, const char *s) {
  lcdAt(col, row);
  while (*s) lcdData((byte)*s++);
}

void lcdBlankRow(byte row) {
  lcdAt(0, row);
  for (byte i = 0; i < LCD_COLS; i++) lcdData(' ');
}

void lcdBacklight(bool on) {
  lcdLamp = on;
  LcdMap m = map();
  lcdExpander(on ? (byte)(m.blHigh ? (1 << m.blBit) : 0)
                 : (byte)(m.blHigh ? 0 : (1 << m.blBit)));
  delay(2);
}

// ---------------------------------------------------------------------------
// screens
// ---------------------------------------------------------------------------
void showScreen() {
  if (!lcdFound || lcdHold) return;
  lcdBlankRow(0);
  lcdText(0, 0, "KEY: ");
  lcdData((byte)lastKey);

  lcdBlankRow(1);
  lcdText(0, 1, "NUMBER: ");
  byte col = 8;
  for (byte i = 0; i < numberLen && col < LCD_COLS; i++) lcdData((byte)number[i++]);
}

// Widest possible contrast pattern: a full character set plus a solid bar.
// If THIS is unreadable the fault is the contrast pot / pin mapping, never the
// application code.
void contrastTest() {
  if (!lcdFound) return;
  lcdHold = true;
  lcdAt(0, 0);  lcdText(0, 0, "0123456789ABCDEF");
  lcdAt(0, 1);
  for (byte i = 0; i < LCD_COLS; i++) lcdData(0xFF);
  delay(5000);
  lcdHold = false;
  lcdCmd(0x01); delay(5);
  showScreen();
}

// ============================================================================

SoftwareSerial link(WIRE_RX, WIRE_TX);

void setup() {
  Serial.begin(9600);
  Wire.begin();
  link.begin(9600);

  lcdFound = lcdScan();
  if (lcdFound) {
    lcdInit();
    Serial.print(F("RX ready. LCD found @ 0x"));
    Serial.print(lcdAddr, HEX);
    Serial.print(F(" (map preset "));
    Serial.print(lcdMapIdx);
    Serial.println(F(")."));
    lastKey = '-';
    showScreen();
  } else {
    Serial.println(F("RX ready. NO LCD found on I2C (0x20-0x3F)."));
    Serial.println(F("Check VCC->5V GND->GND SDA->A4 SCL->A5."));
  }
  Serial.println(F("Wire: D3 <- TX D11, GND -> GND."));
  Serial.println(F("Cmds: T=test N=next-map S=scan K=screen B=backlight"));
}

void handleLine(char *text, byte len) {
  // "clr" -> wipe the number
  if (len == 3 && text[0] == 'c' && text[1] == 'l' && text[2] == 'r') {
    numberLen = 0;
    number[0] = '\0';
    lastKey = '*';
    Serial.println(F("cleared"));
    showScreen();
    return;
  }
  if (len < 1) return;

  char key = text[0];
  lastKey = key;

  bool isDigit = (key >= '0' && key <= '9') || key == '.';
  if (isDigit) {
    if (numberLen < 10) {
      number[numberLen++] = key;
      number[numberLen] = '\0';
    } else {
      for (byte i = 1; i < 10; i++) number[i - 1] = number[i];
      number[9] = key;
      number[10] = '\0';
    }
  }

  Serial.print(F("key: "));
  Serial.println(key);
  showScreen();
}

void pollSerial() {
  if (!Serial.available()) return;
  char c = (char)Serial.read();
  if (c != 'T' && c != 't' && c != 'N' && c != 'n' && c != 'S' && c != 's' &&
      c != 'K' && c != 'k' && c != 'B' && c != 'b') return;

  if (c == 'T' || c == 't') {
    Serial.println(F("Contrast test for 5 s - turn the BLUE POT now."));
    contrastTest();
  } else if (c == 'N' || c == 'n') {
    lcdMapIdx = (byte)((lcdMapIdx + 1) % LCD_MAP_N);
    Serial.print(F("map -> "));
    Serial.print(lcdMapIdx);
    Serial.print(F("  RS=P"));
    Serial.print(map().rsBit);
    Serial.print(F(" EN=P"));
    Serial.print(map().enBit);
    Serial.println(F(" (send T to test, K to redraw)"));
    lcdInit();
    lastKey = '?';
    showScreen();
  } else if (c == 'S' || c == 's') {
    lcdFound = lcdScan();
    Serial.print(F("scan: LCD "));
    Serial.println(lcdFound ? F("found") : F("NOT found"));
    if (lcdFound) {
      Serial.print(F("  address 0x"));
      Serial.println(lcdAddr, HEX);
      lcdInit();
      lastKey = '?';
      showScreen();
    }
  } else if (c == 'K' || c == 'k') {
    showScreen();
    Serial.println(F("redrawn"));
  } else if (c == 'B' || c == 'b') {
    lcdBacklight(!lcdLamp);
    Serial.print(F("backlight "));
    Serial.println(lcdLamp ? F("ON") : F("OFF"));
  }
}

void loop() {
  pollSerial();

  while (link.available()) {
    char c = (char)link.read();
    if (c == '\n' || c == '\r') {
      if (lineLen > 0) {
        line[lineLen] = '\0';
        handleLine(line, lineLen);
        lineLen = 0;
      }
      continue;
    }
    if (lineLen < sizeof(line) - 1) line[lineLen++] = c;
  }
}