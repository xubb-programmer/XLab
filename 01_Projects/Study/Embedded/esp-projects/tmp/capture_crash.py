import serial, time, sys

s = serial.Serial('COM3', 115200, timeout=1, write_timeout=2)
time.sleep(0.5)
s.reset_input_buffer()
data = b''
end = time.time() + 20
while time.time() < end:
    chunk = s.read(4096)
    if chunk:
        data += chunk
s.close()

text = data.decode('utf-8', errors='replace')
lines = text.splitlines()
# 打印崩溃相关的行和上下文
for i, l in enumerate(lines):
    if any(k in l for k in ['abort', 'Guru', 'assert', 'Backtrace', 'rst:', 'panic', 'ELF file', 'Stack', 'Rebooting', 'CORRUPT']):
        start = max(0, i - 3)
        end_i = min(len(lines), i + 15)
        print(f'--- 崩溃点(第{i}行) ---')
        print('\n'.join(lines[start:end_i]))
        break
sys.stdout.flush()
