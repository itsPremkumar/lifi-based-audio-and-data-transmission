# LiFi Based Audio and Data Transmission Using Arduino

Wireless communication through **visible light** — no radio, no Wi-Fi. One Arduino transmits text and
phone audio via a white LED; a second Arduino receives it with an LDR, shows every message on a
16×2 I2C LCD and plays audio on a speaker.
**Once uploaded, both boards run fully automatically** (text beacons + LCD + speaker beeps, no PC needed).

Built with **PlatformIO** · 2× Arduino Uno · No MIC · No buttons · No external resistors.

## Features

- 📡 OOK light link (LED ON = 1, OFF = 0), 25 bps tuned for LDR physics (40 ms/bit)
- 📦 Packet protocol with preamble sync (`AA AA 55`) + XOR checksum
- 🔊 Phone AUX → LED brightness → LDR → PAM8403 speaker audio path
- 🔁 Auto-beacon TX loop + auto-acknowledge RX beeps — standalone on USB chargers
- 📟 16×2 I2C LCD on the receiver: message text, calibration progress, live audio level meter
- 🔧 Serial commands only: `A/D/M/C/?/+/-` + `L/R/X` for the LCD · built-in LDR debugger (`?`)

## Hardware (exact)

| TX board | RX board |
|----------|----------|
| D8 → 220 Ω → 2N2222 → white LED | LDR: A0–GND only (internal pull-up) |
| Phone AUX → 10 µF → A0 (10 k/10 k bias) | D10 → 10 k/10 nF/10 µF → PAM8403 R → 8 Ω speaker |
| D2, D9 free · USB | 16×2 I2C LCD: VCC→5 V, GND→GND, SDA→A4, SCL→A5 |
| | D2, D9 free · USB · 5 V supply |

LDR faces LED at **20–30 cm**, shaded, indoors.

## Quick start

1. Install VS Code + PlatformIO IDE.
2. Open `lifi-transmitter/` → Upload to TX board. Open `lifi-receiver/` → Upload to RX board.
   (Close Serial Monitor before uploading.)
3. Wire the 16×2 I2C LCD to the **receiver** only: VCC→5 V, GND→GND, SDA→A4, SCL→A5,
   backpack jumper **ON**, turn the blue contrast pot until text appears.
4. Power both — TX beacons `HI LIFI / HELLO MINI PROJECT / ECE MINI PROJECT` every 5 s,
   RX prints `Got: ...`, beeps per message, and shows the text on the LCD.
5. Audio: send `A` to both, play phone into TX A0 → speaker plays via light.
6. Diagnose: RX send `?` for live LDR stream, `C` to recalibrate, `+`/`-` gain,
   `L` LCD backlight, `R` LCD re-init, `X` clear the LCD.

## Repo layout

```
lifi-transmitter/   TX firmware (D8 LED beacon + AUX audio)
lifi-receiver/      RX firmware (LDR decode + PAM8403 audio + I2C LCD + auto beeps)
serial-debug/       Standalone HW debugger (LDR stream + beep test)
speaker-test/       Speaker-only beep test
ldr-test/ rx-monitor/ tx-blink/ cal-test/
                    Minimal single-purpose checks used during bring-up
tools/              PC-side scripts (auto test, monitor, send, full check)
DOCUMENTATION.md    Full detailed documentation
```

## Verified results

`HI` → `Got: HI`, `HELLO` → `Got: HELLO` (2/2 PASS), 40 s passive auto-loop PASS,
LDR bright ≈ 975 / dark ≈ 893 @ threshold ≈ 934. See `DOCUMENTATION.md` §8 for full evidence.

## License

MIT — see `LICENSE`.
