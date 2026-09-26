# EdgeAgent 上位机模拟测试客户端

这是独立于 EdgeAgent 主程序的 Qt5/QMake 可视化测试工具，用于模拟上位机或现场设备，对 UDP、TCP、串口和 CAN 进行统一收发测试。

## 功能

- UDP 单播收发、UDP 组播加入和发送。
- TCP 客户端连接、TCP 服务端监听。
- 串口收发，支持 COM3 和 `/dev/ttyUSB0` 等设备名。
- 经典 CAN 2.0 收发，支持标准帧、扩展帧、RTR、DLC 校验和 `vcan0`/`can0`。
- TCP/串口支持分隔符、固定长度、长度前缀、空闲超时四种分帧方式。
- 发送区支持 ASCII 和 HEX；TCP/串口可以自动追加分隔符、长度前缀或固定长度补零。
- 接收结果显示时间、方向、来源、长度、HEX、ASCII 和 JSON 解析结果。

## Qt Creator 打开方式

在 Qt Creator 中直接打开：

```text
D:/XLab/Dev/Context/EdgeAgentTester/EdgeAgentTester.pro
```

选择 Qt 5 Kit 后构建运行即可。不要打开主工程 `EdgeAgent.pro` 来启动测试工具；两个工程分别生成 `edgeagent` 和 `edgeagent-tester`。

## WSL 编译

CAN 页面依赖 Qt Serial Bus 和 SocketCAN 插件。WSL Ubuntu 未安装时先执行：

```bash
sudo apt-get update
sudo apt-get install -y libqt5serialbus5-dev libqt5serialbus5-plugins can-utils
```

```bash
cd /mnt/d/XLab/Dev/Context/EdgeAgentTester
mkdir -p build-qmake
cd build-qmake
qmake ../EdgeAgentTester.pro
make -j"$(nproc)"
./edgeagent-tester
```

如果 WSL 没有图形桌面，不能直接显示窗口；建议在 Windows Qt Creator 中运行。仅验证程序能启动时可以使用：

```bash
QT_QPA_PLATFORM=offscreen timeout 2s ./edgeagent-tester
```

## 测试 EdgeAgent UDP

1. 启动 EdgeAgent，使用 `config.example.json`。
2. 打开本工具，选择“UDP 数据报”。
3. 本地绑定端口填 `19090`，发送目标填 `127.0.0.1:18080`。
4. 点击“打开接收”。
5. 发送区选择“按 HEX 发送”，输入 `01020304`，点击“发送报文”。
6. EdgeAgent 的 MQTT 上行应出现 `data=01020304`；本工具也会显示收到的 UDP 回包（如果对端有回包）。

## 测试 EdgeAgent TCP

1. 选择“TCP 字节流”。
2. 选择“客户端：连接 EdgeAgent”，目标填 `127.0.0.1:18081`。
3. 分帧方式选择“分隔符”，分隔符填写 `\\n`。
4. 点击“打开接收”，确认状态变为 TCP 已连接。
5. 输入 `01020304` 并选择 HEX 发送；保持“自动封装 TCP/串口分帧”，工具会自动追加换行符。

## 测试串口

1. 准备两个串口或串口回环工具，选择对应端口。
2. 设置与被测设备相同的波特率、数据位、校验、停止位和流控。
3. 选择与设备协议一致的分帧方式。
4. 点击“打开接收”，发送 ASCII 或 HEX 报文。

WSL 使用 USB 串口前，需要先将设备映射为 `/dev/ttyUSB0` 等 Linux 设备，并确保当前用户具有读写权限。

## 测试 CAN

1. 在 WSL 中创建虚拟接口：

```bash
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan 2>/dev/null || true
sudo ip link set up vcan0
```

2. EdgeAgent 使用 `config.can.example.json` 启动。
3. 测试程序选择“CAN 总线帧”，接口填写 `vcan0`，点击“打开接收”。
4. CAN 发送页填写：

```text
CAN ID：0x123
扩展帧：不勾选
RTR：不勾选
DLC：4
数据 HEX：01020304
```

5. 点击“发送 CAN 帧”。EdgeAgent 会收到并向 `edge/v1/devices/can_vcan_dev/data` 发布 `edge.can.v1` JSON；测试程序勾选“接收本程序发出的回环帧”时也会显示回环报文。
