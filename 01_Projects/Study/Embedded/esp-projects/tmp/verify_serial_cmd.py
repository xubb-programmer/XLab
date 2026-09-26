import serial, time, sys

s = serial.Serial('COM3', 115200, timeout=1, write_timeout=2)
time.sleep(0.5)
s.reset_input_buffer()

def dump(label, data):
    print(label)
    print(data.decode('utf-8', errors='replace')[-1600:])
    sys.stdout.flush()

# 1. 读 10 秒启动输出:检查 NVS 配置读取 + 配置终端提示 + 无看门狗
data = b''
end = time.time() + 10
while time.time() < end:
    chunk = s.read(4096)
    if chunk:
        data += chunk
t = data.decode('utf-8', errors='replace')
dump('--- 启动输出(关键行) ---', ('\n'.join([l for l in t.splitlines() if ('NVS' in l or '配置终端' in l or 'task_wdt' in l or '尝试连接' in l or 'WiFi' in l)])).encode())

# 2. 发送 wifi 查询命令
try:
    s.write(b'wifi\r')
except Exception as e:
    dump('--- 写入异常 ---', repr(e).encode())
time.sleep(0.3)
data = b''
end = time.time() + 3
while time.time() < end:
    chunk = s.read(4096)
    if chunk:
        data += chunk
dump('--- wifi 查询 ---', data)

# 3. 空转 12 秒,检查看门狗
data = b''
end = time.time() + 12
while time.time() < end:
    chunk = s.read(4096)
    if chunk:
        data += chunk
t = data.decode('utf-8', errors='replace')
has_wdt = 'task_wdt' in t
dump('--- 空转 12 秒结果 ---', ('发现看门狗超时!' if has_wdt else '正常,无看门狗超时').encode())
s.close()
