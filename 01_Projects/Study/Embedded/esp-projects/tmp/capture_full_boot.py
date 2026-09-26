import serial, time, sys

s = serial.Serial('COM3', 115200, timeout=1, write_timeout=2)
time.sleep(0.3)
s.reset_input_buffer()
data = b''
end = time.time() + 16
while time.time() < end:
    chunk = s.read(4096)
    if chunk:
        data += chunk
s.close()
text = data.decode('utf-8', errors='replace')
lines = text.splitlines()
for l in lines:
    print(l)
sys.stdout.flush()
