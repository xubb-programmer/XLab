# EdgeAgent 通信代理：文档入口

更新日期：2026-11-09。

EdgeAgent 将 UDP、TCP、串口和经典 CAN 接口双向映射到 MQTT Topic。代理负责连接、收发、字节流分帧、传输元数据及发送回执；设备业务报文由上层服务生成和解释。

```text
现场设备 ↔ UDP/TCP/串口/CAN ↔ EdgeAgent ↔ MQTT Broker ↔ 业务服务
```

## 阅读顺序

| 文档 | 内容 |
|---|---|
| [01 方案大纲与架构](01-方案大纲与架构.md) | 产品职责、通道与 Topic 绑定、数据链路 |
| [02 核心实现细节](02-核心实现细节.md) | 代码模块、分帧、重连、线程和生命周期 |
| [03 配置与接口契约](03-配置与接口契约.md) | 配置版本、字段、Topic、命令和回执 |
| [04 扩展开发与迁移](04-扩展开发与迁移.md) | 构建、配置迁移和新增通信通道 |
| [05 验证与实现边界](05-验证与实现边界.md) | 测试入口、覆盖范围和现场验收边界 |

## 当前能力

- UDP 单播、组播及来源/目标白名单。
- TCP 客户端、服务端、多连接接收和指定连接发送。
- 串口参数配置、收发及断线重开。
- 经典 CAN 标准帧、扩展帧、RTR、过滤和重连。
- TCP/串口的分隔符、固定长度、长度前缀和空闲超时分帧。
- MQTT 命令、数据、发送回执、在线状态与异常断线遗嘱。
- 默认统一 JSON：id、ts、transport、data、meta；元数据共用 endpoint、connection、frame。
- 显式 message_format=legacy 保留旧数据及回执输出，旧下行格式继续兼容。
- v1 通道内声明设备、v2 独立设备绑定；每通道最多一个设备 Topic 绑定。
- 单份 config.json 管理全部通道；通过通道 enabled 开关启停，关闭通道时其设备 Topic 也不启用。

## 示例和测试

- [统一配置 config.json](../EdgeAgent/config.json)：包含 UDP 单播/组播、TCP 客户端/服务端、串口和 CAN。默认仅启用 UDP 单播和 TCP 服务端，其余按需修改参数并设置 enabled=true。
- [测试入口](../EdgeAgent/tests/run_tests.py)
- [独立图形测试工具](../EdgeAgentTester/README.md)

Linux/WSL 中从项目根目录执行：

```bash
python3 EdgeAgent/tests/run_tests.py
```

所有相对路径均以同时包含 `Doc`、`EdgeAgent` 和 `EdgeAgentTester` 的项目根目录为准。

运行时通过 `--config` 指定这份文件；未指定参数和环境变量时，程序查找可执行文件所在目录的 config.json。修改配置需重启，不需要改动多份协议示例文件。
