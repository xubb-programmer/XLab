# EdgeAgent 通信代理

EdgeAgent 将 UDP、TCP、串口和经典 CAN 接口接入 MQTT。它管理现场连接，将收到的数据报或帧发布到设备 Topic，并将 MQTT 命令中的字节提交给对应的现场接口。设备报文的编码、解析和应答关联由业务服务处理。

```text
现场设备 ↔ UDP / TCP / 串口 / CAN ↔ EdgeAgent ↔ MQTT Broker ↔ 业务服务
```

## 默认配置

仓库的 [config.json](../EdgeAgent/config.json) 使用配置 v2，Broker 为 `127.0.0.1:1883`，网关标识为 `edge_box_dev`。默认启用两个通道。

| 通道 | 本地地址 | MQTT 设备 ID | 接收规则 |
| --- | --- | --- | --- |
| UDP 单播 | `0.0.0.0:18080` | `plc_udp_dev` | 每个数据报单独上报，白名单只允许 `127.0.0.1` 的任意端口 |
| TCP 服务端 | `0.0.0.0:18081` | `plc_tcp_server_dev` | 按换行符 `0A` 分帧，最多 8 条连接 |

UDP 组播、TCP 客户端、串口和 CAN 均为关闭的模板。启用前先按环境修改参数，再设置对应通道的 `enabled: true` 并重启。默认数据和发送回执采用 JSON。旧消费者可通过设备绑定中的 `message_format: "legacy"` 继续接收旧格式。

## 文档导航

| 我需要了解 | 文档 |
| --- | --- |
| 职责、模块和数据流 | [01 方案大纲与架构](01-方案大纲与架构.md) |
| 接收分帧、连接恢复和日志 | [02 核心实现细节](02-核心实现细节.md) |
| 配置字段、Topic、命令和兼容格式 | [03 配置与接口契约](03-配置与接口契约.md) |
| 构建、启动、UDP 联调和迁移 | [04 扩展开发与迁移](04-扩展开发与迁移.md) |
| 测试入口、执行结果和验收边界 | [05 验证与实现边界](05-验证与实现边界.md) |

文档中的构建和测试命令从同时包含 `Doc`、`EdgeAgent`、`EdgeAgentTester` 的目录执行。主程序和 [EdgeAgentTester](../EdgeAgentTester/README.md) 是独立的 Qt 工程。测试工具模拟现场端，MQTT 侧由 Broker 和业务客户端完成。
