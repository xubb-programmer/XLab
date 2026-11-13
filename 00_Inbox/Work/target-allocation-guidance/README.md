# 无人机视频与地图目标展示

使用 Node.js 24。首次运行执行 `npm install`，然后执行 `npm start`。浏览器打开 http://127.0.0.1:4177/。修改服务配置后，重启服务再刷新页面。

界面与接入范围见 [当前需求](REQUIREMENTS.md)。

RK3588 Ubuntu 20.04 ARM64 的打包、安装及现场配置见 [离线部署](tools/offline-rk3588/README.md)。

## 切换卫星底图

地图左上角用「切换在线」和「切换离线」切换底图，保留当前视角。页面默认使用离线底图。需要改变页面初始模式时，编辑 `public/scenarios/scenario-default.json` 的顶层 `mapMode`，设置为 `"online"` 或 `"offline"` 后刷新。RK3588 安装后对应配置在 `/var/lib/target-guidance/scenarios/scenario-default.json`。

全球和中国概览读取 `public/offline-satellite/`。厦门细图读取 MapTileGenerator 生成的 Web Mercator XYZ JPEG 数据库 `dist/offline-maps/highres-satellite/tiles.db` 和同目录的 `manifest.json`。细图范围随层级变化，覆盖外显示较低层级或概览影像。概览瓦片缺失时使用 Cesium 随附的 Natural Earth II 底图。

## 无人机

- 名称或呼号：填写 `public/scenarios/scenario-default.json` 的 `uavs` 数组内，每架无人机一条记录，设置唯一 `id` 以及名称 `name` 或呼号 `callsign`。
- 位置：卡片保留经度、纬度和高度。位置输出方式尚未确定，当前不显示假坐标或计划航线。
- 视频：从 `config/video.example.json` 复制出 `config/video.json`，填写 `streams`。每路视频的 `uavId` 填对应无人机的 `id`，并填写该机内唯一的通道 `id`、下拉列表标签 `label` 和 `rtspUrl`。本机需安装 FFmpeg，也可以在 `ffmpegPath` 填写可执行文件的完整路径。点击无人机卡片的「打开视频」显示窗口。有多个通道时，在窗口标题栏切换。地址暂时留空的通道也会列出，选中时显示未配置提示。点击「关闭视频」会隐藏窗口并断开视频。

## 地面火力点

火力点可在「火力整备」中维护，现有服务将其保存到场景文件。地图使用经度、纬度（十进制度）和高程（米）。

## 数据管理

执行 `npm run preview:data`，从「工具 → 数据管理演示」或本机 `127.0.0.1:4196` 打开。演示页支持新增、按名称或编号查询、编辑和删除三类手动录入的资料。数据保存在 `dist/data-preview/records.sqlite`，其中侦察席、火力点和目标各有独立的数据表。

演示库独立运行，使用手动录入的数据。编辑目标时可查看其最近 60 个轨迹坐标。恢复已删除的目标时，可用相同类型和编号重新新增。

## 地图目标

从 `config/targets.example.json` 复制出 `config/targets.json`，填写 EdgeAgent 使用的 `brokerUrl`（`mqtt://` 或 `mqtts://`），按需填写 `username` 和 `password`。目标主题默认为 `edge/v1/devices/plc_udp_dev/data`，设备状态主题默认为 `edge/v1/devices/plc_udp_multicast_dev/data`。

Windows + WSL 开发时，设备状态的 Windows 组播转发器接收 `239.255.0.1:5678`，把原始报文单播送入 WSL EdgeAgent 的 `18082` 端口；EdgeAgent 再通过 MQTT 发布到设备状态主题。启动 EdgeAgent 和网页后，在项目目录另开终端运行 `npm run relay:edgeagent`。RK3588 部署包中的 EdgeAgent 则直接加入该组播并发布到 MQTT。平台只订阅 MQTT。目标报文仍由 EdgeAgent 通过 MQTT 转发。服务解析 ScoutMonitor 033（1501）报文，在地图和右侧列表显示侦察目标与炸点目标。配置和连接状态在顶部显示。没有收到有效报文时，目标列表为空。

同一类型、同一编号的目标始终显示最新位置。现有服务在内存中保留最近 60 个坐标点，连续重复的坐标不追加，地图从第二个不同坐标开始连线。服务重启后重新积累。

向火力点上报时，网页服务通过同一 MQTT Broker 把报文发给 EdgeAgent，由网关通过 TCP 转交外部接收端。阵位的「EdgeAgent 设备 ID」须与网关配置中的 `device_id` 一致，默认使用阵位编号。报文和回执格式见 [部署说明](tools/offline-rk3588/README.md#目标上报协议)。

整批通过阵位容量、坐标和设备绑定校验后开始发送。打开「上报记录」查看逐条结果或重试可发送目标。结果未知的设备暂停发送，操作员核对接收端后，在对应记录中标记「已处理」或「未处理」。
