import re

path = r'D:\XLab\XProjects\esp-projects\src\clock\build\clock.elf'
data = open(path, 'rb').read()

# 提取所有可打印 ASCII 串(长度 >= 4)
strings = re.findall(rb'[\x20-\x7e]{4,}', data)
texts = [s.decode('ascii', errors='ignore') for s in strings]

for kw in ['MyHomeWiFi', 'wifi_cfg', '12345678', 'SSID', 'cmd', 'wifi', '配置']:
    hits = [t for t in texts if kw in t]
    print(f'--- {kw}: {len(hits)} 处 ---')
    for h in hits[:6]:
        print('   ', h[:90])
