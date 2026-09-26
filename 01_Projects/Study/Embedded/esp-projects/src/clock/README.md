# 网络时钟(clock)

ESP32-S3 + 320x240 ST7789 IPS 屏 实现的网络时钟。

## 功能

- 连接 WiFi 后通过 NTP(阿里云)自动校时,显示北京时间
- 屏幕显示:纯黑背景 + 7 段数码管时分秒,每秒刷新,不闪屏
- 未配置 WiFi 时自动使用本地时间(从设定初始值走秒)

## 使用前必改

打开 `main/main.c`,把文件顶部的 `WIFI_SSID` 和 `WIFI_PASSWORD` 改成你自己的
WiFi 账号密码,然后编译烧录即可联网校时。

## 目录说明

```
clock/
├── main/
│   └── main.c          # 主程序(带详细中文注释)
├── components/BSP/     # 正点原子官方外设驱动(屏幕/SPI/I2C/IO扩展/LED)
├── CMakeLists.txt
├── partitions-16MiB.csv
└── sdkconfig.defaults
```
