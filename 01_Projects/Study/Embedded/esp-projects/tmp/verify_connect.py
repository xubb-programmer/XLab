import serial, time, sys

s = serial.Serial('COM3', 115200, timeout=1, write_timeout=2)
time.sleep(0.5)
s.reset_input_buffer()

data = b''
end = time.time() + 22
while time.time() < end:
    chunk = s.read(4096)
    if chunk:
        data += chunk
s.close()

text = data.decode('utf-8', errors='replace')
keywords = ['NVS', 'X-MCore', 'TestNet', 'IP', 'NTP', '校时', '配置终端', '超时']
lines = text.splitlines()
print('===== 关键日志 =====')
for l in lines:
    if any(k in l for k in keywords):
        print(l)
print('===== 末尾 12 行 =====')
print('\n'.join(lines[-12:]))
sys.stdout.flush()
