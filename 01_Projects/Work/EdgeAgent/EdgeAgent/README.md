# EdgeAgent UDP/TCP/Serial/CAN + MQTT transport slice

当前实现使用 Qt 5 和 qmake，完成 UDP、TCP、串口、经典 CAN 接入、字节流分帧、统一事件日志，以及通过 MQTT 3.1.1 上行数据和下行命令。当前范围暂不包含 CAN FD 和 Store-and-Forward。

独立的可视化上位机模拟测试工具位于同级目录 `../EdgeAgentTester/`，可用于 UDP、TCP、串口、CAN 的报文发送、接收、分帧和 JSON 展示，详见 `../EdgeAgentTester/README.md`。

## 已实现

- UDP IPv4 绑定、单播接收、广播发送、组播加入。
- UDP `allowed_peers` 来源 IP/端口校验；端口为 `0` 表示该 IP 的任意源端口。
- UDP 最大报文长度限制。
- TCP Client 和 Server。
- TCP `delimiter`、`fixed_length`、`length_prefix`、`idle_timeout` 四种分帧方式。
- TCP Client 连接超时、指数退避重连、Server 最大连接数和连接级 `connection_id`。
- Qt SerialPort 串口收发、参数配置、四种分帧方式，以及设备断开后的指数退避重连。
- Qt Serial Bus SocketCAN 经典 CAN 2.0 收发，支持标准帧、扩展帧、RTR、DLC 校验、过滤器和断线重连。
- 基于 nlohmann/json 的配置和 MQTT 消息解析。
- 基于 spdlog 的结构化 JSON 日志。
- 基于 libmosquitto 的 MQTT 连接、自动重连、遗嘱、订阅和发布。
- MQTT 下行命令到 UDP/TCP/串口的路由，以及 `command-results` 和 `errors` 反馈。

## 依赖安装（WSL Ubuntu）

```bash
sudo apt-get update
sudo apt-get install -y qtbase5-dev qt5-qmake g++ make \
    libqt5serialport5-dev libqt5serialbus5-dev libqt5serialbus5-plugins \
    nlohmann-json3-dev libspdlog-dev libfmt-dev \
    libmosquitto-dev mosquitto mosquitto-clients can-utils
```

源码使用系统头文件：

```cpp
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <mosquitto.h>
```

qmake 通过 `EdgeAgent.pro` 中的以下配置完成链接：

```qmake
LIBS += -lmosquitto
LIBS += -lspdlog -lfmt
```

## 构建

需要 Qt 5.12+、qmake 和 C++17 编译器：

```bash
mkdir -p build-qmake
cd build-qmake
qmake ../EdgeAgent.pro
make -j"$(nproc)"
```

## 运行

```bash
./build-qmake/edgeagent -c ./config.example.json
```

也可以使用环境变量：

```bash
export EDGEAGENT_CONFIG="$PWD/config.example.json"
./build-qmake/edgeagent
```

`--run-for-ms 5000` 可用于自动退出的冒烟测试。

## MQTT Topic

| Topic | 用途 |
|---|---|
| `edge/v1/gateways/{node_id}/status` | 网关在线/离线状态，保留消息 |
| `edge/v1/devices/{device_id}/data` | UDP/TCP/串口/CAN 上行数据 |
| `edge/v1/devices/{device_id}/commands` | Host 下发命令 |
| `edge/v1/devices/{device_id}/command-results` | 命令执行结果 |
| `edge/v1/devices/{device_id}/errors` | 设备级错误 |

UDP 下行命令示例：

```json
{
  "msg_id": "udp-down-1",
  "target_ip": "127.0.0.1",
  "target_port": 19090,
  "data": "A1B2"
}
```

TCP 下行命令可以使用 JSON 指定连接和 HEX 数据：

```json
{
  "msg_id": "tcp-down-1",
  "connection_id": "server-9",
  "data": "C3D4"
}
```

TCP 也支持把 MQTT payload 作为原始字节直接转发。

## 日志

每条日志是一行 JSON，同时写入控制台和日志文件。默认日志文件为程序目录下的 `logs/edgeagent.log`，最多保留 5 个历史文件，每个文件最大 10 MB。

也可以通过环境变量指定日志文件：

```bash
export EDGEAGENT_LOG_FILE="$PWD/logs/edgeagent.log"
```

普通事件输出到标准输出，错误事件输出到标准错误；日志文件会完整保存两类事件。例如：

`event`、`code`、`schema`、`state` 等字段是稳定的机器标识，保留英文；`message` 字段用于人阅读，使用简体中文。

```json
{
  "event": "rx",
  "message": "收到通道数据",
  "schema": "edge.transport.v1",
  "transport": "udp",
  "len": 4,
  "data": "01020304"
}
```

## 配置

`config.example.json` 是可直接用于本地回环验证的示例，默认使用：

- MQTT：`127.0.0.1:1883`
- UDP：`127.0.0.1:18080`
- TCP Server：`127.0.0.1:18081`，换行分帧
- UDP 组播：`239.255.0.1:18082`，网卡为 `eth0`

串口按现场设备配置独立通道，不在示例配置中默认启用，避免没有接入硬件时启动无效重连。配置示例：

```json
{
  "channel_id": "serial_laser_dev",
  "device_id": "laser_dev",
  "type": "serial",
  "mode": "raw",
  "port_name": "/dev/ttyUSB0",
  "baud_rate": 115200,
  "data_bits": 8,
  "parity": "none",
  "stop_bits": 1,
  "flow_control": "none",
  "reconnect_initial_ms": 1000,
  "reconnect_max_ms": 60000,
  "frame": {
    "kind": "idle_timeout",
    "idle_ms": 20,
    "max_bytes": 4096
  },
  "qos": 0,
  "store_forward": { "enabled": false }
}
```

Windows 串口名示例为 `COM3`。串口上行按配置分帧后以原始字节发布到设备的 `data` Topic；下行可使用 `{"msg_id":"serial-1","data":"A1B2"}` 发送 HEX 字节，也可将 MQTT 正文作为原始字节发送。发送端不会自动追加分隔符。

在 WSL 中使用串口时，设备必须先映射到 WSL 并显示为 `/dev/tty*`；当前用户还需具有设备读写权限（通常加入 `dialout` 组后重新登录）。Windows 原生运行时才使用 `COM3` 这类名称。

CAN v1 使用经典 CAN 2.0 JSON 信封。Linux/WSL 示例配置见 `config.can.example.json`，接口可以是物理 `can0` 或测试用的 `vcan0`。CAN 上行示例：

当前 Qt 5.12 的 SocketCAN 插件负责打开接口、收发报文和过滤；物理 CAN 的比特率及 Bus-Off 自动恢复参数由 Linux 网络接口配置负责。使用物理 `can0` 时，请先按现场硬件配置执行 `ip link` 命令，例如：

```bash
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 500000 restart-ms 100
sudo ip link set can0 up
```

`config.can.example.json` 中的 `bitrate` 和 `restart_on_bus_off` 用于表达通道配置并进行校验；Qt 5.12 SocketCAN 插件本身不提供对应的运行时参数键，因此不会替代上述 Linux 接口配置。

```json
{
  "schema": "edge.can.v1",
  "msg_id": "can-rx-1",
  "can_id": "0x123",
  "extended": false,
  "rtr": false,
  "dlc": 4,
  "ts": 0,
  "replay": false,
  "data": "01020304"
}
```

CAN 下行使用同一设备的 `commands` Topic：

```bash
mosquitto_pub -h 127.0.0.1 -p 1883 \
  -t 'edge/v1/devices/can_vcan_dev/commands' \
  -m '{"schema":"edge.can.v1","msg_id":"can-tx-1","can_id":"0x123","extended":false,"rtr":false,"dlc":4,"data":"01020304"}'
```

在 WSL 中创建虚拟 CAN：

```bash
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan 2>/dev/null || true
sudo ip link set up vcan0
```

启动本地 MQTT Broker：

```bash
mosquitto -p 1883 -d
```

订阅所有 EdgeAgent 消息：

```bash
mosquitto_sub -h 127.0.0.1 -p 1883 -t 'edge/v1/#' -v
```
