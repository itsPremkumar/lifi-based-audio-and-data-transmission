# LiFi / Keypad - Serial Monitor (both boards at once)

Windows tool, no pip installs needed (`pyserial` only).

## Quick start

```
python tools/serialmon.py
```

That watches the receiver (COM3) and the transmitter (COM18) for 60 seconds and
prints both, tagged `RX` / `TX`. Press keypad keys while it runs.

## Options

| Command | What it does |
|---|---|
| `python tools/serialmon.py` | both boards, 60 s |
| `python tools/serialmon.py --seconds 120` | watch longer |
| `python tools/serialmon.py --reset` | reset both boards first, then watch |
| `python tools/serialmon.py --tx-only` | transmitter only |
| `python tools/serialmon.py --rx-only` | receiver only |
| `python tools/serialmon.py --rx-port COM7 --tx-port COM12` | different ports |

`Ctrl+C` stops early.

## If it says "FAILED"

Close every Serial Monitor / IDE serial window first - only one program can hold
a COM port. Then check the USB cable.

## Reading the output

Wired keypad mode (`DATA_LINK_WIRED 1`):

| Transmitter (TX) | Receiver (RX) |
|---|---|
| `[TX] wired key '5' sent on D11.` | `[RX] wired key '5'` |

If you see the TX line but no RX line, the two jumper wires are missing or on
the wrong pins.