/*
 * LiFi Based Audio and Data Transmission Using Arduino
 * ---------------------------------------------------
 * RECEIVER (Arduino Uno #2) - PlatformIO (Arduino framework)
 *
 * Dept. of ECE Mini Project
 *
 * MODES (must match TRANSMITTER mode):
 *   1. AUDIO MODE - samples light sensor on A0, reconstructs audio on D9 -> amp -> speaker
 *   2. DATA MODE  - decodes light OOK bits to text, prints on Serial Monitor
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
 *   Mode control: SERIAL ONLY (NO BUTTON in this build).
 *     D2 FREE. Send A=Audio D=Data M=Toggle C=Calibrate ?=Debug +/-=Volume.
 *
 *   USB ---> PC, open Serial Monitor @ 9600 to see received text + calibration.
 *
 * CALIBRATION (DATA mode):
 *   - On boot, receiver auto-measures ambient light for 2 sec.
 *   - Every packet preamble (0xAA 0xAA) re-locks threshold = (min+max)/2.
 *   - Tip: do demo indoors, avoid direct sunlight on sensor. Shade sensor with tube.
 *   - Keep TX LED and RX sensor aligned, distance 10-30cm first, then extend.
 *   - Send 'C' in Serial Monitor to re-calibrate anytime.
 *
 * DATA PROTOCOL (must match transmitter, LDR slow mode):
 *   25 bps OOK (40ms/bit, LDR needs ~30ms), START=OFF, 8 bits LSB-first, STOP=ON
 *   Packet: AA AA 55 LEN PAYLOAD CHECKSUM(XOR) 0x0A
 */

#include <Arduino.h>

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
  Serial.println(F("LDR: A0-GND only. PAM8403 D10. 20-30cm, shade, indoors."));

  calibrateAmbient();
  setMode(MODE_DATA); // AUTO: boot straight into listen loop (send 'A' for speaker audio)

  Serial.println(F("------------------------------"));
}

void loop() {
  pollSerial();

  if (rxMode == MODE_AUDIO) {
    audioLoop();
  } else {
    dataLoop();
  }
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
    Serial.println(F("[RX] MODE = AUDIO. Output D10 -> 10k/10nF/10uF -> PAM8403 R-IN -> speaker."));
    Serial.print(F("[RX] Gain=")); Serial.print(currentGain);
    Serial.println(F(" (+/- to trim, PAM8403 knob at middle)."));
  } else {
    digitalWrite(STATUS_LED, LOW);
    // CENTER = silence (0V AC after 10uF DC-block). Hold here in DATA mode.
    audioPWM(RX_CENTER);
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
    else if (up == 'M') toggleMode();
    else if (up == 'C') { calibrateAmbient(); if (rxMode == MODE_DATA) setMode(MODE_DATA); }
    else if (c == '?') debugStream();
    else if (c == '+' || c == '=') {
      currentGain += 0.5f; if (currentGain > 8.0f) currentGain = 8.0f;
      Serial.print(F("[RX] Gain=")); Serial.println(currentGain);
    }
    else if (c == '-' || c == '_') {
      currentGain -= 0.5f; if (currentGain < 0.5f) currentGain = 0.5f;
      Serial.print(F("[RX] Gain=")); Serial.println(currentGain);
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
    delay(200);
  }
  Serial.print(F("[DBG] min=")); Serial.print(mn);
  Serial.print(F(" max=")); Serial.print(mx);
  Serial.print(F(" variation=")); Serial.println(mx - mn);
  if (mx - mn < 40) Serial.println(F("[DBG] FAIL: variation <40. Check A0-GND wiring, 20-30cm distance, shade tube."));
  else Serial.println(F("[DBG] OK: LDR sees light changes."));
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
  }
  // return to loop() to service serial
}

// ---------------- DATA MODE (AUTO LISTEN LOOP) ----------------
// Once uploaded: listens forever, prints Got: each time TX beacon arrives,
// AND plays beep on speaker automatically (no phone/PC needed).
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
    ackBeep(); // speaker confirms automatically using LED+LDR+speaker only
  }
  // receivePacket is blocking (slow LDR: ~0.4s/byte, packet ~4s),
  // so serial checked between packets in loop().
}

// Measure ambient: assumes TX is sending preamble/idle toggling,
// or at least TX LED is ON. We sample 2 sec min/max.
void calibrateAmbient() {
  Serial.println(F("[RX] Calibrating... keep TX LED facing sensor, TX in DATA mode."));
  int mn = 1023, mx = 0;
  unsigned long t0 = millis();
  while (millis() - t0 < 2000) {
    int v = readSensor();
    if (v < mn) mn = v;
    if (v > mx) mx = v;
    delay(5);
    // blink while calibrating
    digitalWrite(STATUS_LED, (millis() / 200) % 2);
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
    // allow background? tight loop ok for short timeout
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
    // (approximate: sample current sensor extremes during hunt)
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
