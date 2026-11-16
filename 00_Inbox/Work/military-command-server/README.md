# 无人机视频与地图目标展示

使用 Node.js 24。首次运行执行 `npm install`，然后执行 `npm start`。浏览器打开 http://127.0.0.1:4177/。修改服务配置后，重启服务再刷新页面。

界面与接入范围见 [当前需求](REQUIREMENTS.md)。

## 工程结构

- `src/` 存放浏览器入口、样式和 JavaScript。地图图层在 `src/js/layers/`，界面模块在 `src/js/ui/`。
- `server/` 存放 Node.js HTTP 入口、设备通信和数据存储模块。
- `public/` 存放场景和浏览器可读取的静态资源。
- `config/` 存放本地配置和示例。
- `tools/` 存放开发辅助脚本和 RK3588 离线部署工具。
- `dist/` 存放生成的离线地图和本地运行数据。

## 在新 RK3588 上离线部署

目标板需运行 Ubuntu 20.04 ARM64 或 Debian 11 ARM64，并使用 systemd。先在目标板确认发行版和架构。

```bash
. /etc/os-release
printf '%s %s\n' "$ID" "$VERSION_ID"
dpkg --print-architecture
```

从现有开发板 `ztl@192.168.0.201` 的 `/opt/jw/military-command-server/` 获取对应部署包及同名的 `.sha256` 文件，用 U 盘或局域网拷贝到新板的同一目录。

| 系统检查结果 | 部署包 |
| --- | --- |
| `ubuntu 20.04`、`arm64` | `military-command-server-ubuntu20.04-arm64.tar.gz` |
| `debian 11`、`arm64` | `military-command-server-debian11-arm64.tar.gz` |

进入文件所在目录，按系统设置 `package` 并执行。安装脚本会核对发行版、版本和架构，使用包内 APT 仓库安装依赖。

```bash
package=military-command-server-ubuntu20.04-arm64.tar.gz
# Debian 11 使用 package=military-command-server-debian11-arm64.tar.gz
sha256sum -c "$package.sha256" &&
tar --touch -xzf "$package" &&
cd military-command-server-offline &&
sudo bash install.sh
```

安装后检查服务。三项应分别显示 `enabled` 和 `active`，目标接口应返回 `"state":"subscribed"`。

```bash
systemctl is-enabled mosquitto server-edge military-command-server
systemctl is-active mosquitto server-edge military-command-server
curl -fsS http://192.168.0.201:4177/api/targets
```

在已安装 Chromium 的板卡桌面终端打开指挥界面。

```bash
/opt/jw/military-command-server/current/launch-military-command.sh
```

现场接入时，在 `/etc/military-command-server/edgeagent.json` 配置 UDP 发送端白名单、组播来源及 `fire-01-tcp` 的接收地址和端口，修改后重启 `server-edge.service`。在同目录的 `video.json` 配置 RTSP，修改后重启 `military-command-server.service`。「即时通讯」和「侦察车智能助手」分别依赖板上已有的 LANChat 和语音助手服务。

Ubuntu 包已通过完整性校验，原生 Ubuntu 20.04 ARM64 板上的断网首装待现场验证。打包及更多现场配置见 [离线部署](tools/offline-rk3588/README.md)。

## RK3588 配置文件清单

当前 Ubuntu 板上有 **8 份生效的业务配置**。网页后端 5 份，EdgeAgent 1 份，Mosquitto 2 份。另有 **3 份 systemd 服务单元**，含启动配置时合计 **11 份**。以下路径均以板上实际安装为准。

| 服务 | 文件 | 用法与生效方式 |
| --- | --- | --- |
| 网页 | `/etc/military-command-server/web.env` | `MILITARY_COMMAND_SERVER_BIND_HOST=192.168.0.201` 和 `PORT=4177` 控制 HTTP 监听。换板时填实际已配置的静态 IP。`DEVICE_SIM_SOURCE_IPS` 用逗号分隔组播来源 IP，与 EdgeAgent 的来源白名单对应。修改后重启网页服务。 |
| 网页 | `/etc/military-command-server/targets.json` | `brokerUrl` 指向 Mosquitto，当前为 `mqtt://127.0.0.1:1883`。`topic` 订阅目标，`deviceStatusTopic` 订阅设备状态。启用认证时填写 `username`、`password`；`deviceStatusRelayIp` 用于转发来源识别。修改后重启网页服务。 |
| 网页 | `/etc/military-command-server/video.json` | 在 `streams` 中用 `uavId` 关联设备，每路设置 `id`、`label`、`rtspUrl`。可指定 `ffmpegPath` 和 `rtspTimeoutOption`；离线包使用 `stimeout`。修改后重启网页服务。 |
| 网页 | `/etc/military-command-server/reporting.json` | `defaultEndpoint.timeoutMs` 设置默认上报等待时间，`endpoints` 按阵位编号（如 `FIRE-01`）覆盖。TCP 地址在 EdgeAgent 中配置。后端初始化上报处理器时读取，修改后重启网页服务。 |
| 网页 | `/var/lib/military-command-server/scenarios/scenario-default.json` | 保存地图模式、场景中心、侦察平台及火力点。阵位通过 `deviceId` 绑定 EdgeAgent 上报设备。优先用页面管理阵位及挂载；离线手工编辑时先停网页服务，完成后再启动并刷新页面。 |
| EdgeAgent | `/etc/military-command-server/edgeagent.json` | `broker` 配置 MQTT；`channels` 定义连接；`devices` 用 `device_id` 绑定 `channel_id`。目标通道 `target-input-udp` 监听 UDP 28080，状态通道 `device-status-udp` 加入 `239.255.0.1:5678`，两者的 `allowed_peers` 用 `IP:0` 放行指定来源的任意源端口。上报通道 `fire-01-tcp` 的 `remote_host` 和 `remote_port` 填现场接收端，对应设备 `fire-01-device`。修改后重启 `server-edge`。 |
| Mosquitto | `/etc/mosquitto/mosquitto.conf` | 服务用 `-c` 加载该主配置。当前启用持久化，数据目录为 `/var/lib/mosquitto/`，并通过 `include_dir` 加载 `/etc/mosquitto/conf.d/` 下的 `.conf`。 |
| Mosquitto | `/etc/mosquitto/conf.d/military-command-server-local.conf` | 当前配置为 `listener 1883 127.0.0.1` 和 `allow_anonymous true`。这是板内 MQTT 监听，与网页端口 4177 分开。改 MQTT 监听或认证时同步更新两个 MQTT 客户端配置，先重启 Mosquitto，再重启 EdgeAgent 和网页服务。 |

systemd 启动配置包括 `/etc/systemd/system/military-command-server.service`（加载 `web.env`，指定各 JSON 和 SQLite 路径）、`/etc/systemd/system/server-edge.service`（指定网关 JSON、工作目录和日志）和 `/lib/systemd/system/mosquitto.service`（发行版提供，指定 MQTT 主配置）。修改单元后执行 `sudo systemctl daemon-reload`，再重启对应服务。

已清理板上 4 份历史备份：`edgeagent.json.bak`、`edgeagent.json.bak-20261114-1406`、`web.env.bak-20261114-1405` 和 `web.env.bak-multi-ip-20261114`。这些文件原位于 `/etc/military-command-server/`，服务启动时不加载备份。Mosquitto 的 `aclfile.example`、`pskfile.example`、`pwfile.example` 和 `conf.d/README` 也是示例或说明。离线地图的 `manifest.json` 和 `tiles.db` 属于地图资源；运行 SQLite 库属于业务数据。

源码中的配置来源为 `config/targets.example.json`、`config/video.example.json`、`config/reporting.example.json`、`public/scenarios/scenario-default.json` 和 `tools/offline-rk3588/edgeagent.json`。本地 `npm start` 读取 `config/targets.json`、`config/video.json`、`config/reporting.json`，前两份可从示例复制。离线包在 `config/` 和 `services/` 提供模板；`web.env` 由安装器在首次安装时生成，Mosquitto 主配置由系统包提供。升级沿用已有现场配置。

目标上报 `28080/UDP` 和设备状态组播 `239.255.0.1:5678` 默认接收 `192.168.0.0/24` 与 `192.168.10.0/24` 两个局域网网段的设备。打包时展开网段，首次安装同步生成网页的组播来源配置。已有部署调整组播来源时，请同时更新 EdgeAgent 的 `device-status-udp.allowed_peers` 和 `web.env` 的 `DEVICE_SIM_SOURCE_IPS`。

使用 `192.168.0.102:5567/TCP` 接收目标上报时，在 EdgeAgent 配置 `fire-02-tcp` 和 `fire-02-device`，并把对应场景阵位的 `deviceId` 设为 `fire-02-device`。两条 TCP 通道按阵位绑定分别使用。已有部署可参考离线包模板，将这条通道及其设备绑定合入生效的 `edgeagent.json`，再重启 `server-edge`。

配置完成后，根据修改范围执行：

```bash
sudo systemctl restart mosquitto server-edge military-command-server
systemctl is-active mosquitto server-edge military-command-server
curl -fsS http://192.168.0.201:4177/api/targets
```

目标接口应返回 `"state":"subscribed"`。主 README 在离线包内名为 `PROJECT-README.md`，板上可在 `/opt/jw/military-command-server/README.md` 查阅。

## 切换卫星底图

地图左上角用「切换在线」和「切换离线」切换底图，保留当前视角。页面默认使用离线底图。需要改变页面初始模式时，编辑 `public/scenarios/scenario-default.json` 的顶层 `mapMode`，设置为 `"online"` 或 `"offline"` 后刷新。RK3588 安装后对应配置在 `/var/lib/military-command-server/scenarios/scenario-default.json`。

全球和中国概览读取 `public/offline-satellite/`。厦门、朱日和和赤峰细图合成为 `dist/offline-maps/combined-theaters/`，网页按各战区图层范围和逐行瓦片覆盖加载同一份 Web Mercator XYZ JPEG 数据库。原始地图仍分别保存在 `highres-satellite/` 与 `zhurihe-chifeng/`，合并脚本从 SQLite 一致性快照生成图集；覆盖以外显示概览影像。概览瓦片缺失时使用 Cesium 随附的 Natural Earth II 底图。

## 无人机

- 名称或呼号：填写 `public/scenarios/scenario-default.json` 的 `uavs` 数组内，每架无人机一条记录，设置唯一 `id` 以及名称 `name` 或呼号 `callsign`。
- 位置：卡片保留经度、纬度和高度。位置输出方式尚未确定，当前不显示假坐标或计划航线。
- 视频：从 `config/video.example.json` 复制出 `config/video.json`，填写 `streams`。每路视频的 `uavId` 填对应无人机的 `id`，并填写该机内唯一的通道 `id`、下拉列表标签 `label` 和 `rtspUrl`。本机需安装 FFmpeg，也可以在 `ffmpegPath` 填写可执行文件的完整路径。点击无人机卡片的「打开视频」显示窗口。有多个通道时，在窗口标题栏切换。地址暂时留空的通道也会列出，选中时显示未配置提示。点击「关闭视频」会隐藏窗口并断开视频。

## 地面火力点

火力点可在「火力整备」中维护，现有服务将其保存到场景文件。地图使用经度、纬度（十进制度）和高程（米）。

## 数据管理

执行 `npm start`，从主界面的「设置 → 数据管理」打开。页面支持新增、按名称或编号查询、编辑和删除三类手动录入的资料。Windows 开发环境沿用原有的 `dist/data-preview/records.sqlite`，离线板端保存在 `/var/lib/military-command-server/management/records.sqlite`。

数据管理按来源显示设备上报、场景配置和手工记录，每条记录均可从这里删除。设备快照删除后，设备下一次有效心跳会重新入库，沿用原有名称、角色和坐标。实时目标删除后从地图和列表移除，再次上报时重新开始记录轨迹。手工记录可编辑，已删除的手工目标可用相同类型和编号重新录入。

删除已挂载的设备前，在数据管理页先点「解绑」。删除场景阵位会解除该阵位的挂载，已保存的上报历史仍可查阅。已删除阵位上的旧指派无法按原阵位重试。

侦察席与火力点不再按设备 ID 互斥：同一组播设备可以继续显示在侦察席，同时作为一个火力阵位的通信信道。挂载只建立设备与阵位的绑定，同一设备最多挂载一个阵位；挂载后视频、侦察席坐标和飞机轨迹继续有效，解除挂载后清除阵位绑定但保留侦察席配置。通过「同时作为火力点」设置的独立火力点标记保存在 SQLite；已设为独立火力点的设备如需挂载，须先「取消独立火力点」。

## 地图目标

从 `config/targets.example.json` 复制出 `config/targets.json`，填写 EdgeAgent 使用的 `brokerUrl`（`mqtt://` 或 `mqtts://`），按需填写 `username` 和 `password`。目标主题默认为 `edge/v1/devices/target-input-device/data`，设备状态主题默认为 `edge/v1/devices/device-status-device/data`。

Windows + WSL 开发时，设备状态的 Windows 组播转发器接收 `239.255.0.1:5678`，把原始报文单播送入 WSL EdgeAgent 的 `18082` 端口；EdgeAgent 再通过 MQTT 发布到设备状态主题。`DEVICE_SIM_SOURCE_IPS` 用逗号分隔多个设备来源 IP。启动网页和转发器的终端都要设置同一列表，例如 PowerShell 使用 `$env:DEVICE_SIM_SOURCE_IPS="192.168.10.102,192.168.10.130"`。在项目目录另开终端运行 `npm run relay:edgeagent`。RK3588 部署包中的 EdgeAgent 则直接加入该组播并发布到 MQTT，`edgeagent.json` 的 `device-status-udp.allowed_peers` 与 `/etc/military-command-server/web.env` 的 `DEVICE_SIM_SOURCE_IPS` 都要包含相同来源。平台只订阅 MQTT。目标报文仍由 EdgeAgent 通过 MQTT 转发。服务解析 ScoutMonitor 033（1501）报文，在地图和右侧列表显示侦察目标与炸点目标。配置和连接状态在顶部显示。没有收到有效报文时，目标列表为空。

侦察卡片的「态势上报」向该设备上报的 IP 和 UDP 28081 端口单播一次 UTF-8 JSON 指令。设备按指令每 2000 毫秒向固定地址 `192.168.0.201:28080` 上报。指令保留「目标编号」「目标类型」「最大速度米每秒」三个字段，值均为 JSON `null`。网页提示指令的发送结果。

同一类型、同一编号的目标保存最新位置和最近 60 个不同于前一点的轨迹坐标。目标入库后可从数据管理查询，地图在服务启动时恢复最近更新的 300 个目标。设备最新快照保存在 `devices.sqlite`，重启后可查看，收到新心跳后恢复在线。火力点场景配置和对外上报记录沿用各自的保存位置。

设备状态组播的 `飞机位置` 可位于协议 2.0 的 `数据` 内或报文顶层。载荷位置需包含 `有效: true`、数值型 `经度`、`纬度`、`高程` 和可解析的 `更新时间`。平台按上报设备分别保留最近 60 个轨迹点及最新位置，忽略重复坐标和乱序位置。设备离线或标记位置无效时，地图标注末次有效位置。

地图上方的「载荷」和「目标」默认都勾选，可单独开关。红色显示载荷及其轨迹，蓝色显示侦察目标和炸点目标。「定位载荷」将地图移到已收到的位置。侦察席手动坐标可在「配置」中另行设置。

向火力点上报时，网页服务通过同一 MQTT Broker 把报文发给 EdgeAgent，由网关通过 TCP 转交外部接收端。阵位的「EdgeAgent 设备 ID」须与网关配置中的 `device_id` 一致，默认使用阵位编号。报文和回执格式见 [部署说明](tools/offline-rk3588/README.md#目标上报协议)。

整批通过阵位容量、坐标和设备绑定校验后开始发送。打开「上报记录」查看逐条结果或重试可发送目标。结果未知的设备暂停发送，操作员核对接收端后，在对应记录中标记「已处理」或「未处理」。
