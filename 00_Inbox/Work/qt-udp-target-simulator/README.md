# 033（1501）UDP 目标模拟器

使用 Qt Widgets 和 Qt Network 的独立测试程序。在窗口中选择侦察目标或炸点，填写编号、经纬高和目标 IP、端口，再点击「发送 UDP 报文」。发送结果下方显示十六进制数据，可用于抓包对照。

| 报文 | 消息代号 | UDP 数据长度 |
| --- | --- | ---: |
| 侦察目标 | `0x45` | 164 字节 |
| 炸点 | `0x3C` | 78 字节 |

经纬度以十进制度填写，高程以米填写。炸点报文按 TargetTable 源码把高程截断为整数。常规测试使用正整数编号。

「发送单点」使用起点坐标。「发送连续航迹」按设定间隔，从起点到终点依次发送同一编号的插值坐标。点数包含起终点，默认每秒发送一点，共 20 点。发送过程中可点击「停止发送」。

## 运行

在 Qt Creator 中打开 `udp-target-simulator.pro`，选择 Qt 5 Kit 构建运行。

本机已用 WSL Qt 5 构建。在 Windows PowerShell 执行以下命令可打开已构建的窗口。

```powershell
wsl.exe --cd /mnt/d/xubb/XLab/00_Inbox/Work/qt-udp-target-simulator/build-wsl --exec ./udp-target-simulator
```

在 WSL 中用以下命令重新构建并运行。

```bash
cd /mnt/d/xubb/XLab/00_Inbox/Work/qt-udp-target-simulator
mkdir -p build-wsl
cd build-wsl
qmake ../udp-target-simulator.pro
make -j4
./udp-target-simulator
```

## 与 EdgeAgent 联调

1. 在 EdgeAgent 配置中启用 `udp_plc_dev`，并将 `plc_udp_dev` 绑定到该通道，消息格式使用 `json`。
2. 使 EdgeAgent 和网页服务连接到同一 MQTT Broker。网页订阅 `edge/v1/devices/plc_udp_dev/data`。
3. 模拟器默认发往 `127.0.0.1:18080`。若模拟器和 EdgeAgent 分属不同机器或网络环境，在窗口填写可达的 EdgeAgent UDP 地址，并在 EdgeAgent 的 `allowed_peers` 放行模拟器的来源 IP。
4. 点击发送，在网页地图和目标列表检查记录。同一类型、同一编号会更新已有记录。

单独抓包验证 UDP 时，直接用本地 UDP 接收端即可。
