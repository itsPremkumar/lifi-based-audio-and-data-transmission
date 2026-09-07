import serial, time
TX='COM3'; RX='COM18'
def read_for(s, secs):
    t0=time.time(); out=''
    while time.time()-t0 < secs:
        d=s.read(500).decode(errors='ignore')
        if d: out+=d
    return out
print("=== 1. BOOT CHECK (open both, wait 6s) ===")
tx=serial.Serial(TX,9600,timeout=0.5)
rx=serial.Serial(RX,9600,timeout=0.5)
time.sleep(6)
tx_boot=read_for(tx,1); rx_boot=read_for(rx,1)
print("--- TX boot ---"); print(tx_boot[-600:])
print("--- RX boot ---"); print(rx_boot[-800:])
tx_ok = ("TRANSMITTER Ready" in tx_boot) and ("AUTO BEACON" in tx_boot or "MODE = DATA" in tx_boot)
rx_ok = ("RECEIVER Ready" in rx_boot) and ("Calibrated" in rx_boot)
print(f"BOOT: TX={'PASS' if tx_ok else 'FAIL'} RX={'PASS' if rx_ok else 'FAIL'}")
print("\n=== 2. AUTO-LINK CHECK (passive 35s, expect TX Sending + RX Got) ===")
t0=time.time(); txo=''; rxo=''
while time.time()-t0 < 35:
    a=tx.read(400).decode(errors='ignore')
    b=rx.read(400).decode(errors='ignore')
    if a:
        txo+=a
        for l in a.strip().splitlines():
            if l.strip(): print("[TX] "+l.strip()[:100])
    if b:
        rxo+=b
        for l in b.strip().splitlines():
            if l.strip(): print("[RX] "+l.strip()[:100])
tx_send = ("Sending" in txo); rx_got = ("Got:" in rxo)
print(f"LINK: TX-send={'PASS' if tx_send else 'FAIL'} RX-got={'PASS' if rx_got else 'FAIL'}")
print("\n=== 3. RX LDR DEBUG (?) ===")
rx.reset_input_buffer()
rx.write(b'?\n'); rx.flush()
db=read_for(rx,7)
print(db[-1200:])
dbg_ok = ("raw=" in db) and ("variation=" in db or "var=" in db)
print(f"DEBUG: {'PASS' if dbg_ok else 'FAIL'}")
print("\n=== SUMMARY ===")
allok = tx_ok and rx_ok and tx_send and rx_got and dbg_ok
print("ALL PASS - everything working" if allok else "PARTIAL - see FAIL lines above")
print("NOTE: speaker beep was verified earlier; AUDIO path needs phone AUX on TX A0 + send A to both.")
tx.close(); rx.close()
