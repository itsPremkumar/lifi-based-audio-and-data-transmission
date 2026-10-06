/*
 * LiFi Based Audio and Data Transmission Using Arduino
 * ---------------------------------------------------
 * RECEIVER (Arduino Uno #2) - PlatformIO (Arduino framework)
 *
 * Dept. of ECE Mini Project
 *
 * MODES (must match TRANSMITTER mode):
 *   1. AUDIO MODE - samples light sensor on A0, reconstructs audio on D10 -> amp -> speaker
 *   2. DATA MODE  - decodes light OOK bits to text, prints it on Serial AND on the
 *                   16x2 I2C LCD (status screen shows threshold + packet count)
 *
 * RECEIVER WIRING:
 *
 *   Light Sensor - LDR ONLY, NO RESISTOR (this build):
 *     LDR leg 1 ---> A0, LDR leg 2 ---> GND. Nothing to 5V.
 *     Code uses internal pull-up (~34k). A0-GND wiring only.
 *     Keep LDR facing TX LED 20-30cm (NOT 5cm: LDR saturates, variation ~3).
 *     Shade sides with black tube. Indoors, no sunlight.
 *
 *   Audio Output (AUDIO mode) - TUNED FOR PAM8403, D10 version (D9 COMPLETELY FREE):
 *     D10 ---[10k]---+---[10nF to GND]---[10uF electrolytic (+ towards Arduino)]---> PAM8403 R-IN
 *                    (RC low-pass removes PWM carrier, 10uF blocks 2.5V DC)
 *     Arduino GND ---> PAM8403 GND (input GND, MUST common with Arduino)
 *     PAM8403 VCC/GND ---> external 5V 1-2A supply (phone charger).
 *                    Join charger GND to Arduino GND. Do NOT power loud speaker
 *                    from Uno 5V pin (Uno will reset).
 *     PAM8403 R+ / R- ---> 8 ohm Speaker (4 ohm also OK)
 *     L-IN left open (mono). For 2 speakers, split same signal to L-IN + R-IN.
 *     Keep PAM8403 volume knob at middle during testing.
 *     D9 is NOT used on this board (free / high-Z).
 *     NOTE: D10 is 31.25kHz PWM (Timer1 OC1B). Do NOT use analogWrite on D9/D10.
 *
 *   I2C LCD DISPLAY (16x2 HD44780 + PCF8574 "backpack" module):
 *     LCD VCC ---> 5V
 *     LCD GND ---> GND
 *     LCD SDA ---> A4   (Uno hardware I2C SDA)
 *     LCD SCL ---> A5   (Uno hardware I2C SCL)
 *     Backlight jumper on the backpack must be ON (JP1 / "LED").
 *     I2C address is AUTO-SCANNED at boot (0x27 usual, 0x3F next).
 *     No external pull-ups: the backpack already has 4.7k on SDA/SCL.
 *     I2C stays at 100kHz (long dupont wires are not reliable at 400kHz).
 *
 *     >>> THE TWO THINGS THAT LOOK LIKE "NO TEXT" ON THESE MODULES <<<
 *     1) CONTRAST = the blue trimpot on the BACK of the module. Firmware cannot
 *        set it (V0/V1 are hardwired). Too low = blank, too high = solid black
 *        box, correct band = crisp text. On boot the firmware shows a 15 s
 *        "contrast hunt" screen so you can find that band by turning the pot.
 *     2) EXPANDER PIN MAPPING. Cheap modules differ: P0=RS P1=RW P2=EN P3=BL is
 *        the usual one, but clones exist with other assignments. A wrong
 *        mapping shows the same solid black box and contrast will NOT fix it.
 *        Press 'N' to cycle mappings live until the text appears.
 *
 *     No external LCD library is used any more - the driver is in this file
 *     (see "I2C LCD LOW-LEVEL DRIVER" below), so nothing can fail to resolve.
 *     Set LCD_ENABLED 0 at the top of this file to build a display-free firmware.
 *
 *   Mode control: SERIAL ONLY (NO BUTTON in this build).
 *     D2 FREE. Send A=Audio D=Data M=Toggle C=Calibrate ?=Debug +/-=Volume
 *                 L=LCD backlight  R=LCD re-init  X=LCD clear
 *                 T=LCD contrast test  N=LCD pin mapping  I=LCD icons
 *
 *   USB ---> PC, open Serial Monitor @ 9600 to see received text + calibration.
 *
 * CALIBRATION (DATA mode):
 *   - On boot, receiver auto-measures ambient light for 2 sec (progress on LCD).
 *   - Every packet preamble (0xAA 0xAA) re-locks threshold = (min+max)/2.
 *   - Tip: do demo indoors, avoid direct sunlight on sensor. Shade sensor with tube.
 *   - Keep TX LED and RX sensor aligned, distance 10-30cm first, then extend.
 *   - Send 'C' in Serial Monitor to re-calibrate anytime.
 *
 * DATA PROTOCOL (must match transmitter, LDR slow mode):
 *   25 bps OOK (40ms/bit, LDR needs ~30ms), START=OFF, 8 bits LSB-first, STOP=ON
 *   Packet: AA AA 55 LEN PAYLOAD CHECKSUM(XOR) 0x0A
 *
 * LCD TIMING RULE (important):
 *   The LCD is drawn through a DEFERRED screen model. Only lcdService() actually
 *   talks I2C, and it is never called from receivePacket()/receiveByteOOK(),
 *   because an I2C write blocks ~4ms and would break the 40ms/bit bit timing.
 */

#include <Arduino.h>

// ---------------- DATA LINK SELECTION  <-- the simple/wired switch ----------
//   1 = WIRED   : 2 jumper wires carry the data (TX board D11 -> this board's
//                  D3, plus a common GND). Press a key on the transmitter and
//                  the character lands on the LCD instantly.
//   0 = OPTICAL : the original LiFi path - OOK light pulses LED -> LDR at 25 bps
//                  (40 ms/bit, needs 20-30 cm alignment and a working LDR).
#define WIRED_LINK         1
#define WIRE_RX_PIN        3    // <- from transmitter D11
#define WIRE_TX_PIN        4    // unused for reception

// ---------------- I2C LCD configuration (must stay ABOVE the include below) ---
#define LCD_ENABLED       1         // 0 = build without the 16x2 I2C display
#define LCD_COLS          16
#define LCD_ROWS          2
#define LCD_I2C_ADDR      0x27      // usual PCF8574 backpack address (auto-scanned)
#define LCD_I2C_SPEED     100000UL  // 100kHz: safe with long breadboard jumpers
#define LCD_PROBE_TRIES   3         // LCD needs a moment after power-up
#define LCD_PROBE_RETRY_MS 300
#define LCD_MIN_DRAW_MS   500       // redraw throttle (audio meter refresh rate)
#define LCD_MSG_HOLD_MS   4000      // how long a received message stays on screen
#define LCD_HUNT_MS       15000     // boot "contrast hunt" screen duration
#define LCD_USE_ICONS     1         // custom glyphs on/off (toggle live with 'I')

#include <Wire.h>

// Backlight state, kept at file scope because the driver updates it.
bool lcdLamp = true;

// ============================================================================
// WIRED LINK (WIRED_LINK == 1)
//   SoftwareSerial on D3 so the USB Serial Monitor stays available for
//   debugging. The transmitter sends one short line per keypress:
//       "5\n"  -> show giant 5, append 5 to the rolling ENTRY string
//       "clr\n"-> clear the entry
// Two wires only: TX D11 -> RX D3, and GND -> GND.
// ============================================================================
#if WIRED_LINK
#include <SoftwareSerial.h>
SoftwareSerial wireLink(WIRE_RX_PIN, WIRE_TX_PIN);   // RX, TX

// Forward declarations: the LCD application layer lives at the bottom of the
// file, so name these two before wirePoll() uses them.
void lcdShowPacket(const String &msg);
void lcdClearEntry();

char    wireBuf[24];
uint8_t wireLen = 0;

// Read whatever has arrived and act on each complete line.
void wirePoll() {
  while (wireLink.available()) {
    char c = (char)wireLink.read();
    if (c == '\n' || c == '\r') {
      if (wireLen == 0) continue;
      wireBuf[wireLen] = '\0';

      if (strncmp(wireBuf, "clr", 3) == 0) {
        lcdClearEntry();
        Serial.println(F("[RX] entry cleared (wired 'clr')."));
      } else {
        // Show it exactly like an optical keypad packet: giant char + entry.
        String msg = String("#") + wireBuf;
        lcdShowPacket(msg);
        Serial.print(F("[RX] wired key '"));
        Serial.print(wireBuf);
        Serial.println(F("'"));
      }
      wireLen = 0;
      continue;
    }
    if (wireLen < sizeof(wireBuf) - 1) wireBuf[wireLen++] = c;
  }
}
#endif // WIRED_LINK

// ============================================================================
// I2C LCD LOW-LEVEL DRIVER - self-contained, no external library
//   HD44780 16x2 behind a PCF8574 "backpack", driven directly over I2C.
//   Written in-house on purpose:
//     1) the expander PIN MAPPING can be switched at runtime ('M' command).
//        Several "I2C LCD" modules wire RS/EN/backlight to different Px bits,
//        and a wrong mapping is the classic cause of "backlight on, one black
//        box, no text" - which no amount of contrast turning will fix.
//     2) nothing to resolve/fail at build time; the build stays dependency-free.
// ============================================================================
#define LCD_CMD_CLEAR    0x01
#define LCD_CMD_HOME     0x02
#define LCD_CMD_ENTRY    0x06
#define LCD_CMD_DISP_ON  0x0C
#define LCD_CMD_FUNC_2L  0x28
#define LCD_CMD_DDRAM    0x80
#define LCD_CMD_CGRAM    0x40

// Preset 0 = by far the most common (YWRobot / "LCD1602 I2C"):
//   P0=RS  P1=RW  P2=EN  P3=backlight  P4..P7 = D4..D7
// Preset 1 = same pins, backlight pin is active LOW
// Preset 2 = RS on P1, EN on P0 (seen on some clones)
struct LcdMap { uint8_t rsBit, enBit, blBit; bool blHigh; };
static const LcdMap LCD_MAPS[] = {
  { 0, 2, 3, true  },
  { 0, 2, 3, false },
  { 1, 0, 3, true  },
};
#define LCD_MAP_N ((uint8_t)(sizeof(LCD_MAPS) / sizeof(LCD_MAPS[0])))

class LcdI2C {
public:
  uint8_t addr;
  uint8_t mapIdx;

  void begin(uint8_t a) { addr = a; mapIdx = 0; }
  void setMap(uint8_t i) { mapIdx = (uint8_t)(i % LCD_MAP_N); }
  const LcdMap &map() const { return LCD_MAPS[mapIdx]; }

  bool probe() {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
  }

  // HD44780 power-on sequence (datasheet p.24). ~250 ms total.
  void init() {
    backlight();
    delay(50);
    expWrite(0);                            // RS=RW=EN=data all low
    delay(100);
    pulse(0x30); delayMicroseconds(4500);   // 8-bit -> 4-bit handshake
    pulse(0x30); delayMicroseconds(4500);
    pulse(0x30); delayMicroseconds(150);
    pulse(0x20); delayMicroseconds(100);    // 4-bit mode from here
    send(LCD_CMD_FUNC_2L, false);           // 4-bit, 2 lines, 5x8 font
    delay(5);
    send(LCD_CMD_DISP_ON, false);           // display on, cursor off, no blink
    send(LCD_CMD_ENTRY, false);             // increment, no shift
    send(LCD_CMD_CLEAR, false);             // blank it
    delay(5);
  }

  void clear() { send(LCD_CMD_CLEAR, false); delay(3); setCursor(0, 0); }
  void home()  { send(LCD_CMD_HOME,  false); delay(3); }

  void setCursor(uint8_t col, uint8_t row) {
    static const uint8_t off[] = { 0x00, 0x40, 0x14, 0x54 };
    if (row >= LCD_ROWS) row = (uint8_t)(LCD_ROWS - 1);
    if (col >= LCD_COLS) col = (uint8_t)(LCD_COLS - 1);
    send((uint8_t)(LCD_CMD_DDRAM | (col + off[row])), false);
    delayMicroseconds(300);
  }

  void createChar(uint8_t loc, uint8_t mapBytes[8]) {
    loc &= 0x07;
    send((uint8_t)(LCD_CMD_CGRAM | (loc << 3)), false);
    for (uint8_t i = 0; i < 8; i++) send(mapBytes[i], true);
    send(LCD_CMD_DDRAM, false);
    delayMicroseconds(300);
  }

  void backlight()   { lcdLamp = true;  expWrite(map().blHigh ? (uint8_t)(1 << map().blBit) : 0); delay(2); }
  void noBacklight() { lcdLamp = false; expWrite(map().blHigh ? 0 : (uint8_t)(1 << map().blBit)); delay(2); }

  void write(uint8_t c) { send(c, true); }

  void print(const char *s) {
    while (*s) write((uint8_t)*s++);
  }

  void print(const __FlashStringHelper *f) {
    const char *p = (const char *)f;
    char buf[LCD_COLS + 1];
    uint8_t i = 0;
    while (i < LCD_COLS) {                  // never spill past the row
      buf[i] = (char)pgm_read_byte(p + i);
      if (!buf[i]) break;
      i++;
    }
    buf[i] = '\0';
    print(buf);
  }

  void print(long v) {
    char buf[12];
    uint8_t i = 0;
    bool neg = (v < 0);
    unsigned long u = neg ? (unsigned long)(-v) : (unsigned long)v;
    if (u == 0) buf[i++] = '0';
    while (u) { buf[i++] = (char)('0' + (u % 10)); u /= 10; }
    if (neg) buf[i++] = '-';
    while (i) write((uint8_t)buf[--i]);
  }

  void print(double v, int digits) {
    static const long p10[] = { 1, 10, 100 };
    if (digits < 0) digits = 0;
    if (digits > 2) digits = 2;
    bool neg = (v < 0);
    if (neg) v = -v;
    long scaled = (long)(v * (double)p10[digits] + 0.5);
    if (neg && scaled) write('-');
    print(scaled / p10[digits]);
    if (digits) { write('.'); print(scaled % p10[digits]); }
  }

private:
  void expWrite(uint8_t data) {             // P0..P7 as-is, backlight forced
    const LcdMap &m = map();
    uint8_t d = (uint8_t)(data & (uint8_t)~(1 << m.blBit));
    if (m.blHigh) d |= (uint8_t)(1 << m.blBit);
    Wire.beginTransmission(addr);
    Wire.write(d);
    Wire.endTransmission();
  }
  void pulse(uint8_t nibbleRs) {            // EN strobe; nibble already in P4..P7
    const LcdMap &m = map();
    expWrite((uint8_t)(nibbleRs | (1 << m.enBit)));
    delayMicroseconds(1);
    expWrite(nibbleRs);
    delayMicroseconds(50);
  }
  void send(uint8_t v, bool isData) {
    uint8_t rs = isData ? (uint8_t)(1 << map().rsBit) : 0;
    pulse((uint8_t)((v & 0xF0) | rs));
    pulse((uint8_t)(((v << 4) & 0xF0) | rs));
  }
};

// ---------------- Pins (D10 audio, D2/D9 FREE, NO BUTTON) ----------------
#define SENSOR_PIN   A0   // LDR: leg1->A0, leg2->GND (NO resistor, NO 5V)
#define AUDIO_OUT    10   // PWM audio to PAM8403 (Timer1 OC1B). D9 FREE.
#define STATUS_LED   13
#define VOLUME_POT   A1   // optional pot (not used, NO BUTTON build)
// D2 FREE (no button). D9 FREE. Control via Serial only.

// No-resistor LDR mode: LDR between A0 and GND, use internal pull-up.
// BUGFIX: was `1023 - readSensor()` (infinite recursion -> crash/reboot loop).
// Correct: 1023 - analogRead(SENSOR_PIN) so bright = HIGH.
#define NO_RESISTOR_LDR 1
inline int readSensor() {
#if NO_RESISTOR_LDR
  return 1023 - analogRead(SENSOR_PIN); // un-invert: bright = HIGH
#else
  return analogRead(SENSOR_PIN);
#endif
}
// Safe bit delays: delayMicroseconds() inaccurate above 16383us on AVR.
inline void bitDelay()     { delay(40); }
inline void bitDelayHalf() { delay(20); }

// ---------------- Modes (AUTO: boots in DATA, listens forever) ----------------
enum RxMode : uint8_t { MODE_AUDIO = 0, MODE_DATA = 1 };
RxMode rxMode = MODE_DATA;

// ---------------- Data protocol (LDR slow mode) ----------------
#define BIT_PERIOD_US  40000UL  // must match transmitter (40000 for LDR)
#define PREAMBLE_1     0xAA
#define PREAMBLE_2     0xAA
#define SYNC_BYTE      0x55
#define MAX_PAYLOAD    32

// ---------------- Audio (tuned for PAM8403 Class-D) ----------------
#define USE_VOLUME_POT  0        // set 1 if 10k pot wiper connected to A1
#define RX_GAIN         3.0f     // PAM8403 has 24dB fixed gain, very sensitive.
                                 // 2.0-4.0 ideal. 4.0+ clips/distorts. Use +/- keys to trim live.
#define RX_CENTER       128
#define RX_MAX_DEV      90       // output limiter: CENTER +/- 90 (~1.75Vpp into PAM8403).
                                 // Prevents harsh clipping. 70=quiet clean, 110=loud harsh.

// ---------------- Threshold / calibration ----------------
// For NO_RESISTOR_LDR (A0-GND + INPUT_PULLUP) measured: ON ~984, OFF ~873 => threshold ~928
// For 10k divider (old): ON ~800 OFF ~200
#if NO_RESISTOR_LDR
int lightHigh = 985;
int lightLow  = 875;
int threshold = 930;
#else
int lightHigh = 800;
int lightLow  = 200;
int threshold = 512;
#endif

// ---------------- LCD screen state (deferred model) ----------------
// These only record WHAT to show. Nothing here touches the I2C bus: rendering
// happens only in lcdService(), never in the bit-timing receive path.
// Custom glyph slots. Icons live in the TOP slots so the big-digit slots
// (0..5) can be reloaded on the keypad screen without destroying them.
#define ICON_SUN 6   // custom glyph 6: light / sun
#define ICON_SPK 7   // custom glyph 7: speaker
#define BIG_SLOTS 6  // slots 0..5 -> three 2-row-tall digits

enum LcdScreen : uint8_t { SCR_LISTEN, SCR_MSG, SCR_CAL, SCR_AUDIO, SCR_DEBUG, SCR_HUNT, SCR_KEY };

LcdI2C     lcd;                    // one static instance, no heap
bool       lcdReady = false;       // a working display was found and initialised
uint8_t    lcdAddr = LCD_I2C_ADDR;
LcdScreen  lcdScreen = SCR_LISTEN;
char       lcdMsg[MAX_PAYLOAD + 1] = { 0 };
uint8_t    lcdMsgLen = 0;
uint8_t    lcdPktCount = 0;
uint8_t    lcdLevel = 0;           // 0..8 blocks for the AUDIO meter
// Keypad entry: the last few digits typed on the transmitter keypad.
char       lcdEntry[9] = { 0 };    // rolling digits, e.g. "1234"
uint8_t    lcdEntryLen = 0;
char       lcdLastKey = '-';       // most recent key received
int        lcdCalMn = 0, lcdCalMx = 0;
unsigned long lcdCalMs = 0;
int        lcdDbgVar = 0, lcdDbgHi = 0, lcdDbgLo = 0;
bool       lcdIcons = LCD_USE_ICONS;
bool       lcdDirty = true;
unsigned long lcdLastDraw = 0;
// Non-blocking contrast hunt: a 15 s BLOCKING wait at boot swallowed serial
// commands and made the receiver look dead. It now runs from loop().
bool         lcdHunting = false;
unsigned long lcdHuntT0 = 0;

// ---------------- Prototypes ----------------
void setupTimer1FastPWM();
inline void audioPWM(uint8_t v) { OCR1B = v; }
void setMode(RxMode m);
void toggleMode();
void pollSerial();
void debugStream(); // serial debugger: streams raw/light/threshold 5s
void audioLoop();
void dataLoop();
void calibrateAmbient();
void fadeToCenter();   // pop-free mute for PAM8403 (ramps PWM to CENTER=silence)
void recalibrateFromPreamble(int mn, int mx);
bool waitStartBit(unsigned long timeoutMs);
uint8_t receiveByteOOK(bool &ok);
bool receivePacket(String &out);
void ackBeep();
#if WIRED_LINK
void wirePoll();      // defined near the top
#endif
// LCD API - no-ops when LCD_ENABLED is 0
void lcdService(bool force = false);
void lcdSetLevel(uint8_t level);
void lcdInit();
void lcdClear();
void lcdSetLamp(bool on);
void lcdTestPattern();
void lcdHunt();
void lcdHuntTick();
void lcdPrintMapInfo();
void lcdNextMap();
void lcdBuildIcons();
void lcdRefreshCurrent();
void lcdShowListening();
void lcdShowMessage(const String &msg);
void lcdShowPacket(const String &msg);
void lcdHoldMessage();
void lcdClearEntry();
void lcdShowKeypadIdle();
void lcdDrawBigChar(char c, uint8_t col, uint8_t row);
void lcdShowCalibrating(unsigned long elapsedMs, int mn, int mx);
void lcdShowAudio(uint8_t level);
void lcdShowDebug(int mn, int mx, int var);

// Audio baseline (ambient light tracker)
float audioBaseline = 512.0f;
unsigned long lastVolRead = 0;
float currentGain = RX_GAIN;

void setup() {
  pinMode(2, INPUT); // D2 FREE (no button in this build)
  pinMode(STATUS_LED, OUTPUT);
  pinMode(AUDIO_OUT, OUTPUT);
  pinMode(9, INPUT); // D9 completely free / high-Z on RX board
#if NO_RESISTOR_LDR
  pinMode(SENSOR_PIN, INPUT_PULLUP); // internal ~34k replaces 10k resistor
#endif

  Serial.begin(9600);
  while (!Serial) { ; }

  lcdInit();      // I2C + probe + splash + contrast hunt (also reports on Serial)

  setupTimer1FastPWM();
  audioPWM(RX_CENTER);
  delay(500); // let PAM8403 power settle (avoids turn-on pop), let DC-block cap charge

  // STARTUP BEEP (standalone proof: speaker works on power-up, no PC needed)
  tone(AUDIO_OUT, 1000, 250); delay(350);
  tone(AUDIO_OUT, 1500, 250); delay(350);
  noTone(AUDIO_OUT);
  setupTimer1FastPWM(); audioPWM(RX_CENTER); // tone() disturbs Timer1 PWM, restore it

  Serial.println(F("\n=============================="));
  Serial.println(F(" LiFi RECEIVER Ready (PAM8403, NO BUTTON)"));
  Serial.println(F("=============================="));
  Serial.println(F("Commands: A=Audio D=Data M=Toggle C=Calibrate ?=Debug +/-=Volume"));
  Serial.println(F("LCD     : L=Backlight R=Re-init X=Clear T=Test N=Map I=Icons"));
  Serial.println(F("           E=Clear keypad entry   G=Keypad screen demo"));

#if WIRED_LINK
  Serial.println(F("LINK    : WIRED - RX on D3 from TX D11, GND to GND."));
  Serial.println(F("Keypad  : press a key -> giant char + rolling ENTRY on LCD."));
  lcdShowKeypadIdle();
  Serial.println(F("------------------------------"));
#else
  Serial.println(F("Keypad  : transmitter '#<key>' -> giant char + rolling ENTRY on LCD."));
  Serial.println(F("LDR: A0-GND only. PAM8403 D10. 20-30cm, shade, indoors."));

  calibrateAmbient();
  setMode(MODE_DATA); // AUTO: boot straight into listen loop
  Serial.println(F("------------------------------"));
#endif
}

void loop() {
  pollSerial();
  lcdHuntTick();   // non-blocking: never delays serial/audio/data

#if WIRED_LINK
  // Simple mode: only the wired link, no light/LDR/bit-timing involved.
  wirePoll();
#else
  if (rxMode == MODE_AUDIO) {
    audioLoop();
  } else {
    dataLoop();
  }
#endif

  lcdService(); // SAFE POINT: never inside the 40ms/bit receive path
}

void setupTimer1FastPWM() {
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;
  TCCR1A = _BV(COM1B1) | _BV(WGM10); // OC1B (D10) fast PWM 8-bit
  TCCR1B = _BV(WGM12) | _BV(CS10);
  OCR1B = RX_CENTER;
  pinMode(9, INPUT); // keep D9 free
}

void setMode(RxMode m) {
  // Always fade first -> no thump in PAM8403 speaker
  fadeToCenter();
  rxMode = m;
  if (rxMode == MODE_AUDIO) {
    digitalWrite(STATUS_LED, HIGH);
    // Re-seed baseline to current ambient to avoid pop
    long sum = 0;
    for (int i = 0; i < 32; i++) sum += readSensor();
    audioBaseline = sum / 32.0f;
    lcdShowAudio(0);
    Serial.println(F("[RX] MODE = AUDIO. Output D10 -> 10k/10nF/10uF -> PAM8403 R-IN -> speaker."));
    Serial.print(F("[RX] Gain=")); Serial.print(currentGain);
    Serial.println(F(" (+/- to trim, PAM8403 knob at middle)."));
  } else {
    digitalWrite(STATUS_LED, LOW);
    // CENTER = silence (0V AC after 10uF DC-block). Hold here in DATA mode.
    audioPWM(RX_CENTER);
    lcdShowListening();
    Serial.println(F("[RX] MODE = DATA. Amp muted (CENTER). Waiting for light packets... (TX must be in DATA too)"));
    Serial.print(F("[RX] Threshold=")); Serial.print(threshold);
    Serial.print(F(" (High=")); Serial.print(lightHigh);
    Serial.print(F(" Low=")); Serial.print(lightLow);
    Serial.println(F("). Send 'C' to recalibrate if garbage."));
  }
}

void toggleMode() {
  setMode(rxMode == MODE_AUDIO ? MODE_DATA : MODE_AUDIO);
}

// Ramp current PWM output to CENTER step-by-step (~30ms).
// Prevents loud POP in PAM8403 when switching modes (DC-block cap friendly).
void fadeToCenter() {
  int cur = OCR1B;
  while (cur != RX_CENTER) {
    if (cur < RX_CENTER) cur++;
    else cur--;
    audioPWM((uint8_t)cur);
    delayMicroseconds(800);
  }
}

void pollSerial() {
  // SERIAL ONLY (NO BUTTON). Single-char commands, rest of line flushed.
  if (Serial.available()) {
    char c = (char)Serial.read();
    // flush rest of line if user typed line
    while (Serial.available() && Serial.peek() != '\n') Serial.read();
    if (Serial.available()) Serial.read(); // consume \n
    char up = toupper(c);
    if (up == 'A') setMode(MODE_AUDIO);
    else if (up == 'D') setMode(MODE_DATA);
    else if (up == 'M') { /* fallback for old builds: toggle mode */ toggleMode(); }
    else if (up == 'C') { calibrateAmbient(); if (rxMode == MODE_DATA) setMode(MODE_DATA); }
    else if (c == '?') debugStream();
    else if (c == '+' || c == '=') {
      currentGain += 0.5f; if (currentGain > 8.0f) currentGain = 8.0f;
      Serial.print(F("[RX] Gain=")); Serial.println(currentGain);
      lcdRefreshCurrent();
    }
    else if (c == '-' || c == '_') {
      currentGain -= 0.5f; if (currentGain < 0.5f) currentGain = 0.5f;
      Serial.print(F("[RX] Gain=")); Serial.println(currentGain);
      lcdRefreshCurrent();
    }
    // --- LCD commands ---
    else if (c == 'L' || c == 'l') {
      if (lcdLamp) lcd.noBacklight(); else lcd.backlight();
      Serial.print(F("[LCD] backlight=")); Serial.println(lcdLamp ? F("ON") : F("OFF"));
    }
    else if (c == 'R' || c == 'r') {
      lcdInit();
      lcdRefreshCurrent();
    }
    else if (c == 'X' || c == 'x') {
      lcdClear();
      Serial.println(F("[LCD] cleared."));
    }
    else if (c == 'T' || c == 't') {
      // NOTE: each line is followed by a small delay. At 9600 baud the 64-byte
      // UART buffer overruns if several println()s are issued back-to-back,
      // which garbles the monitor text (seen as overlapping/duplicated lines).
      Serial.println(F("[LCD] TEST: 'LCD CONTRAST TEST' + growing bar, 15 s."));
      delay(60);
      Serial.println(F("[LCD] Turn the BLUE POT on the BACK of the module NOW."));
      delay(60);
      Serial.println(F("[LCD] Blank = pot too low. Solid black = pot too high."));
      delay(60);
      Serial.println(F("[LCD] If nothing at ANY pot position, press 'N' (pin map)."));
      delay(60);
      lcdHunt();                 // non-blocking: serial stays alive
    }
    else if (c == 'N' || c == 'n') {      // 'M' is taken by mode toggle above
      lcdNextMap();
      lcdPrintMapInfo();
    }
    else if (c == 'I' || c == 'i') {
      lcdIcons = !lcdIcons;
      Serial.print(F("[LCD] icons=")); Serial.println(lcdIcons ? F("ON") : F("OFF"));
      lcdBuildIcons();
      lcdRefreshCurrent();
    }
    else if (c == 'E' || c == 'e') {
      lcdClearEntry();
      Serial.println(F("[LCD] keypad entry cleared."));
    }
    else if (c == 'G' || c == 'g') {      // show the keypad screen immediately
      lcdScreen = SCR_KEY;
      lcdLastKey = '7';
      lcdEntryLen = 0;
      lcdEntry[0] = '\0';
      lcdDirty = true;
      lcdService(true);
      Serial.println(F("[LCD] keypad screen demo (giant '7')."));
    }
  }
}

// Serial debugger: streams raw/light/threshold + BRIGHT/DARK for 5s.
// Use: cover LDR -> DARK, shine TX LED 20-30cm -> BRIGHT. Variation must be >40.
void debugStream() {
  Serial.println(F("[DBG] 5s stream: raw light threshold state (cover/uncover LDR, move LED)"));
  unsigned long t0 = millis();
  int mn = 1023, mx = 0;
  while (millis() - t0 < 5000) {
    int raw = analogRead(SENSOR_PIN);
    int light = readSensor();
    if (light < mn) mn = light;
    if (light > mx) mx = light;
    Serial.print(F("raw=")); Serial.print(raw);
    Serial.print(F(" light=")); Serial.print(light);
    Serial.print(F(" thr=")); Serial.print(threshold);
    Serial.println(light > threshold ? F(" BRIGHT") : F(" DARK"));
    lcdService();
    delay(200);
  }
  Serial.print(F("[DBG] min=")); Serial.print(mn);
  Serial.print(F(" max=")); Serial.print(mx);
  Serial.print(F(" variation=")); Serial.println(mx - mn);
  if (mx - mn < 40) Serial.println(F("[DBG] FAIL: variation <40. Check A0-GND wiring, 20-30cm distance, shade tube."));
  else Serial.println(F("[DBG] OK: LDR sees light changes."));
  lcdShowDebug(mn, mx, mx - mn); // verdict stays on the LCD
}

// ---------------- AUDIO MODE (PAM8403) ----------------
// Sample sensor fast, subtract slow ambient baseline, amplify to PWM.
// Output is limited to CENTER +/- RX_MAX_DEV so PAM8403 line-in never hard-clips.
void audioLoop() {
  const int BATCH = 400;

#if USE_VOLUME_POT
  if (millis() - lastVolRead > 200) {
    lastVolRead = millis();
    int v = analogRead(VOLUME_POT); // 0..1023
    currentGain = 1.0f + (v / 1023.0f) * 7.0f; // 1..8
  }
#endif

  for (int i = 0; i < BATCH; i++) {
    int raw = readSensor();

    // Slow baseline follows ambient drift, fast audio rides on top
    audioBaseline += 0.002f * ((float)raw - audioBaseline);

    float ac = ((float)raw - audioBaseline) * currentGain;
    // Limiter for PAM8403: soft-clip to +/- RX_MAX_DEV
    if (ac > RX_MAX_DEV) ac = RX_MAX_DEV;
    if (ac < -RX_MAX_DEV) ac = -RX_MAX_DEV;
    int out = (int)(RX_CENTER + ac);
    if (out < 0) out = 0;
    if (out > 255) out = 255;
    audioPWM((uint8_t)out);

    // LCD level meter source (drawn later by lcdService(), throttled)
    int mag = (int)((ac < 0) ? -ac : ac);
    uint8_t lvl = (uint8_t)((mag * 8) / RX_MAX_DEV);
    if (lvl > 8) lvl = 8;
    lcdSetLevel(lvl);
  }
  // return to loop() to service serial + LCD
}

// ---------------- DATA MODE (AUTO LISTEN LOOP) ----------------
// Once uploaded: listens forever, prints Got: per message, shows it on the
// LCD, and plays a beep on the speaker automatically (no phone/PC needed).
void ackBeep() {
  noTone(AUDIO_OUT);
  tone(AUDIO_OUT, 1200, 150); delay(200);
  tone(AUDIO_OUT, 1600, 150); delay(220);
  noTone(AUDIO_OUT);
  setupTimer1FastPWM(); audioPWM(RX_CENTER); // restore LiFi PWM after tone()
}
void dataLoop() {
  String msg;
  if (receivePacket(msg)) {
    digitalWrite(STATUS_LED, HIGH);
    Serial.print(F("[RX] Got: "));
    Serial.println(msg);
    digitalWrite(STATUS_LED, LOW);
    lcdShowPacket(msg);   // keypad press -> giant char, else -> text on both rows
    ackBeep();            // speaker confirms automatically
    lcdHoldMessage();     // keep it on screen, service serial, then status
  }
  // receivePacket is blocking (slow LDR: ~0.4s/byte, packet ~4s),
  // so serial is checked between packets in loop().
}

// Measure ambient: assumes TX is sending preamble/idle toggling,
// or at least TX LED is ON. We sample 2 sec min/max.
void calibrateAmbient() {
  Serial.println(F("[RX] Calibrating... keep TX LED facing sensor, TX in DATA mode."));
  int mn = 1023, mx = 0;
  unsigned long t0 = millis();
  unsigned long lastLcd = 0;
  while (millis() - t0 < 2000) {
    int v = readSensor();
    if (v < mn) mn = v;
    if (v > mx) mx = v;
    delay(5);
    // blink while calibrating
    digitalWrite(STATUS_LED, (millis() / 200) % 2);
    // live progress on the LCD (safe here: no bit timing in this window)
    if (millis() - lastLcd >= 120) {
      lastLcd = millis();
      lcdShowCalibrating(millis() - t0, mn, mx);
    }
  }
  digitalWrite(STATUS_LED, LOW);

  // If no variation seen (TX steady), synthesize range around mean
  if (mx - mn < 40) {
    int mean = (mx + mn) / 2;
    mx = mean + 60; if (mx > 1023) mx = 1023;
    mn = mean - 60; if (mn < 0) mn = 0;
    Serial.println(F("[RX] Low variation - using estimated range. For best results, ensure TX is ON and sending."));
  }
  lightHigh = mx;
  lightLow = mn;
  threshold = (mx + mn) / 2;

  Serial.print(F("[RX] Calibrated. High=")); Serial.print(lightHigh);
  Serial.print(F(" Low=")); Serial.print(lightLow);
  Serial.print(F(" Threshold=")); Serial.println(threshold);
}

// Refine threshold from preamble min/max observed
void recalibrateFromPreamble(int mn, int mx) {
  if (mx - mn < 20) return; // ignore noise
  lightHigh = (lightHigh * 3 + mx) / 4; // slow adapt
  lightLow  = (lightLow * 3 + mn) / 4;
  threshold = (lightHigh + lightLow) / 2;
}

inline bool lightIsHigh() {
  return readSensor() > threshold;
}

// Wait for falling edge (ON -> OFF = start bit) with timeout
bool waitStartBit(unsigned long timeoutMs) {
  unsigned long t0 = millis();
  // First ensure we are in HIGH (idle) - wait for HIGH briefly
  while (millis() - t0 < timeoutMs) {
    if (readSensor() > threshold) break;
  }
  // Now wait for falling edge
  while (millis() - t0 < timeoutMs) {
    if (readSensor() <= threshold) return true;
  }
  return false;
}

// Receive one OOK byte. Caller must have already detected start edge.
// Returns byte, ok=false on stop-bit error.
// Uses safe bitDelay() (delayMicroseconds overflows above 16383us on AVR).
// NOTE: no LCD/I2C traffic allowed in here - it would break the 40ms bit grid.
uint8_t receiveByteOOK(bool &ok) {
  // We enter right at falling edge (start of START bit).
  // Wait 1.5 bit periods to land mid of bit0, then sample.
  bitDelay(); bitDelayHalf();

  uint8_t b = 0;
  for (uint8_t i = 0; i < 8; i++) {
    int v = readSensor();
    if (v > threshold) b |= (1 << i);
    if (i < 7) bitDelay();
  }
  // Move to middle of STOP bit and verify HIGH
  bitDelay();
  int stopV = readSensor();
  ok = (stopV > threshold - 15); // small hysteresis for noise
  // inter-byte gap remainder
  bitDelayHalf();
  return b;
}

bool receivePacket(String &out) {
  // Hunt for preamble AA AA 55. LDR slow: 1 byte = 0.4s, preamble = 1.2s.
  // Hunt window 15s so preamble is not missed.
  unsigned long huntEnd = millis() + 15000;
  uint8_t b1 = 0, b2 = 0, b3 = 0;
  bool ok = false;
  int preMin = 1023, preMax = 0;

  // --- Sync hunt ---
  while (millis() < huntEnd) {
    if (!waitStartBit(500)) {
      // allow loop() to service serial on next dataLoop iteration
      if (Serial.available()) return false;
      continue;
    }
    uint8_t b = receiveByteOOK(ok);
    if (!ok) continue;

    // track preamble amplitude for auto-threshold
    int v = readSensor();
    if (v < preMin) preMin = v;
    if (v > preMax) preMax = v;

    b1 = b2; b2 = b3; b3 = b;
    if (b1 == PREAMBLE_1 && b2 == PREAMBLE_2 && b3 == SYNC_BYTE) break;
    // not synced yet, keep hunting
    if (millis() >= huntEnd) return false;
  }
  if (!(b1 == PREAMBLE_1 && b2 == PREAMBLE_2 && b3 == SYNC_BYTE)) return false;

  recalibrateFromPreamble(preMin, preMax);

  // --- LEN ---
  if (!waitStartBit(3000)) return false;
  uint8_t len = receiveByteOOK(ok);
  if (!ok) return false;
  if (len == 0 || len > MAX_PAYLOAD) return false;

  // --- PAYLOAD ---
  char buf[MAX_PAYLOAD + 1];
  uint8_t chk = len;
  for (uint8_t i = 0; i < len; i++) {
    if (!waitStartBit(3000)) return false;
    uint8_t d = receiveByteOOK(ok);
    if (!ok) return false;
    buf[i] = (char)d;
    chk ^= d;
  }
  buf[len] = '\0';

  // --- CHECKSUM ---
  if (!waitStartBit(3000)) return false;
  uint8_t rxChk = receiveByteOOK(ok);
  if (!ok) return false;

  // --- END marker 0x0A ---
  if (!waitStartBit(3000)) return false;
  uint8_t endB = receiveByteOOK(ok);
  if (!ok) return false;

  if (rxChk != chk) {
    Serial.println(F("[RX] Checksum error - try closer alignment / shade sensor / recalibrate (C)."));
    return false;
  }
  if (endB != '\n') {
    // tolerate but warn
  }

  out = String(buf);
  return true;
}

// ============================================================================
// I2C LCD APPLICATION LAYER
//   Probe/init + a deferred screen model. Only lcdService() renders.
// ============================================================================

// 3x5 pixel font for the digits and a few symbols, drawn as GIANT characters
// that are two LCD rows tall. Each glyph is 5 columns x 7 rows, stored as
// 7 bytes with bit 0 = LEFTMOST pixel (HD44780 CGRAM bit order).
#define GLYPH_W 5
#define GLYPH_H 7

// '0'..'9' then '-', '*', '#', ' ' (blank) -> 14 glyphs
static const uint8_t GLYPH_3x5[][GLYPH_H] = {
  { 0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110 }, // 0
  { 0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110 }, // 1
  { 0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111 }, // 2
  { 0b11111, 0b00010, 0b00100, 0b00010, 0b00001, 0b10001, 0b01110 }, // 3
  { 0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010 }, // 4
  { 0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110 }, // 5
  { 0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110 }, // 6
  { 0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000 }, // 7
  { 0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110 }, // 8
  { 0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100 }, // 9
  { 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000 }, // ' ' blank
  { 0b00000, 0b00000, 0b01110, 0b01110, 0b01110, 0b00000, 0b00000 }, // '-'
  { 0b00000, 0b10101, 0b01110, 0b11111, 0b01110, 0b10101, 0b00000 }, // '*'
  { 0b01010, 0b01010, 0b11111, 0b01010, 0b11111, 0b01010, 0b01010 }, // '#'
  { 0b01110, 0b00001, 0b00110, 0b00000, 0b00110, 0b10000, 0b01110 }, // '?' fallback
};
#define GLYPH_BLANK 10
#define GLYPH_COUNT 14

// Map a character to its glyph index. Anything unknown becomes '?'.
static uint8_t glyphIndex(char c) {
  if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
  if (c == ' ')  return GLYPH_BLANK;
  if (c == '-' || c == '_') return 11;
  if (c == '*')  return 12;
  if (c == '#')  return 13;
  return GLYPH_COUNT - 1;
}

// Draw a GIANT character: 2 cells wide x 2 rows tall.
//
// The HD44780 CGRAM is only 5x8 PER CELL, so a 5x7 glyph cannot be blown up
// inside one cell. It is drawn as four cells instead: left-top, right-top,
// left-bottom, right-bottom. Source columns are shared at the seam
// (left = cols 0,1,2 | right = cols 2,3,4) and every pixel is doubled, so the
// result is a crisp 10x14 pixel character.
void lcdDrawBigChar(char c, uint8_t col, uint8_t row) {
  uint8_t gi = glyphIndex(c);
  uint8_t slot = (uint8_t)(col * 4);            // 4 slots per giant char
  if (slot + 3 >= BIG_SLOTS) return;            // would clobber the icon slots

  for (uint8_t half = 0; half < 2; half++) {     // 0 = left half, 1 = right half
    uint8_t map[8];
    for (uint8_t r = 0; r < 8; r++) {
      uint8_t bits = GLYPH_3x5[gi][r < 4 ? r : (uint8_t)(r - 4)];
      uint8_t out = 0;
      for (uint8_t b = 0; b < 3; b++) {
        uint8_t srcCol = (uint8_t)(half * 2 + b);   // left 0,1,2 | right 2,3,4
        if (srcCol < GLYPH_W && (bits & (1 << srcCol))) {
          out |= (uint8_t)((1 << (b * 2)) | (1 << (b * 2 + 1)));
        }
      }
      map[r] = out;
    }
    lcd.createChar((uint8_t)(slot + half), map);       // top row pair
    lcd.createChar((uint8_t)(slot + 2 + half), map);   // bottom row pair
  }

  lcd.setCursor(col, row);
  lcd.write((uint8_t)slot);
  lcd.write((uint8_t)(slot + 1));
  lcd.setCursor(col, (uint8_t)(row + 1));
  lcd.write((uint8_t)(slot + 2));
  lcd.write((uint8_t)(slot + 3));
}

// Scan the PCF8574 address window (0x20..0x3F). Returns 0 if nothing answers.
static uint8_t lcdFindDevice() {
  for (uint8_t a = 0x20; a <= 0x3F; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) return a;
  }
  return 0;
}

void lcdBuildIcons() {
  if (!lcdReady) return;
  uint8_t sun[8] = { 0x00, 0x04, 0x0A, 0x1F, 0x0A, 0x04, 0x00, 0x00 };
  uint8_t spk[8] = { 0x00, 0x02, 0x06, 0x1F, 0x1F, 0x1F, 0x06, 0x02 };
  lcd.createChar(ICON_SUN, sun);
  lcd.createChar(ICON_SPK, spk);
}

void lcdInit() {
  bool firstTime = !lcdReady;      // only hunt for contrast on a cold start
  Wire.begin();
  Wire.setClock(LCD_I2C_SPEED);
  delay(50); // let the LCD rail settle

  uint8_t addr = 0;
  for (uint8_t tries = 0; tries < LCD_PROBE_TRIES && addr == 0; tries++) {
    if (tries) delay(LCD_PROBE_RETRY_MS);
    addr = lcdFindDevice();
  }

  if (addr == 0) {
    lcdReady = false;
    Serial.println(F("[LCD] No I2C display found - running without LCD."));
    Serial.println(F("[LCD] Check: VCC->5V, GND->GND, SDA->A4, SCL->A5, jumper ON."));
    return;
  }

  lcdAddr = addr;
  lcd.begin(addr);
  lcd.init();          // ~250 ms, plus the 100 ms rail settle above
  lcdBuildIcons();
  lcdReady = true;
  lcdDirty = true;

  Serial.print(F("[LCD] PCF8574 LCD found @ 0x"));
  Serial.print(addr, HEX);
  Serial.println(F(" - driven directly, no library."));
  delay(60);
  lcdPrintMapInfo();

  if (firstTime) {
    Serial.println(F("[LCD] 15 s contrast hunt starting - turn the BLUE POT now."));
    delay(60);
    Serial.println(F("[LCD] Blank = pot too low. Solid black = pot too high."));
    delay(60);
    lcdHunt();                // non-blocking: board stays fully responsive
  }
  lcdRefreshCurrent();
}

// ---- screen model: set state, request a redraw --------------------------
void lcdShowListening() {
  lcdScreen = SCR_LISTEN;
  lcdDirty = true;
  lcdService(true);
}

void lcdShowMessage(const String &msg) {
  uint8_t n = msg.length();
  if (n > MAX_PAYLOAD) n = MAX_PAYLOAD;
  for (uint8_t i = 0; i < n; i++) lcdMsg[i] = msg.charAt(i);
  lcdMsg[n] = '\0';
  lcdMsgLen = n;
  if (lcdPktCount < 255) lcdPktCount++;
  lcdScreen = SCR_MSG;
  lcdDirty = true;
  lcdService(true);
}

// Called for every packet that arrives from the transmitter.
//
// The transmitter keypad sends "#<key>" (KEY_PREFIX + one character) on each
// press, and beacon messages like "HI LIFI". We split the two cases:
//   - payload starts with '#' -> a keypad press: show the GIANT character and
//     append it to the rolling ENTRY string (digits build up a number).
//   - anything else -> normal message screen.
void lcdShowPacket(const String &msg) {
  char first = msg.length() ? msg.charAt(0) : '\0';
  if (first != '#' || msg.length() < 2) {
    lcdShowMessage(msg);            // beacon / typed text
    return;
  }

  char key = msg.charAt(1);         // the pressed key
  lcdLastKey = key;

  // Digits and . / - build a rolling entry; letters act as ENTER (commit).
  bool isDigit = (key >= '0' && key <= '9') || key == '.' || key == '-';
  if (isDigit) {
    if (lcdEntryLen < sizeof(lcdEntry) - 1) {
      lcdEntry[lcdEntryLen++] = key;
      lcdEntry[lcdEntryLen] = '\0';
    } else {
      // scroll: keep the newest characters
      for (uint8_t i = 1; i < sizeof(lcdEntry) - 1; i++) lcdEntry[i - 1] = lcdEntry[i];
      lcdEntry[sizeof(lcdEntry) - 2] = key;
      lcdEntry[sizeof(lcdEntry) - 1] = '\0';
      lcdEntryLen = sizeof(lcdEntry) - 1;
    }
  } else if (key == '*') {                 // '*' clears the entry
    lcdEntryLen = 0;
    lcdEntry[0] = '\0';
  }

  if (lcdPktCount < 255) lcdPktCount++;
  lcdScreen = SCR_KEY;
  lcdDirty = true;
  lcdService(true);       // immediate: a keypress must feel instant
}

// Hold received text on screen (services serial + LCD), then return to status.
void lcdHoldMessage() {
  unsigned long t0 = millis();
  while (millis() - t0 < LCD_MSG_HOLD_MS && rxMode == MODE_DATA) {
    pollSerial();
    lcdService();
    delay(10);
  }
  if (rxMode == MODE_DATA) lcdShowListening();
}

void lcdShowCalibrating(unsigned long elapsedMs, int mn, int mx) {
  lcdScreen = SCR_CAL;
  lcdCalMs = elapsedMs;
  lcdCalMn = mn;
  lcdCalMx = mx;
  lcdDirty = true;
  lcdService(true); // no bit timing in this window: an immediate draw is safe
}

// Immediate draw. NOTE: audioLoop never calls this - it only uses lcdSetLevel()
// so the level meter stays throttled and never disturbs the sample rate.
void lcdShowAudio(uint8_t level) {
  if (level > 8) level = 8;
  lcdScreen = SCR_AUDIO;
  lcdLevel = level;
  lcdDirty = true;
  lcdService(true);
}

// Hot path (called per audio sample): only flips a flag, no I2C.
void lcdSetLevel(uint8_t level) {
  if (level > 8) level = 8;
  if (level == lcdLevel) return;
  lcdScreen = SCR_AUDIO;
  lcdLevel = level;
  lcdDirty = true;
}

void lcdShowDebug(int mn, int mx, int var) {
  lcdDbgLo = mn; lcdDbgHi = mx; lcdDbgVar = var;
  lcdScreen = SCR_DEBUG;
  lcdDirty = true;
  lcdService(true);
}

// Redraw whatever the current state is (used by 'R' and gain changes).
void lcdRefreshCurrent() {
  if (!lcdReady) return;
  if (rxMode == MODE_AUDIO) { lcdShowAudio(lcdLevel); return; }
  switch (lcdScreen) {
    case SCR_MSG:   lcdDirty = true; lcdService(true); break;
    case SCR_CAL:   lcdDirty = true; lcdService(true); break;
    case SCR_DEBUG: lcdDirty = true; lcdService(true); break;
    case SCR_KEY:   lcdDirty = true; lcdService(true); break;
    default:        lcdShowListening(); break;
  }
}

// Welcome screen for the wired keypad demo: shows what to do next.
void lcdShowKeypadIdle() {
  lcdLastKey = '*';
  lcdEntryLen = 0;
  lcdEntry[0] = '\0';
  lcdScreen = SCR_KEY;
  lcdDirty = true;
  lcdService(true);
}

// Clear the keypad entry (serial command 'E' also clears the giant glyph).
void lcdClearEntry() {
  lcdEntryLen = 0;
  lcdEntry[0] = '\0';
  lcdLastKey = ' ';
  lcdScreen = SCR_KEY;
  lcdDirty = true;
  lcdService(true);
}

void lcdSetLamp(bool on) {
  if (on) lcd.backlight(); else lcd.noBacklight();
}

void lcdClear() {
  if (!lcdReady) return;
  lcd.clear();
  lcdLastDraw = millis();
  lcdDirty = false;
}

// Contrast / mapping self-test. Row 2 is INTENTIONALLY a solid bar.
// If this is unreadable the problem is the contrast pot (hardware), because
// this exact pattern is the widest-contrast test possible.
void lcdTestPattern() {
  if (!lcdReady) return;
  lcd.setCursor(0, 0);
  lcd.print(F("0123456789ABCDEF"));
  lcd.setCursor(0, 1);
  for (uint8_t i = 0; i < LCD_COLS; i++) lcd.write((uint8_t)0xFF);
  lcdLastDraw = millis();
  lcdDirty = false;
}

// Start the NON-BLOCKING contrast hunt: a wide-contrast pattern with a progress
// bar for LCD_HUNT_MS, so the pot can be turned until the text resolves.
// Used at cold boot and by the 'T' command. Returns immediately.
void lcdHunt() {
  if (!lcdReady) return;
  lcdScreen = SCR_HUNT;
  lcdHuntT0 = millis();
  lcdHunting = true;
  lcdDirty = true;
  lcdService(true);   // first frame immediately
}

// Service the hunt from loop(). Draws ~5 fps, keeps everything else running.
void lcdHuntTick() {
  if (!lcdHunting) return;
  unsigned long el = millis() - lcdHuntT0;
  if (el >= LCD_HUNT_MS) {
    lcdHunting = false;
    lcdRefreshCurrent();          // back to the normal screen
    return;
  }
  lcdDirty = true;
  lcdService(true);               // forced: this screen owns the display
}

// Cycle the PCF8574 pin-mapping preset (for modules that wire RS/EN/BL
// differently - a wrong mapping shows a black box that contrast cannot fix).
void lcdNextMap() {
  if (!lcdReady) return;
  lcd.setMap((uint8_t)(lcd.mapIdx + 1));
  lcd.init();
  lcdBuildIcons();
  lcdHunting = false;          // stop any hunt, this is an explicit override
  lcdTestPattern();            // widest-contrast pattern, stays until next screen
  lcdLastDraw = millis();
  lcdDirty = false;
}

// Cycle the PCF8574 pin-mapping preset (for modules that wire RS/EN/BL
// differently - a wrong mapping shows a black box that contrast cannot fix).
void lcdPrintMapInfo() {
  Serial.print(F("[LCD] pin map preset -> "));
  Serial.print((long)lcd.mapIdx);
  Serial.print(F("  (RS=P"));
  Serial.print((long)lcd.map().rsBit);
  Serial.print(F(" EN=P"));
  Serial.print((long)lcd.map().enBit);
  Serial.print(F(" BL=P"));
  Serial.print((long)lcd.map().blBit);
  Serial.println(lcd.map().blHigh ? F(" HIGH") : F(" LOW"));
  delay(60);
  Serial.println(F("[LCD] Black screen at every preset = module/wiring fault."));
  delay(60);
}

// ---- the ONLY function that renders on I2C ------------------------------
void lcdDraw() {
  switch (lcdScreen) {

    // <=16 chars: text on row0, "[OK] len pkt#n" on row1
    // >16 chars: message split across both rows (covers the full 32-char payload)
    case SCR_MSG: {
      lcd.setCursor(0, 0);
      for (uint8_t i = 0; i < LCD_COLS && i < lcdMsgLen; i++) lcd.write((uint8_t)lcdMsg[i]);
      lcd.setCursor(0, 1);
      if (lcdMsgLen <= LCD_COLS) {
        lcd.print(F("[OK] "));
        lcd.print((long)lcdMsgLen);
        lcd.print(F("ch pkt#"));
        lcd.print((long)lcdPktCount);
      } else {
        for (uint8_t i = LCD_COLS; i < lcdMsgLen && i < (uint8_t)(2 * LCD_COLS); i++)
          lcd.write((uint8_t)lcdMsg[i]);
      }
      break;
    }

    // Calibration progress
    case SCR_CAL: {
      static const char spin[4] = { '|', '/', '-', '\\' };
      uint8_t idx = (uint8_t)((lcdCalMs / 200) % 4);
      lcd.setCursor(0, 0);
      lcd.print(F("CALIBRATING "));
      lcd.write((uint8_t)spin[idx]);
      lcd.setCursor(0, 1);
      lcd.print(F("H:")); lcd.print((long)lcdCalMx);
      lcd.print(F(" L:")); lcd.print((long)lcdCalMn);
      break;
    }

    // KEYPAD screen: giant last key on the left, typed digits on the right
    case SCR_KEY: {
      lcd.setCursor(0, 0);
      lcd.print(F("KEY "));
      lcd.setCursor(6, 0);
      lcd.print(F("ENTRY:"));
      // right side of row 0: up to 9 typed characters
      lcd.setCursor(12, 0);
      for (uint8_t i = 0; i < 4 && i < lcdEntryLen; i++)
        lcd.write((uint8_t)lcdEntry[lcdEntryLen - 4 + i]);

      // GIANT character: cols 0-3, rows 0-1
      lcdDrawBigChar(lcdLastKey, 0, 0);

      lcd.setCursor(6, 1);
      lcd.print(F("pkt#"));
      lcd.print((long)lcdPktCount);
      lcd.setCursor(13, 1);
      lcd.print(F(" "));
      break;
    }

    // Audio mode: gain + level meter
    case SCR_AUDIO: {
      lcd.setCursor(0, 0);
      if (lcdIcons) lcd.write((uint8_t)ICON_SPK);
      lcd.print(F(" AUDIO  G:"));
      lcd.print((double)currentGain, 1);
      lcd.setCursor(0, 1);
      for (uint8_t i = 0; i < LCD_COLS; i++) lcd.write(i < lcdLevel ? (uint8_t)0xFF : (uint8_t)' ');
      break;
    }

    // Contrast hunt: widest-contrast pattern + progress bar
    case SCR_HUNT: {
      unsigned long el = millis() - lcdHuntT0;
      uint8_t step = (uint8_t)(el * LCD_COLS / LCD_HUNT_MS);
      if (step > LCD_COLS) step = LCD_COLS;
      lcd.setCursor(0, 0);
      lcd.print(F("LCD CONTRAST TEST"));
      lcd.setCursor(0, 1);
      for (uint8_t i = 0; i < LCD_COLS; i++)
        lcd.write(i < step ? (uint8_t)0xFF : (uint8_t)' ');
      break;
    }

    // LDR debug verdict
    case SCR_DEBUG: {
      lcd.setCursor(0, 0);
      if (lcdDbgVar >= 40) lcd.print(F("LDR OK  var:"));
      else               lcd.print(F("LDR BAD var:"));
      lcd.print((long)lcdDbgVar);
      lcd.setCursor(0, 1);
      lcd.print(F("H:")); lcd.print((long)lcdDbgHi);
      lcd.print(F(" L:")); lcd.print((long)lcdDbgLo);
      break;
    }

    // DATA mode: listening / status
    case SCR_LISTEN:
    default: {
      lcd.setCursor(0, 0);
      if (lcdIcons) lcd.write((uint8_t)ICON_SUN);
      lcd.print(F(" LiFi RX: DATA"));
      lcd.setCursor(0, 1);
      lcd.print(F("thr:")); lcd.print((long)threshold);
      lcd.print(F("  pkts:")); lcd.print((long)lcdPktCount);
      break;
    }
  }
  lcdLastDraw = millis();
  lcdDirty = false;
}

// Throttled renderer. force=true bypasses the throttle (mode changes, packet
// received, calibration). NEVER call from the bit-timing path.
void lcdService(bool force) {
  if (!lcdReady || !lcdDirty) return;
  unsigned long now = millis();
  if (!force && (now - lcdLastDraw) < LCD_MIN_DRAW_MS) return;
  lcdDraw();
}