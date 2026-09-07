import serial, time, sys
TX="COM3"
RX="COM18"
BAUD=9600
tests=["HI","HELLO"]

def open_port(port):
    return serial.Serial(port, BAUD, timeout=0.5)

def drain(s, secs=1.0, tag=""):
    t0=time.time()
    out=""
    while time.time()-t0 < secs:
        d=s.read(500)
        if d:
            txt=d.decode(errors='ignore')
            out+=txt
            # print live
            sys.stdout.write(txt)
            sys.stdout.flush()
    return out

def send_line(s, line):
    s.write((line+"\n").encode())
    s.flush()

try:
    print(f"Opening {TX} (TX) and {RX} (RX) @ {BAUD}...")
    tx=serial.Serial(TX, BAUD, timeout=0.5)
    rx=serial.Serial(RX, BAUD, timeout=0.5)
    time.sleep(2)
    # drain boot msgs
    print("\n--- TX boot ---")
    drain(tx,1.5)
    print("\n--- RX boot ---")
    drain(rx,1.5)

    print("\n>>> Switching both to DATA mode (D)...")
    send_line(tx,"D")
    send_line(rx,"D")
    time.sleep(1)
    drain(tx,1)
    drain(rx,1)

    print("\n>>> Calibrating RX (C)... align LED facing LDR 10cm NOW!")
    send_line(rx,"C")
    # 2 sec calibration + msgs
    drain(rx,3)
    drain(tx,0.5)

    print("\n>>> Starting light tests. Keep LED facing LDR 10cm, shade sunlight!")
    print(">>> LDR slow mode: 40ms/bit = LED blinks VISIBLE ~5 sec per message")
    print(">>> TX sends, RX should print Got: ...")
    passed=0
    for msg in tests:
        print(f"\n--- TEST: TX -> '{msg}' ---")
        # clear rx buffer
        rx.reset_input_buffer()
        send_line(tx,msg)
        # LDR mode: packet ~3-5 sec, wait longer
        time.sleep(0.5)
        tx_out=drain(tx,4)
        rx_out=drain(rx,8)

        # check if rx_out contains Got: msg
        if f"Got: {msg}" in rx_out or f"Got:{msg}" in rx_out or msg in rx_out:
            print(f"PASS: '{msg}' received")
            passed+=1
        else:
            # also check with strip
            if msg in rx_out:
                print(f"PASS (partial): '{msg}'")
                passed+=1
            else:
                print(f"FAIL: '{msg}' not seen. RX output tail:")
                print(rx_out[-400:])

        time.sleep(1)

    print(f"\n========== RESULT: {passed}/{len(tests)} passed ==========")
    if passed==len(tests):
        print("LiFi DATA LINK WORKING!")
    elif passed>0:
        print("Partial - move closer 10cm, shade LDR, retry")
    else:
        print("No data - check LED facing LDR, 10cm, indoor, calibration")

    print("\n--- Final monitor 5 sec (both ports live) ---")
    t0=time.time()
    while time.time()-t0<5:
        for s, name in [(tx,"TX"),(rx,"RX")]:
            if s.in_waiting:
                d=s.read(s.in_waiting).decode(errors='ignore')
                for line in d.splitlines():
                    if line.strip():
                        print(f"[{name}] {line}")

    tx.close(); rx.close()
    print("\nDone. Boards left in DATA mode. Send 'A' to both for AUDIO.")

except Exception as e:
    print(f"ERROR: {e}")
    import traceback; traceback.print_exc()
    try: tx.close()
    except: pass
    try: rx.close()
    except: pass
