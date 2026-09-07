import serial, time
tx=serial.Serial('COM3',9600,timeout=0.5)
rx=serial.Serial('COM18',9600,timeout=0.5)
print("PASSIVE MONITOR 40s - NO sends, boards must auto-loop themselves")
print("Expect TX: Sending.../Done..., RX: Got: ... every ~10s")
t0=time.time()
while time.time()-t0 < 40:
    a=tx.read(400).decode(errors='ignore')
    b=rx.read(400).decode(errors='ignore')
    if a.strip():
        for line in a.strip().splitlines():
            if line.strip(): print("[TX] "+line.strip())
    if b.strip():
        for line in b.strip().splitlines():
            if line.strip(): print("[RX] "+line.strip())
print("MONITOR DONE")
tx.close(); rx.close()
