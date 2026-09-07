import serial, time
TX='COM3'; RX='COM18'; MSG='HI LIFI'
tx=serial.Serial(TX,9600,timeout=0.5)
rx=serial.Serial(RX,9600,timeout=0.5)
print("Waiting 5s for both boots (RX calibrates 2s)...")
time.sleep(5)
tx.reset_input_buffer(); rx.reset_input_buffer()
tx.write(b'D\n'); tx.flush()
time.sleep(0.5)
rx.write(b'D\n'); rx.flush()
time.sleep(2)
print("TX:"); print(tx.read(600).decode(errors='ignore'))
print("RX:"); print(rx.read(1500).decode(errors='ignore'))
print("SENDING: " + MSG)
tx.write((MSG+'\n').encode()); tx.flush()
t0=time.time(); rxo=''
while time.time()-t0 < 20:
    a=tx.read(400).decode(errors='ignore')
    b=rx.read(400).decode(errors='ignore')
    if a.strip(): print("[TX] " + a.strip())
    if b.strip():
        print("[RX] " + b.strip())
        rxo+=b
    if ("Got: "+MSG) in rxo:
        break
print("RESULT: " + ("PASS - RX got '"+MSG+"'" if ("Got: "+MSG) in rxo else "FAIL"))
tx.close(); rx.close()
