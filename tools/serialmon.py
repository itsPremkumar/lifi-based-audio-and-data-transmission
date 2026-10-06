#!/usr/bin/env python3
"""
LiFi/KEYPAD - Serial Monitor for both boards (Windows, no extra deps).

Usage:
    python tools/serialmon.py                 # watch both boards, 60 s
    python tools/serialmon.py --seconds 120   # watch longer
    python tools/serialmon.py --tx-only       # only the transmitter
    python tools/serialmon.py --rx-only       # only the receiver
    python tools/serialmon.py --reset         # also reset the boards at start

Ports default to COM3 (receiver) and COM18 (transmitter); override with
--rx-port / --tx-port. Keys typed into this window are NOT forwarded - use the
Serial Monitor in your IDE for sending single-char commands.
"""

import argparse
import sys
import time
import threading
import serial

STOPS = threading.Event()


def reader(port, label, out, lock):
    """Print every complete line that arrives on `port`.

    Reads with a small timeout and buffers bytes itself, because readline() can
    return a single character when the board sends CR and LF in separate writes -
    that produced endless bogus one-character lines on this project.
    """
    try:
        port.reset_input_buffer()
    except Exception:
        pass
    buf = b""
    last = [None]        # last (label, line) printed, for de-duplication
    repeats = 0
    while not STOPS.is_set():
        try:
            chunk = port.read(64)
        except Exception:
            break
        if not chunk:
            time.sleep(0.01)
            continue
        buf += chunk
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            line = raw.decode("utf-8", errors="replace").rstrip("\r")
            if line.strip():
                # Suppress repeats: some USB-serial drivers replay a partial line
                # instead of the full one, which floods the console with fragments.
                key = (label, line)
                if key == last[0]:
                    repeats += 1
                    continue
                if repeats:
                    with lock:
                        out.write("[{}] ... ({} duplicate lines suppressed)\n"
                                  .format(label, repeats))
                        out.flush()
                    repeats = 0
                last[0] = key
                with lock:
                    out.write("[{}] {}\n".format(label, line))
                    out.flush()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rx-port", default="COM3")
    ap.add_argument("--tx-port", default="COM18")
    ap.add_argument("--seconds", type=int, default=60)
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--rx-only", action="store_true")
    ap.add_argument("--tx-only", action="store_true")
    ap.add_argument("--reset", action="store_true")
    args = ap.parse_args()

    ports = []
    if not args.tx_only:
        ports.append((args.rx_port, "RX"))
    if not args.rx_only:
        ports.append((args.tx_port, "TX"))

    opened = []
    for name, label in ports:
        try:
            p = serial.Serial(name, args.baud, timeout=0.1)
            p.dtr = False
            p.rts = False
            opened.append(p)
            print("[{}] {} open at {} baud".format(label, name, args.baud))
        except Exception as exc:
            print("[{}] {} FAILED: {}".format(label, name, exc))
    if not opened:
        print("No ports opened - check the USB cables and close any Serial Monitor.")
        return 1

    if args.reset:
        print("--- resetting boards ---")
        for p in opened:
            try:
                p.dtr = True
                time.sleep(0.06)
                p.dtr = False
            except Exception:
                pass
        time.sleep(0.3)

    print("--- listening for {} s (press keypad keys NOW) ---\n".format(args.seconds))
    threads = []
    lock = threading.Lock()          # both threads print to the same stream
    for p, (name, label) in zip(opened, ports):
        t = threading.Thread(target=reader, args=(p, label, sys.stdout, lock),
                             daemon=True)
        t.start()
        threads.append(t)

    try:
        time.sleep(args.seconds)
    except KeyboardInterrupt:
        print("\n-- interrupted --")
    finally:
        STOPS.set()
        for p in opened:
            try:
                p.close()
            except Exception:
                pass
    print("\n--- done ---")
    return 0


if __name__ == "__main__":
    sys.exit(main())