# LiFi Based Audio and Data Transmission Using Arduino — Full Project Documentation

> PlatformIO (Arduino framework) implementation · 2× Arduino Uno · No MIC · No buttons · No external resistors
> Serial control only · Fully automatic beacon loop once uploaded

---

## 1. Abstract

This project demonstrates **Light Fidelity (LiFi)** — wireless communication using visible light instead of
radio waves. Two Arduino Uno boards form a complete optical link: the **transmitter** converts phone audio
(AUX) and text messages into light signals emitted by a high-brightness white LED, and the **receiver**
captures those signals with an LDR, decodes them back into text on the Serial Monitor, and plays audio
through a PAM8403 amplifier + speaker.

Once uploaded, the system runs **fully automatically**: the transmitter loops text beacons
(`HI LIFI` → `HELLO MINI PROJECT` → `ECE MINI PROJECT`, 5 s gap) and the receiver prints every received
message and plays a confirmation beep on the speaker — using only the LED, LDR and speaker hardware.

---

## 2. Objectives

1. Build a working LiFi link that transmits **both audio and digital data** through visible light.
2. Demonstrate communication that is **secure** (light cannot pass through walls), **interference-free**
   (no RF), and **energy-efficient** (reuses LED lighting).
3. Implement everything on low-cost Arduino Uno hardware with PlatformIO, with **no extra modules**:
   no MIC module, no push buttons, no external resistors.
4. Make the link **self-running**: after upload, TX beacons and RX acknowledges forever without a PC.

---

## 3. Background Theory

### 3.1 LiFi / Visible Light Communication (VLC)

LiFi modulates the intensity of an LED faster than the human eye can perceive. A photosensor on the
receiver converts brightness variations back into electrical signals. Base concept: H. Haas,
"High-Speed Wireless Networking Using Visible Light Communication (Li-Fi)", IEEE.

### 3.2 Modulation used here — OOK (On-Off Keying)

- LED **ON (bright) = bit 1**, LED **OFF (dark) = bit 0**.
- Each byte is framed like a UART frame: **START (0) + 8 data bits (LSB first) + STOP (1)**.
- Each packet adds sync + error check:
  `[0xAA][0xAA][0x55 sync][LEN][PAYLOAD 1–32 bytes][CHECKSUM = XOR of LEN+PAYLOAD][0x0A]`.
- The `0xAA 0xAA 0x55` preamble lets the receiver lock its threshold before real data arrives.

### 3.3 Why 40 ms per bit (25 bps)?

An LDR is slow — its resistance needs ~30 ms to settle after a light change. So one bit lasts **40 ms**
(`BIT_PERIOD 40 ms`), giving a reliable 25 bps text link. (A fast photodiode could use ~2 ms/bit, but
the LDR needs the slow rate.) One `HELLO` packet (~11 bytes × 10 bits × 40 ms) takes ~4.4 s on air —
the LED visibly blinks during transmission, which is ideal for classroom demonstration.

### 3.4 Audio path

- **TX:** phone AUX voltage on A0 (≈2.5 V bias) is sampled, DC-removed, and reproduced as LED
  brightness using 4-bit software PWM on D8 (D8 has no hardware PWM; 16 brightness levels at ~4 kHz
  sampling — DATA is unaffected and bit-perfect).
- **RX:** the LDR signal on A0 is sampled, ambient baseline is tracked with a slow moving average,
  the difference is amplified (`RX_GAIN 3.0`, limited to ±90) and output as 31.25 kHz Timer1 PWM on
  D10 → RC filter → PAM8403 (24 dB fixed gain) → speaker.

---

## 4. Hardware Requirements (exact build)

| Qty | Component | Used for |
|-----|-----------|----------|
| 2 | Arduino Uno (COM3 = TX, COM18 = RX on dev PC) | Transmitter + receiver |
| 1 | High-brightness white LED (5 mm) | LiFi light source (TX D8 via transistor) |
| 1 | 2N2222 NPN transistor (IRFZ44N + heatsink if 1 W LED) | LED driver — never drive LED from pin directly |
| 1 | LDR (photoresistor), 2 pins, no polarity | Light sensor (RX A0–GND, internal pull-up, no resistor) |
| 1 | PAM8403 Class-D amplifier board | Speaker driver (RX D10 → R) |
| 1 | 8 Ω speaker (either wire to R+/R−, polarity free) | Audio output |
| 1 | 3.5 mm AUX cable + 10 µF cap + 2× 10 k (TX A0 bias network) | Phone/laptop audio input (NO MIC module) |
| 2 | USB cables (data) | Upload + Serial Monitor @ 9600 |
| 1 | 5 V supply (Uno 5V OK for demo; external 5 V 1–2 A for loud volume) | PAM8403 power |
| — | Breadboard + jumper wires | Assembly |

**Deliberately NOT used:** MIC module, push buttons (serial commands instead), external resistors
for the LDR (Arduino internal pull-up ~34 kΩ is used).

---

## 5. Wiring (final, verified)

### 5.1 Transmitter (Uno #1) — LED on D8, D2/D9 free

```
D8 ---[220 Ω]---> Base of 2N2222
2N2222 Emitter ---> GND
2N2222 Collector ---> LED Cathode (-)
LED Anode (+) ---[100 Ω 1 W]---> 5V
Phone 3.5mm GND ---> Arduino GND
Phone L+R joined ---> 10 µF cap (-) side
10 µF cap (+) ---> A0
A0 ---> 10 k ---> 5V   } 2.5 V bias divider
A0 ---> 10 k ---> GND  }
Phone volume 50–70 %
D2 = FREE, D9 = FREE (high-Z), USB ---> PC
```

### 5.2 Receiver (Uno #2) — LDR on A0, PAM8403 on D10, D2/D9 free

```
LDR leg 1 ---> A0
LDR leg 2 ---> GND        (nothing to 5V; INPUT_PULLUP used)
D10 ---> 10 k ---> node ---> 10 µF (+ to D10 side) ---> PAM8403 R-IN
                         node ---> 10 nF ---> GND
Arduino GND ---> PAM8403 GND (must be common)
Uno 5V ---> PAM8403 VCC  (demo volume; external 5 V 2 A + joined GNDs for loud)
PAM8403 L-IN ---> empty
PAM8403 R+ ---> speaker wire 1
PAM8403 R- ---> speaker wire 2   (either way; L+/L- empty)
Knob at middle. LDR faces TX LED at 20–30 cm, shaded with a tube, indoors.
```

> 5 cm saturates the LDR (variation ≈ 3 ADC counts — link fails). 10–30 cm with shading is the
> working window. Sunlight on the sensor kills the link.

---

## 6. Software Design

### 6.1 Repository layout

```
lifi-transmitter/      PlatformIO project for TX board (src/main.cpp, platformio.ini)
lifi-receiver/         PlatformIO project for RX board (src/main.cpp, platformio.ini)
serial-debug/          Standalone debugger: LDR stream + speaker beep (upload to RX to verify HW)
speaker-test/          Speaker-only beep test (D10 → PAM8403)
ldr-test/  rx-monitor/ tx-blink/ cal-test/
                       Minimal single-purpose hardware checks used during bring-up
tools/                 PC-side helpers: auto link test, passive monitor, one-shot send, full check
DOCUMENTATION.md       This file
README.md              Quick-start for GitHub
```

### 6.2 Transmitter firmware (`lifi-transmitter/src/main.cpp`)

- **Pins:** `LED_PIN 8`, `AUX_IN A0`, D2/D9 free (high-Z). No button, no MIC code.
- **Safe timing:** `bitDelay() = delay(40)`, `bitDelayHalf() = delay(20)` — `delayMicroseconds()`
  is inaccurate above 16383 µs on AVR, so 40 ms LDR bits must use `delay()`.
- **Boot:** straight into `MODE_DATA` (auto-beacon). Serial banner + command list @ 9600.
- **Beacon loop (`dataLoop`):** sends `HI LIFI` → `HELLO MINI PROJECT` → `ECE MINI PROJECT`,
  5 s idle (LED ON) between messages, 3 s after boot — forever, no PC needed.
- **Manual override:** typing a line in DATA mode sends it immediately and resets the beacon timer.
- **Serial commands:** `A` = audio, `D` = data, `M` = toggle.
- **Audio (`audioLoop`):** 120-sample batches, bias tracker (`audioBias += 0.002·(raw−bias)`),
  gain 2.0 around center 128, 4-bit software PWM (16 levels × 8 µs ≈ 128 µs/sample).
- **OOM framing (`sendByteOOK`):** START(OFF) + 8 bits LSB-first + STOP(ON) + half-bit gap.

### 6.3 Receiver firmware (`lifi-receiver/src/main.cpp`)

- **Pins:** `SENSOR_PIN A0` (LDR to GND, `INPUT_PULLUP`), `AUDIO_OUT 10` (Timer1 OC1B,
  31.25 kHz), D2/D9 free. No button code.
- **Sensor reading:** `readSensor() = 1023 − analogRead(A0)` so bright = HIGH.
  (An earlier revision called itself recursively and crash-looped — fixed.)
- **Threshold:** auto-calibrated 2 s at boot (`High/Low/Threshold` printed), re-locked from every
  preamble; defaults for no-resistor wiring: High ≈ 985, Low ≈ 875, Threshold ≈ 930.
- **Boot:** startup double-beep on speaker (standalone proof), calibrate, straight into
  `MODE_DATA` listen loop. DATA mode holds PWM at CENTER = silence (mute by design).
- **Listen loop (`dataLoop` → `receivePacket`):** 15 s preamble hunt (`AA AA 55`), 3 s per-byte
  timeouts (slow-LDR safe), checksum verification, prints `Got: <text>`, then `ackBeep()` —
  1200 Hz + 1600 Hz double-beep on the speaker, fully automatic (no phone/PC needed).
- **Audio (`audioLoop`):** 400-sample batches, baseline `+= 0.002·(raw−baseline)`,
  gain 3.0 with ±90 limiter into PAM8403; `fadeToCenter()` ramps PWM on mode change (no POP).
- **Serial commands:** `A/D/M` modes, `C` recalibrate, `?` 5 s LDR debug stream
  (`raw/light/thr BRIGHT|DARK` + variation verdict), `+`/`−` live gain trim.

### 6.4 PC tools (`tools/`)

| Script | Purpose |
|--------|---------|
| `auto_lifi_test.py` | Auto test: boots, DATA mode, calibrate, sends `HI` + `HELLO`, reports PASS/FAIL |
| `auto_monitor.py` | Passive 40 s monitor — proves boards loop with zero manual sends |
| `send_msg.py` | One-shot addressed send (`HI LIFI`) with TX/RX transcript + PASS/FAIL |
| `check_all.py` | Full health check: boot + link + LDR debug with summary |

### 6.5 Diagnostic firmwares

`serial-debug/` (LDR stream + `B` beep + `S` stats), `speaker-test/` (1 kHz/500 Hz beeps),
`ldr-test/`, `rx-monitor/`, `tx-blink/` (500 ms D8 blink), `cal-test/` — used during bring-up to
isolate LED vs LDR vs speaker faults one at a time.

---

## 7. Build & Upload (PlatformIO)

1. VS Code + **PlatformIO IDE** extension.
2. Open folder `lifi-transmitter` → select env `uno` → set upload port to TX board → **Upload**.
   Repeat with `lifi-receiver` for the RX board. (Close Serial Monitor before uploading —
   an open monitor holds the COM port and `avrdude: stk500_getsync() not in sync` fails.)
3. Dev-PC mapping used here: **COM3 = TX, COM18 = RX**. Boards: `uno`, `atmelavr@5.3.0`,
   9600 baud monitor, ~6.6 kB TX / ~10.3 kB RX flash.

---

## 8. Testing & Measured Results

All checks run over real serial ports; representative evidence:

| Test | Method | Result |
|------|--------|--------|
| LDR connected? | `ldr-test` stream | Disconnected: `raw=1023 light=0` constant. Connected: `raw≈104 light≈919` |
| Light tracking? | `rx-monitor` + TX 500 ms blink | `light` oscillates 873 ↔ 984 in sync with LED (variation ≈ 110) |
| Saturation limit | `cal-test` at 5 cm | `mn=986 mx=989` (variation 3) → link impossible that close; 20–30 cm required |
| Speaker HW | `speaker-test` | 1 kHz/500 Hz beeps heard — PASS |
| RX boot (fixed) | Serial boot log | `Calibrated High=975 Low=893 Threshold=934`, `MODE = DATA` — PASS |
| Auto link | `auto_lifi_test.py` | `TX Sending HI` → `RX Got: HI`; `HELLO` → `Got: HELLO` — **2/2 PASS** |
| One-shot send | `send_msg.py` | `Got: HI LIFI` — **PASS** |
| Passive autonomy | `auto_monitor.py` 40 s, zero sends | TX `Sending/Done` + RX `Got:` repeating — **PASS** |
| Post-fix loop | `check_all.py` | TX-send PASS, RX-got PASS, LDR debug PASS |

**Current behavior on power-up (verified):** TX beacons text every 5 s (visible LED blinks);
RX prints `Got:` per message and double-beeps the speaker per message; RX power-up double-beep.

---

## 9. Troubleshooting

| Symptom | Cause → Fix |
|---------|-------------|
| `raw=1023` constant | LDR open circuit → legs fully into A0 + GND (skip breadboard for test) |
| RX stuck at `Calibrating...` / reboot loop | Old `readSensor()` recursion bug → fixed in current code |
| `Checksum error` / no `Got:` | Too close (saturates) or too far/sunlight → 20–30 cm, align, shade tube, send `C` |
| Mostly `DARK` at threshold | Ambient drifted → send `C` to recalibrate |
| `avrdude stk500_getsync not in sync` | Serial Monitor holding port → close monitor, wait 3 s, retry upload |
| Speaker silent in DATA | By design (amp muted) → send `A` to both + play phone into TX A0 |
| Speaker silent in AUDIO | Knob low / gain low / no source → knob middle, `+` gain, phone 60 %, check D10→R, GND→GND, 5V→VCC |
| POP on mode switch | Missing DC block → keep 10 µF series cap; code `fadeToCenter()` already ramps |
| Uno resets at loud volume | USB 500 mA limit → external 5 V 2 A for PAM8403, join GNDs |

---

## 10. Limitations & Future Scope

- **Rate:** 25 bps suits text beacons, not files/video — a photodiode + comparator front-end
  would allow kbps rates with the same protocol.
- **Range:** LDR + room light limits practical range to tens of cm; lenses, shades and brighter
  LED drivers extend it.
- **Half-duplex broadcast:** one optical channel, no addressing/encryption — add node IDs and
  AES+XOR framing for multi-node use; add solar-cell energy harvesting to match the
  "existing lighting infrastructure" objective.

---

## 11. References

1. H. Haas, "High-Speed Wireless Networking Using Visible Light Communication (Li-Fi)", IEEE.
2. Singh, Kumar, Sharma, "Li-Fi Based Wireless Audio and Data Transmission Using Arduino",
   IJERT, 2023.
3. IEEE Xplore — LiFi / VLC research papers.
4. Arduino Uno documentation (ATmega328P, Timer1 PWM, ADC).
5. PAM8403 datasheet (3 W Class-D, 24 dB fixed gain).

---

*Serial-monitor transcript evidence for every claim above was captured during bring-up with the
scripts in `tools/`.*
