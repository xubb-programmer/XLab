import serial, time, sys

s = serial.Serial('COM3', 115200, timeout=1, write_timeout=2)
time.sleep(0.5)
s.reset_input_buffer()

def dump(label, data):
    print(label)
    print(data.decode('utf-8', errors='replace')[-1000:])
    sys.stdout.flush()

# 1. 读取启动输出
data = b''
end = time.time() + 6
while time.time() < end:
    chunk = s.read(4096)
    if chunk:
        data += chunk
dump('--- 启动输出 ---', data)

# 2. 发送 help 命令,验证输入解析
try:
    s.write(b'help\r')
except Exception as e:
    dump('--- help 写入异常 ---', repr(e).encode())
time.sleep(0.3)
data = b''
end = time.time() + 3
while time.time() < end:
    chunk = s.read(4096)
    if chunk:
        data += chunk
dump('--- help 输出 ---', data)

# 3. 发送 wifi 命令保存配置
try:
    s.write(b'wifi TestNet 12345678\r')
except Exception as e:
    dump('--- wifi 写入异常 ---', repr(e).encode())
time.sleep(0.3)
data = b''
end = time.time() + 3
while time.time() < end:
    chunk = s.read(4096)
    if chunk:
        data += chunk
dump('--- wifi 保存输出 ---', data)
s.close()
