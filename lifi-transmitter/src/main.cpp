/*
 * LiFi Based Audio and Data Transmission Using Arduino
 * ---------------------------------------------------
 * TRANSMITTER (Arduino Uno #1) - PlatformIO (Arduino framework)
 *
 * Dept. of ECE Mini Project
 *
 * MODES:
 *   1. AUDIO MODE - samples analog audio on A0, transmits as LED brightness (PWM)
 *   2. DATA MODE  - reads text lines from Serial Monitor, transmits as light OOK bits
 *
 * TRANSMITTER WIRING (D8 version - D9 COMPLETELY FREE):
 *   LED Driver (DO NOT drive high-power LED directly from pin!):
 *     D8 ---[220R]---> Base of 2N2222 NPN transistor
 *     Transistor Emitter ---> GND
 *     Transistor Collector ---> Cathode (-) of White LED
 *     Anode (+) of White LED ---[100R 1W, or 2x 220R parallel]---> 5V
 *     (For 1W LED use MOSFET IRFZ44N + heatsink + external 5V supply with common GND)
 *     NOTE: D9 is NOT used on this board (free). LED is on D8 digital.
 *     D8 has no hardware PWM, so AUDIO uses 4-bit software PWM (DATA = perfect).
 *
 *   Audio Input - Phone/Laptop AUX only (NO MIC):
 *     GND of 3.5mm -> Arduino GND
 *     Left/Right via 10uF capacitor (+) towards Arduino, (-) towards phone
 *     After cap -> junction of 10k to 5V + 10k to GND (2.5V bias) -> A0
 *     Keep phone volume at 50-70% to avoid clipping.
 *
 *   Mode control: SERIAL ONLY (NO BUTTON in this build).
 *     D2 is FREE. Send A=Audio D=Data M=Toggle via Serial Monitor @9600.
 *
 *   USB ---> PC for Serial Monitor (9600 baud) to type text in DATA mode
 *
 * HOW TO USE:
 *   1. Open this folder `lifi-transmitter` in VS Code with PlatformIO.
 *   2. Select env `uno`, Upload to transmitter Arduino.
 *   3. Open Serial Monitor @ 9600. Type A = Audio, D = Data, M = toggle.
 *   4. In DATA mode, just type a line + Enter -> it is sent over LiFi LED.
 *
 * DATA PROTOCOL (OOK, idle LED = ON, LDR slow mode):
 *   Bit 1 = LED ON (bright), Bit 0 = LED OFF (dark)
 *   Each byte: START(0/OFF, 40ms) + 8 data bits LSB-first + STOP(1/ON, 40ms)
 *   Packet: [0xAA][0xAA][0x55 sync][LEN][PAYLOAD 1..32][CHECKSUM XOR][\n]
 *   Speed: 25 bps (BIT_PERIOD 40ms). LDR needs ~30ms response. Photodiode could use 2ms.
 */

#include <Arduino.h>

// ---------------- Pins (D8 LED, D2/D9 FREE, NO MIC, NO BUTTON) ----------------
#define LED_PIN      8    // Digital pin driving transistor base. D9 FREE on this board.
#define AUX_IN       A0   // Phone/Laptop AUX only (NO MIC module)
#define STATUS_LED   13   // Built-in LED mirrors mode
// D2 FREE (no button). D9 FREE. Control mode via Serial A/D/M only.

// ---------------- Modes (AUTO BEACON: boots in DATA, loops forever) ----------------
enum TxMode : uint8_t { MODE_AUDIO = 0, MODE_DATA = 1 };
volatile TxMode txMode = MODE_DATA;

// Auto-beacon: once uploaded, TX sends these in loop with gaps. No typing needed.
#define AUTO_BEACON 1
const char* BEACON_MSGS[] = {"HI LIFI", "HELLO MINI PROJECT", "ECE MINI PROJECT"};
#define BEACON_N (sizeof(BEACON_MSGS)/sizeof(BEACON_MSGS[0]))
#define BEACON_GAP_MS 5000UL   // pause between messages (LED idle ON)
#define BEACON_FIRST_MS 3000UL // pause after boot before first send
unsigned long beaconLast = 0;
uint8_t beaconIdx = 0;
bool beaconFirst = true;

// ---------------- Data protocol timing (LDR slow mode) ----------------
#define BIT_PERIOD_US   40000UL  // 25 bps for LDR (LDR response ~30ms). Photodiode can use 2000.
// LDR needs 40ms/bit. TX LED OFF=0, ON=1, packet 11 bytes ~4.4 sec for HELLO.
#define PREAMBLE_1      0xAA
#define PREAMBLE_2      0xAA
#define SYNC_BYTE       0x55
#define MAX_PAYLOAD     32

// ---------------- Audio settings ----------------
#define AUDIO_GAIN      2.0f     // 1.5 - 3.0. Increase if receiver volume low
#define AUDIO_CENTER    128      // LED idle brightness (0-255). 128 = 50% ON

// ---------------- Function prototypes ----------------
void setupLED();
// Safe bit delays: delayMicroseconds() is only accurate to 16383us on AVR,
// so 40ms LDR bits MUST use delay() for the ms part.
inline void bitDelay()     { delay(40); }
inline void bitDelayHalf() { delay(20); }
inline void ledPWM(uint8_t v) {
  // D8 has no hardware PWM: 4-bit software PWM handled in audioLoop.
  // This helper is for idle levels: >127 = ON, else OFF.
  digitalWrite(LED_PIN, v > 127 ? HIGH : LOW);
}
inline void ledON()  { digitalWrite(LED_PIN, HIGH); }
inline void ledOFF() { digitalWrite(LED_PIN, LOW); }
void setMode(TxMode m);
void toggleMode();
void checkSerial();
void audioLoop();
void dataLoop();
void sendByteOOK(uint8_t b);
void sendLinePacket(const char *str);

// For audio DC removal (slow tracker of AUX 2.5V bias ~512)
float audioBias = 512.0f;

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(2, INPUT); // D2 FREE (no button in this build)
  pinMode(STATUS_LED, OUTPUT);

  Serial.begin(9600);
  Serial.setTimeout(50);
  while (!Serial) { ; }

  setupLED();
  ledOFF();
  pinMode(9, INPUT); // D9 completely free / high-Z on TX board

  setMode(MODE_DATA); // AUTO: boot straight into DATA loop

  Serial.println(F("\n=============================="));
  Serial.println(F(" LiFi TRANSMITTER Ready"));
  Serial.println(F("=============================="));
  Serial.println(F("Commands: A=Audio  D=Data  M=Toggle (SERIAL ONLY, NO BUTTON)"));
  Serial.println(F("AUTO BEACON ON: loops HI LIFI / HELLO MINI PROJECT / ECE MINI PROJECT every 5s."));
  Serial.println(F("DATA mode: type any line + Enter to send NOW (resets beacon timer)."));
  Serial.println(F("AUDIO mode: play phone/laptop audio into A0, LED carries it (send 'A')."));
  Serial.println(F("NO MIC, NO BUTTON used."));
  Serial.println(F("------------------------------"));
}

void loop() {
  checkSerial();

  if (txMode == MODE_AUDIO) {
    audioLoop();   // returns periodically to service serial
  } else {
    dataLoop();    // non-blocking: checks Serial, sends when line available
  }
}

// ---------- LED setup: D8 digital, D9 free ----------
void setupLED() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  pinMode(9, INPUT);   // D9 free
  pinMode(10, INPUT);
}

// ---------- Mode handling ----------
void setMode(TxMode m) {
  txMode = m;
  if (txMode == MODE_AUDIO) {
    digitalWrite(STATUS_LED, HIGH);
    ledPWM(AUDIO_CENTER);
    audioBias = 512.0f;
    Serial.println(F("[TX] MODE = AUDIO. LED glowing. Play phone audio into A0 (NO MIC)."));
  } else {
    digitalWrite(STATUS_LED, LOW);
    ledON(); // idle ON (illumination) in DATA mode
    beaconLast = millis(); beaconFirst = true; // restart beacon gap on entering DATA
    Serial.println(F("[TX] MODE = DATA AUTO-BEACON. Looping messages every 5s. LED idle = ON."));
  }
}

void toggleMode() {
  setMode(txMode == MODE_AUDIO ? MODE_DATA : MODE_AUDIO);
}

void checkSerial() {
  // --- Serial commands only (NO BUTTON in this build) ---
  // Single chars 'A'/'D'/'M' alone are commands.
  // Multi-char lines are sent immediately (also resets beacon timer).
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() == 1) {
      char c = toupper(cmd.charAt(0));
      if (c == 'A') { setMode(MODE_AUDIO); return; }
      if (c == 'D') { setMode(MODE_DATA); return; }
      if (c == 'M') { toggleMode(); return; }
    }
    // If in DATA mode and line is payload, send it now + reset beacon timer
    if (txMode == MODE_DATA && cmd.length() > 0) {
      sendLinePacket(cmd.c_str());
      beaconLast = millis();
    } else if (txMode == MODE_AUDIO && cmd.length() > 0) {
      Serial.println(F("[TX] In AUDIO. Send 'D' for DATA/beacon."));
    }
  }
}

// ---------- AUDIO MODE on D8 (no hardware PWM) ----------
// 4-bit software PWM: 16 levels, ~4kHz sampling. DATA mode unaffected (perfect).
// For best audio quality use D9 PWM version, but D8 keeps D9 free as requested.
void audioLoop() {
  const int BATCH = 120; // smaller batch: software PWM is slower, keep serial responsive
  for (int i = 0; i < BATCH; i++) {
    int raw = analogRead(AUX_IN);  // ~112 us

    // Track slow DC bias (AUX 2.5V bias ~512)
    audioBias += 0.002f * ((float)raw - audioBias);

    float ac = ((float)raw - audioBias) * AUDIO_GAIN;
    int out = (int)(AUDIO_CENTER + ac);

    if (out < 0) out = 0;
    if (out > 255) out = 255;
    uint8_t lvl = (uint8_t)(out >> 4); // 0..15
    // One 4-bit PWM cycle (~128us): HIGH for lvl steps, LOW for rest
    for (uint8_t s = 0; s < 16; s++) {
      digitalWrite(LED_PIN, s < lvl ? HIGH : LOW);
      delayMicroseconds(8);
    }
  }
  // Return to loop() so serial is serviced without breaking audio much
}

// ---------- DATA MODE (AUTO BEACON LOOP) ----------
// Once uploaded: SEND -> idle ON 5s -> SEND next -> ... forever. No typing needed.
void dataLoop() {
#if AUTO_BEACON
  unsigned long now = millis();
  unsigned long gap = beaconFirst ? BEACON_FIRST_MS : BEACON_GAP_MS;
  if (now - beaconLast >= gap) {
    beaconFirst = false;
    const char* msg = BEACON_MSGS[beaconIdx];
    beaconIdx = (beaconIdx + 1) % BEACON_N;
    sendLinePacket(msg);   // prints Sending/Done, LED blinks ~3-8s per msg
    beaconLast = millis(); // gap counted AFTER send finishes
    ledON(); // idle ON between messages
  } else {
    ledON(); // idle illumination while waiting
    digitalWrite(STATUS_LED, (now / 500) % 2); // slow blink while waiting
    delay(50);
  }
#else
  // Manual only (beacon off)
  static unsigned long lastIdle = 0;
  if (millis() - lastIdle > 1000) {
    lastIdle = millis();
    ledON();
    digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
  }
  delay(10);
#endif
}

// Send one byte: START(0) + 8 bits LSB first + STOP(1)
// Uses safe bitDelay() (NOT delayMicroseconds: inaccurate above 16383us on AVR).
void sendByteOOK(uint8_t b) {
  // START bit = LED OFF
  ledOFF();
  bitDelay();

  for (uint8_t i = 0; i < 8; i++) {
    if (b & (1 << i)) ledON();
    else ledOFF();
    bitDelay();
  }

  // STOP bit = LED ON
  ledON();
  bitDelay();

  // Small inter-byte gap
  bitDelayHalf();
}

void sendLinePacket(const char *str) {
  size_t len = strlen(str);
  if (len == 0) return;
  if (len > MAX_PAYLOAD) len = MAX_PAYLOAD;

  Serial.print(F("[TX] Sending ("));
  Serial.print(len);
  Serial.print(F(" chars): "));
  Serial.println(str);

  // Visual: status LED on during TX
  digitalWrite(STATUS_LED, HIGH);

  // Preamble + sync for receiver AGC/threshold lock
  sendByteOOK(PREAMBLE_1);
  sendByteOOK(PREAMBLE_2);
  sendByteOOK(SYNC_BYTE);

  // Length
  sendByteOOK((uint8_t)len);

  // Payload + checksum
  uint8_t chk = (uint8_t)len;
  for (size_t i = 0; i < len; i++) {
    sendByteOOK((uint8_t)str[i]);
    chk ^= (uint8_t)str[i];
  }
  sendByteOOK(chk);
  sendByteOOK('\n'); // end marker byte

  ledON(); // back to idle
  digitalWrite(STATUS_LED, LOW);
  Serial.println(F("[TX] Done. LED back to idle ON."));
}
