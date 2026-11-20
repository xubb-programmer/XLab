# 模拟设备管理

在项目根目录用 Node.js 24 启动。

```text
npm start
```

在原网页的无人机栏点击「模拟设备」，或打开 <http://127.0.0.1:4177/?devices=simulation>。Windows 组播转发器接收 `239.255.0.1:5678`，转发到 WSL EdgeAgent 的 `18082` 端口；EdgeAgent 发布至 `edge/v1/devices/device-status-device/data`，网页服务通过 MQTT 订阅该主题。EdgeAgent 的 `device-status-udp` 通道需启用并绑定 `18082`，允许 WSL 网关来源 `172.29.48.1`。来源 IP 在 Windows 转发器和网页服务的 `DEVICE_SIM_SOURCE_IPS` 环境变量中以逗号分隔配置，例如 `192.168.10.130,192.168.10.131`。RK3588 的 EdgeAgent `device-status-udp.allowed_peers` 也需包含每个来源 IP，网页服务 `/etc/intelligent-cooperative-server/web.env`（或 `config/web.env`）配置同一组 `DEVICE_SIM_SOURCE_IPS`。支持协议 2.0 和旧版扁平 JSON。

在线设备自动出现在监测席卡片中。点「同时作为作业单元」会在作业单元列表增加一个独立单元条目，但不会从监测席移除；挂载到点位只建立一个单元通信绑定，视频和监测状态继续有效。点「取消独立作业单元」撤销独立单元标记。两份列表都随 EdgeAgent 转发状态更新。5 秒没有新报文的设备判为离线，设备快照保留离线记录。发送间隔较长时可通过 `DEVICE_SIM_ONLINE_MS` 调整。检测到重复 ID 时用序列号区分，并暂停该 ID 的分类。

无人机卡片的「打开视频」使用原视频窗口。窗口列出广播中的视频源，播放模拟器提供的可用 RTSP 流。FFmpeg 默认从 PATH 读取。Windows 可将 `ffmpeg.exe` 放到 `dist/device-status-simulation/`，也可设置 `DEVICE_SIM_FFMPEG_PATH`。

设备状态报文没有经纬高字段。用卡片的「配置经纬高」手动保存，单位为十进制度和米。分类和坐标保存在 `dist/device-status-simulation/devices.sqlite`，用设备 ID 与序列号关联。该服务只在本机开放模拟清单和手动配置接口。
