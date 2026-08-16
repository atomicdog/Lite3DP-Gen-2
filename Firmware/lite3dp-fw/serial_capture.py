import serial, time, sys

port = sys.argv[1] if len(sys.argv) > 1 else 'COM4'
duration = float(sys.argv[2]) if len(sys.argv) > 2 else 8
s = serial.Serial(port, 115200, timeout=1)
s.dtr = False
s.rts = True
time.sleep(0.1)
s.rts = False
time.sleep(0.1)
s.dtr = False
end = time.time() + duration
data = b''
while time.time() < end:
    n = s.in_waiting
    if n:
        data += s.read(n)
    else:
        time.sleep(0.05)
s.close()
print(f"Got {len(data)} bytes", file=sys.stderr)
sys.stdout.buffer.write(data)
