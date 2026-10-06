/*
 * LiFi Based Audio and Data Transmission Using Arduino
 * ---------------------------------------------------
 * TRANSMITTER (Arduino Uno #1) - PlatformIO (Arduino framework)
 *
 * Dept. of ECE Mini Project
 *
 * MODES:
 *   1. AUDIO MODE - samples analog audio on A0, transmits as LED brightness (PWM)
 *   2. DATA MODE  - 4x4 MATRIX KEYPAD + text from Serial Monitor,
 *                   transmits each key over the LiFi LED as light OOK bits
 *
 * TRANSMITTER WIRING (KEYPAD BUILD - LED moved D8 -> D12):
 *   LED Driver (DO NOT drive high-power LED directly from pin!):
 *     D12 --[220R]---> Base of 2N2222 NPN transistor
 *     Transistor Emitter ---> GND
 *     Transistor Collector ---> Cathode (-) of White LED
 *     Anode (+) of White LED ---[100R 1W, or 2x 220R parallel]---> 5V
 *     (For 1W LED use MOSFET IRFZ44N + heatsink + external 5V supply with common GND)
 *     D12 has hardware PWM (Timer0 OC2A), so AUDIO mode can use real analogWrite().
 *     D12 is only driven while the keypad is being scanned (~0.3 ms every 20 ms),
 *     and the LiFi bit timing is unaffected because every LiFi bit uses delay().
 *
 *   4x4 MATRIX KEYPAD (standard phone layout, no diodes needed):
 *     Rows = INPUT_PULLUP (idle HIGH, pulled LOW by the switch, key = active LOW)
 *       R0 (R1 on the pad) ---> D2
 *       R1 (R2 on the pad) ---> D3
 *       R2 (R3 on the pad) ---> D4
 *       R3 (R4 on the pad) ---> D5
 *     Cols = OUTPUT, idle LOW, driven HIGH to scan one column at a time
 *       C0 (C1 on the pad) ---> D6
 *       C1 (C2 on the pad) ---> D7
 *       C2 (C3 on the pad) ---> D8
 *       C3 (C4 on the pad) ---> D9
 *     Pad silk:   C1 C2 C3 C4        (top, 4 column pins)
 *                 1  2  3  A         (keys 1,2,3,A map to R1C1 .. R1C4)
 *                 4  5  6  B
 *                 7  8  9  C
 *                 *  0  #  D
 *                 R1 R2 R3 R4        (bottom, 4 row pins)
 *     Key table used in code (row*4 + col index):
 *       index  0  1  2  3   4  5  6  7   8  9 10 11  12 13 14 15
 *       char   1  2  3  A   4  5  6  B   7  8  9  0   *  #  C  D
 *       (add E F G H on D0-D3 if you wire the 4th row the other way round)
 *     Scan: every 20 ms -> all cols LOW, then for each row: row INPUT_PULLUP,
 *           that row's col HIGH, delay 1 us, read all 4 rows. One detected row
 *           + one detected col = one valid key (ghosting safe).
 *     Debounce 40 ms, released-before-next-key, no auto-repeat (set HOLD_REPEAT_MS).
 *
 *   Audio Input - Phone/Laptop AUX only (NO MIC):
 *     GND of 3.5mm -> Arduino GND
 *     Left/Right via 10uF capacitor (+) towards Arduino, (-) towards phone
 *     After cap -> junction of 10k to 5V + 10k to GND (2.5V bias) -> A0
 *     Keep phone volume at 50-70% to avoid clipping.
 *
 *   Mode control: SERIAL ONLY (NO BUTTON in this build).
 *     A=Audio  D=Data  M=Toggle  K=Keypad test  R=Reset buffer  T=send buffer
 *
 *   USB ---> PC for Serial Monitor (9600 baud)
 *
 * HOW TO USE:
 *   1. Open this folder `lifi-transmitter` in VS Code with PlatformIO.
 *   2. Select env `uno`, Upload to transmitter Arduino.
 *   3. Open Serial Monitor @ 9600. Type A = Audio, D = Data, M = toggle,
 *      K = keypad raw test, T = send typed buffer.
 *   4. In DATA mode every key press is transmitted over LiFi automatically.
 *
 * KEYPAD BEHAVIOUR (KEYS_AS_TEXT 1, default):
 *   Press 5  -> queue the character '5'
 *   Press H  -> queue 'H'          (buffer scrolls at 16 chars = LCD row width)
 *   Press SPACE -> send the whole buffer NOW as one packet:  "HELLO"  -> "#HELLO"
 *   Press C or *  -> clear the buffer
 *   Press S        -> send the buffer NOW (same as SPACE)
 *   Set KEYS_AS_TEXT 0 to send the raw matrix index (row*4+col = 0..15) instead,
 *   and/or LOWERCASE to transmit a..p instead of A..P.
 *
 * DATA PROTOCOL (OOK, idle LED = ON, LDR slow mode):
 *   Bit 1 = LED ON (bright), Bit 0 = LED OFF (dark)
 *   Each byte: START(0/OFF, 40ms) + 8 data bits LSB-first + STOP(1/ON, 40ms)
 *   Packet: [0xAA][0xAA][0x55 sync][LEN][PAYLOAD 1..32][CHECKSUM XOR][\n]
 *   Key packets are plain text with KEY_PREFIX in front, so the existing
 *   receiver prints  Got: #5  and shows  #5  on the 16x2 LCD with no changes.
 *   Speed: 25 bps (BIT_PERIOD 40ms). LDR needs ~30ms response. Photodiode could use 2ms.
 */

#include <Arduino.h>

// ---------------- Pins (LED on D12, keypad D2-D9, NO MIC, NO BUTTON) ------------
#define LED_PIN      12   // LED driver pin (transistor base via 220R)
#define AUX_IN       A0   // Phone/Laptop AUX only (NO MIC module)
#define STATUS_LED   13   // Built-in LED mirrors mode / TX activity
// No push button: mode + keypad are the only inputs, plus Serial commands.

// ============================================================================
// DATA LINK SELECTION  <-- the one switch that makes this project "simple"
//   1 = WIRED   : 2 jumper wires carry the data (D11 -> D3, plus common GND).
//                  Press a key -> the number lands on the receiver LCD almost
//                  instantly. No light alignment, no threshold, no LDR.
//   0 = OPTICAL : the original LiFi path - OOK light pulses LED -> LDR at 25 bps
//                  (40 ms/bit, needs 20-30 cm alignment and a working LDR).
// ============================================================================
#define DATA_LINK_WIRED   1

// Wired link pins (SoftwareSerial so the USB Serial Monitor keeps working).
//   Transmitter D11 (TX) ---> Receiver D3 (RX)
//   Transmitter GND       ---> Receiver GND      (MUST be common)
// D10 (TX board) and D4 (RX board) are the unused opposite ends.
#define WIRE_TX_PIN       11
#define WIRE_RX_PIN       10

// ---------------- 4x4 matrix keypad (row*4 + col -> key index) ---------------
#define KEYPAD_ENABLED    1     // 0 = build without the keypad (LED can go back to D8)
#define KP_ROWS           4
#define KP_COLS           4
#define KEY_DEBOUNCE_MS   40    // must be > one scan period (20 ms)
#define KEY_SCAN_MS       20    // keypad scan period
#define HOLD_REPEAT_MS    0     // 0 = no auto-repeat while a key is held
#define KEYS_AS_TEXT      1     // 1 = "5", 0 = raw index 0..15
#define KEYS_LOWERCASE    0     // 1 = transmit a..p instead of A..P
#define KEY_PREFIX        '#'   // packet prefix -> receiver shows "#5" / "HI 5"
#define TX_BUFFER_LEN     16    // typed buffer = one LCD row
#define TX_BUFFER_START   14    // SPACE or S sends once this many chars are typed

// SEND_EACH_KEY: transmit EVERY keypress immediately, instead of only filling
// the local buffer and waiting for SPACE/S.
//
// WHY THIS IS THE DEFAULT: at 25 bps one "#5" packet takes ~1.2 s on air, so a
// single-key packet is cheap. With the old buffer-only behaviour a keypad user
// pressed digits and NOTHING was transmitted until SPACE (which does not even
// exist on a 4x4 pad) or a serial SPACE/S was sent - the keypad looked dead.
// The local buffer is still maintained for the Serial Monitor echo.
// Set to 0 to restore the old "fill buffer, press SPACE to send" behaviour.
#define SEND_EACH_KEY     1

// Row pins: INPUT_PULLUP (idle HIGH, key press pulls the row LOW)
static const uint8_t KP_ROW_PIN[KP_ROWS] = { 2, 3, 4, 5 };
// Column pins: OUTPUT (idle LOW, one driven HIGH per scan step)
static const uint8_t KP_COL_PIN[KP_COLS] = { 6, 7, 8, 9 };

// row*4 + col -> character (28 chars, single frame -> ~11 s over the 25 bps link)
static const char KEYS_ASCII[] = "1234567890*#ABCDEFGHIJKLMNOP";

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

// ---------------- Key transmit queue (keeps every press, sends one by one) ----
#define TXQ_SIZE 8
uint8_t txq[TXQ_SIZE];
uint8_t txqHead = 0, txqTail = 0;
uint8_t txqCount = 0;

// ---------------- Typed text buffer (KEYS_AS_TEXT mode) ---------------------
char     txText[TX_BUFFER_LEN + 1] = { 0 };
uint8_t  txTextLen = 0;
bool     txSendTextNow = false;   // SPACE / S requested an immediate send

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
  // D12 is a hardware PWM pin (Timer0 OC2A) -> real 8-bit PWM for AUDIO mode.
  // >0 = ON (bright), 0 = OFF (dark).
  analogWrite(LED_PIN, v);
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
void sendTextBuffer();

// keypad (defined further down, prototypes here)
void keypadInit();
void keypadTask();
int  keypadScan();
char keyChar(uint8_t k);
void keypadHandle(uint8_t k);
void keypadFlash(uint8_t times);
void txqPush(uint8_t v);
bool txqPop(uint8_t &v);
uint8_t txqCountNow() { return txqCount; }
void textPush(char c);
void textClear();
bool beaconDue();

// For audio DC removal (slow tracker of AUX 2.5V bias ~512)
float audioBias = 512.0f;

// ---------------- Wired link (DATA_LINK_WIRED == 1) ------------------------
// SoftwareSerial keeps the USB Serial Monitor free for debugging.
#if DATA_LINK_WIRED
#include <SoftwareSerial.h>
SoftwareSerial wireLink(WIRE_RX_PIN, WIRE_TX_PIN);   // RX, TX

// Send one short line per keypad press: "<key>\n". The receiver shows it as a
// giant character and appends digits to its rolling ENTRY string.
// 9600 baud over 2 jumper wires is instant and cannot be misread.
void wireSendKey(char ch) {
  wireLink.print(ch);
  wireLink.print('\n');
}
#endif

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(STATUS_LED, OUTPUT);

  Serial.begin(9600);
  Serial.setTimeout(50);
  while (!Serial) { ; }

  setupLED();
  ledOFF();

#if DATA_LINK_WIRED
  wireLink.begin(9600);     // wired link on D10/D11 - 9600 baud, instant
  Serial.println(F("[WIRE] Link ON: D11(TX) ---> RX board D3(RX), GND ---> GND."));
#endif

#if KEYPAD_ENABLED
  keypadInit();
#endif

  setMode(MODE_DATA); // AUTO: boot straight into DATA loop

  Serial.println(F("\n=============================="));
  Serial.println(F(" LiFi TRANSMITTER Ready (4x4 KEYPAD)"));
  Serial.println(F("=============================="));
  Serial.println(F("Commands: A=Audio  D=Data  M=Toggle  K=Keypad test"));
  Serial.println(F("          T=send typed buffer  R=clear buffer"));
  Serial.println(F("KEYPAD on D2-D9 (rows pull-up, cols drive). LED driver on D12."));
  Serial.println(F("Keys: 1 2 3 A / 4 5 6 B / 7 8 9 C / * 0 # D  (+E F G H on D0-D3)"));
  Serial.print(F("KEYS_AS_TEXT=")); Serial.print(KEYS_AS_TEXT ? 1 : 0);
  Serial.print(F(" LOWERCASE=")); Serial.print(KEYS_LOWERCASE ? 1 : 0);
  Serial.print(F(" prefix='")); Serial.write(KEY_PREFIX);
  Serial.println(F("' -> receiver prints Got: #<key> and shows it on the LCD"));
  Serial.println(F("SPACE or S = send buffer, C or * = clear buffer."));
  Serial.println(F("AUTO BEACON ON: loops HI LIFI / HELLO MINI PROJECT / ECE MINI PROJECT every 5s."));
  Serial.println(F("AUDIO mode: play phone/laptop audio into A0, LED carries it (send 'A')."));
  Serial.println(F("NO MIC, NO BUTTON used."));
  Serial.println(F("------------------------------"));
}

void loop() {
  checkSerial();
  keypadTask();   // non-blocking: 20 ms scan + debounce, LED pulse on press

  if (txMode == MODE_AUDIO) {
    audioLoop();   // returns periodically to service serial + keypad
  } else {
    dataLoop();    // queue first, then beacon; non-blocking
  }
}

// ---------- LED setup: D12 digital/PWM ----------
void setupLED() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
}

// ---------- Mode handling ----------
void setMode(TxMode m) {
  txMode = m;
  if (txMode == MODE_AUDIO) {
    digitalWrite(STATUS_LED, HIGH);
    ledPWM(AUDIO_CENTER);
    audioBias = 512.0f;
    Serial.println(F("[TX] MODE = AUDIO. LED glowing. Play phone audio into A0 (NO MIC)."));
    Serial.println(F("[TX] Keypad paused in AUDIO (send 'D' to transmit keys)."));
  } else {
    digitalWrite(STATUS_LED, LOW);
    ledON(); // idle ON (illumination) in DATA mode
    beaconLast = millis(); beaconFirst = true; // restart beacon gap on entering DATA
    Serial.println(F("[TX] MODE = DATA. Keypad live: every press is sent over LiFi."));
  }
}

void toggleMode() {
  setMode(txMode == MODE_AUDIO ? MODE_DATA : MODE_AUDIO);
}

void checkSerial() {
  // --- Serial commands only (NO BUTTON in this build) ---
  // Single chars 'A'/'D'/'M'/'K'/'T'/'R' alone are commands.
  // Multi-char lines are queued and sent from dataLoop (keeps LED bit timing clean).
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() == 1) {
      char c = toupper(cmd.charAt(0));
      if (c == 'A') { setMode(MODE_AUDIO); return; }
      if (c == 'D') { setMode(MODE_DATA); return; }
      if (c == 'M') { toggleMode(); return; }
      if (c == 'K') { keypadFlash(4); Serial.println(F("[TX] Keypad LED test OK (4 flashes).")); return; }
      if (c == 'R') { textClear(); Serial.println(F("[TX] Typed buffer cleared.")); return; }
      if (c == 'T') { txSendTextNow = true; return; }
    }
    // Multi-char line: queue it as key presses (prefix '#' is added on send)
    if (cmd.length() > 0 && txMode == MODE_DATA) {
      for (uint8_t i = 0; i < cmd.length() && i < TXQ_SIZE; i++) txqPush((uint8_t)cmd.charAt(i));
      Serial.print(F("[TX] Queued ("));
      Serial.print(cmd.length());
      Serial.println(F(" chars). Sending now..."));
    } else if (cmd.length() > 0) {
      Serial.println(F("[TX] In AUDIO. Send 'D' for DATA/keypad."));
    }
  }
}

// ---------- AUDIO MODE on D12 (hardware PWM on Timer0 OC2A) ----------
// Real 8-bit analogWrite() - smoother than the old software PWM, ~490 Hz carrier
// (too slow to carry audio, fine for the eye + the LDR sees slow brightness only).
// DATA/keypad mode is untouched by this: LiFi bits use delay() and the LED is digital.
void audioLoop() {
  const int BATCH = 120;
  for (int i = 0; i < BATCH; i++) {
    int raw = analogRead(AUX_IN);  // ~112 us

    // Track slow DC bias (AUX 2.5V bias ~512)
    audioBias += 0.002f * ((float)raw - audioBias);

    float ac = ((float)raw - audioBias) * AUDIO_GAIN;
    int out = (int)(AUDIO_CENTER + ac);

    if (out < 0) out = 0;
    if (out > 255) out = 255;
    ledPWM((uint8_t)out);
    delay(1);   // ~8 kHz brightness update, still returns to loop() often
  }
  // Return to loop() so serial + keypad are serviced without breaking audio much
}

// ---------- DATA MODE (KEYPAD QUEUE + AUTO BEACON) ----------
bool beaconDue() {
#if AUTO_BEACON
  unsigned long gap = beaconFirst ? BEACON_FIRST_MS : BEACON_GAP_MS;
  return (millis() - beaconLast) >= gap;
#else
  return false;
#endif
}

void dataLoop() {
#if DATA_LINK_WIRED
  // ---- WIRED LINK ----
  // Keys already went out from keypadHandle() the instant they were pressed, so
  // there is nothing to transmit here. This branch only:
  //   * shows the LED is alive,
  //   * drains any serial text typed on the monitor ("123" -> sent as-is),
  //   * re-sends the typed buffer when SPACE / S / 'T' is used.
  ledON();
  digitalWrite(STATUS_LED, (millis() / 500) % 2);   // slow blink = link alive
  delay(10);

  if (txSendTextNow) {                    // SPACE / S / 'T' -> send the buffer
    txSendTextNow = false;
    sendTextBuffer();
  }
  return;
#else
  // 1) Highest priority: "send what I typed" (SPACE / S / 'T' command)
  if (txSendTextNow) {
    txSendTextNow = false;
    sendTextBuffer();
    return;
  }

  // 2) Keypad / serial queue: one key per frame, LED free in between
  uint8_t k = 0;
  if (txqPop(k)) {
    char pkt[TX_BUFFER_LEN + 2];
    uint8_t n = 0;
    pkt[n++] = KEY_PREFIX;
    pkt[n] = '\0';
    if (KEYS_AS_TEXT) {
      char c = keyChar(k);
      pkt[n++] = c;
    } else {
      // raw index 0..15 as one decimal digit ("0".."9", then 'a'..'f')
      pkt[n++] = (k < 10) ? ('0' + k) : ('a' + (k - 10));
    }
    pkt[n] = '\0';
    sendLinePacket(pkt);
    beaconLast = millis();
    return;
  }

#if AUTO_BEACON
  // 3) Auto beacon (never overlaps a key packet - the beacon waits for an empty queue)
  unsigned long now = millis();
  if (beaconDue()) {
    beaconFirst = false;
    const char* msg = BEACON_MSGS[beaconIdx];
    beaconIdx = (beaconIdx + 1) % BEACON_N;
    sendLinePacket(msg);
    beaconLast = millis();
    ledON();
  } else {
    ledON(); // idle illumination while waiting
    digitalWrite(STATUS_LED, (now / 500) % 2); // slow blink while waiting
    delay(20); // keeps the 20 ms keypad scan jitter-free
  }
#else
  static unsigned long lastIdle = 0;
  if (millis() - lastIdle > 1000) {
    lastIdle = millis();
    ledON();
    digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
  }
  delay(20);
#endif // AUTO_BEACON
#endif // DATA_LINK_WIRED
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

// Send everything typed on the keypad as one packet: "HELLO" -> "#HELLO"
void sendTextBuffer() {
  if (txTextLen == 0) {
    Serial.println(F("[TX] Buffer empty - nothing to send (type a key first)."));
    return;
  }
  char pkt[TX_BUFFER_LEN + 2];
  uint8_t n = 0;
  pkt[n++] = KEY_PREFIX;
  for (uint8_t i = 0; i < txTextLen; i++) pkt[n++] = txText[i];
  pkt[n] = '\0';
  textClear();
  sendLinePacket(pkt);
  beaconLast = millis();
}

// ============================================================================
// 4x4 MATRIX KEYPAD DRIVER
//   Rows = INPUT_PULLUP (idle HIGH). Cols = OUTPUT (idle LOW).
//   Scan one column at a time: exactly one row + one column detected = one key.
// ============================================================================

// Keypad state machine
static uint8_t  kpLastRaw    = 0xFF; // last raw scan result (255 = nothing)
static uint8_t  kpCandidate  = 0xFF; // debounce candidate
static unsigned long kpCandMs  = 0;   // when the candidate appeared
static unsigned long kpLastScan = 0;
static uint8_t  kpHeldMs     = 0;    // how long the key is held (auto-repeat)
static char     kpLastChar   = '?';  // last accepted key char

#if KEYPAD_ENABLED

void keypadInit() {
  for (uint8_t r = 0; r < KP_ROWS; r++) {
    pinMode(KP_ROW_PIN[r], INPUT_PULLUP); // idle HIGH, key press pulls LOW
  }
  for (uint8_t c = 0; c < KP_COLS; c++) {
    pinMode(KP_COL_PIN[c], OUTPUT);
    digitalWrite(KP_COL_PIN[c], LOW);     // idle LOW, no ghosting
  }
  kpLastRaw = kpCandidate = 0xFF;
  kpCandMs = kpLastScan = millis();
  Serial.println(F("[KP] 4x4 matrix ready: rows D2 D3 D4 D5 (pull-up), cols D6 D7 D8 D9 (drive)."));
}

// Returns row*4 + col (0..15) of the pressed key, or 255 when nothing is pressed.
int keypadScan() {
  for (uint8_t c = 0; c < KP_COLS; c++) digitalWrite(KP_COL_PIN[c], LOW); // idle
  for (uint8_t r = 0; r < KP_ROWS; r++) pinMode(KP_ROW_PIN[r], INPUT_PULLUP);

  for (uint8_t c = 0; c < KP_COLS; c++) {
    digitalWrite(KP_COL_PIN[c], HIGH);          // drive this column
    delayMicroseconds(1);                       // settle
    for (uint8_t r = 0; r < KP_ROWS; r++) {
      if (digitalRead(KP_ROW_PIN[r]) == LOW) {  // row pulled LOW = key in this cell
        // Keypad feedback pulse: LED flashes on every press (proves the scan works).
        ledON(); delay(1); ledOFF();
        for (uint8_t k = 0; k < KP_COLS; k++) digitalWrite(KP_COL_PIN[k], LOW); // clean up
        return (int)(r * KP_COLS + c);
      }
    }
    digitalWrite(KP_COL_PIN[c], LOW);           // next column
  }
  return 0xFF;
}

char keyChar(uint8_t k) {
  if (k >= sizeof(KEYS_ASCII) - 1) return '?';
  char c = KEYS_ASCII[k];
#if KEYS_LOWERCASE
  if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
#endif
  return c;
}

void keypadFlash(uint8_t times) {
  for (uint8_t i = 0; i < times; i++) {
    ledON();  delay(40);
    ledOFF(); delay(40);
  }
  ledON();
}

// Non-blocking: called every loop(). Scans, debounces and queues one key press.
void keypadTask() {
  unsigned long now = millis();
  if (now - kpLastScan < KEY_SCAN_MS) return;
  kpLastScan = now;

  uint8_t raw = (uint8_t)keypadScan();   // 255 = released

  if (raw != kpCandidate) {              // candidate changed -> start debounce window
    kpCandidate = raw;
    kpCandMs = now;
    kpHeldMs = 0;
    return;
  }

  if (raw == kpLastRaw) {                // stable state
#if HOLD_REPEAT_MS
    if (raw != 0xFF) {                   // optional auto-repeat while held
      kpHeldMs += KEY_SCAN_MS;
      if (kpHeldMs >= HOLD_REPEAT_MS) {
        kpHeldMs = 0;
        kpCandMs = now;                  // keep the debounce window fed
        keypadHandle(raw);
      }
    }
#endif
    return;
  }

  // Stable for KEY_DEBOUNCE_MS -> this is a real (release-to-press) transition
  if ((now - kpCandMs) < KEY_DEBOUNCE_MS) return;
  kpLastRaw = raw;
  keypadHandle(raw);
}

// Route an accepted key press to the text buffer or the transmit queue
void keypadHandle(uint8_t k) {
  if (k == 0xFF) {                       // released
    kpHeldMs = 0;
    return;
  }
  kpHeldMs = 0;

  char ch = keyChar(k);
  kpLastChar = ch;

#if DATA_LINK_WIRED
  // ---- WIRED LINK: every press goes out immediately and returns -----------
  if (ch == '*' || ch == 'C') {          // clear key
    wireLink.print(F("clr"));
    wireLink.print('\n');
    Serial.println(F("[TX] '*' -> clear receiver entry (wired)."));
    keypadFlash(2);
    return;
  }
  wireSendKey(ch);
  Serial.print(F("[TX] wired key '"));
  Serial.write(ch);
  Serial.println(F("' sent on D11."));
  keypadFlash(1);
  return;                                // no buffering, no beacon, done
#else

  if (txMode == MODE_AUDIO) {             // LiFi is busy with audio - do not queue
    Serial.print(F("[TX] Key '"));
    Serial.write(ch);
    Serial.print(F("' (matrix index "));
    Serial.print(k);
    Serial.println(F(") ignored - send 'D' to transmit keys."));
    return;
  }

  // Control keys (independent of KEYS_AS_TEXT)
  if (ch == 'C' || ch == '*') {          // clear the typed buffer
    textClear();
    Serial.println(F("[TX] '*'/'C' -> buffer cleared."));
    keypadFlash(2);
    return;
  }
  if (ch == 'S') {                       // send the typed buffer now
    txSendTextNow = true;
    Serial.println(F("[TX] 'S' -> sending buffer."));
    keypadFlash(3);
    return;
  }

  if (KEYS_AS_TEXT) {
    if (ch == ' ') {                     // SPACE = word send
      if (txTextLen > 0) {
        txSendTextNow = true;
        Serial.println(F("[TX] SPACE -> sending buffer."));
        keypadFlash(3);
      }
      return;
    }
    textPush(ch);                        // local echo in the Serial Monitor

#if SEND_EACH_KEY
    // Transmit this single key right away as a "#<key>" packet. This is what
    // makes the keypad usable: one press -> one packet -> one giant character
    // on the receiver LCD.
    if (txqCountNow() < 2) txqPush(k);   // keep the queue short (one frame in flight)
    Serial.println(F("[TX] key sent over LiFi."));
#endif
  } else {
    txqPush(k);                          // raw index 0..15 straight to the queue
  }
  keypadFlash(1);
#endif // DATA_LINK_WIRED
}

void textPush(char c) {
  if (txTextLen >= TX_BUFFER_LEN) {      // scroll, keep the newest TX_BUFFER_LEN chars
    for (uint8_t i = 1; i < TX_BUFFER_LEN; i++) txText[i - 1] = txText[i];
    txTextLen = TX_BUFFER_LEN - 1;
  }
  txText[txTextLen++] = c;
  txText[txTextLen] = '\0';

  Serial.print(F("[TX] key '"));
  Serial.write(c);
  Serial.print(F("'  buffer=\""));
  Serial.print(txText);
  Serial.print(F("\" ("));
  Serial.print(txTextLen);
  Serial.print(F("/"));
  Serial.print(TX_BUFFER_LEN);
  Serial.println(F(") SPACE=S"));
}

void textClear() {
  txTextLen = 0;
  txText[0] = '\0';
}

// ---------------- tiny ring buffer so no key press is lost ----------------
void txqPush(uint8_t v) {
  if (txqCount >= TXQ_SIZE) {            // full: drop oldest, keep the newest keys
    txqHead = (txqHead + 1) % TXQ_SIZE;
    txqCount--;
    Serial.println(F("[TX] Queue full - oldest key dropped."));
  }
  txq[txqTail] = v;
  txqTail = (txqTail + 1) % TXQ_SIZE;
  txqCount++;
}

bool txqPop(uint8_t &v) {
  if (txqCount == 0) return false;
  v = txq[txqHead];
  txqHead = (txqHead + 1) % TXQ_SIZE;
  txqCount--;
  return true;
}

#else  // ---------- KEYPAD_ENABLED 0: original LED-on-D8 build ----------

void keypadInit() {}
int  keypadScan() { return 0xFF; }
char keyChar(uint8_t k) { return KEYS_ASCII[k < 28 ? k : 27]; }
void keypadFlash(uint8_t times) { for (uint8_t i = 0; i < times; i++) { ledON(); delay(40); ledOFF(); delay(40); } ledON(); }
void keypadHandle(uint8_t k) { (void)k; }
void textPush(char c) { (void)c; }
void textClear() { txTextLen = 0; txText[0] = '\0'; }
void txqPush(uint8_t v) { (void)v; }
bool txqPop(uint8_t &v) { (void)v; return false; }

#endif // KEYPAD_ENABLED
