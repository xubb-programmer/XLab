# RK3588 离线部署（Ubuntu / Debian）

部署目标是 ARM64 的 Ubuntu 或 Debian 系统，并要求目标系统使用 systemd。当前已验证 Ubuntu 20.04 ARM64 和 Debian 11 ARM64。目标板可与 ScoutMonitor、操作员电脑、RTSP 视频源和接收终端在同一个现场局域网内。安装及离线地图模式均不访问公网。

## 先在联网机器生成安装包

在与目标板发行版、版本和架构相同的联网构建环境运行。脚本会安装构建依赖，编译 EdgeAgent，下载并核验 ARM64 Node.js，按项目锁定版本安装网页依赖，以及收集 Mosquitto、FFmpeg 和运行库的 ARM64 DEB 包。构建环境可用 ARM64 机器，也可用支持 ARM64 仿真的容器或虚拟机。当前支持 Ubuntu / Debian 的 DEB 系系统；其他发行版需要另行制作对应包格式和运行库闭包。

```bash
sudo bash tools/offline-rk3588/prepare.sh /path/to/output
```

按 XLab 现有目录结构，脚本会定位 EdgeAgent 源码。若源码位置不同，可传入 `EDGEAGENT_SOURCE`（含 `EdgeAgent.pro` 的目录）。可用 `NODE_VERSION` 固定要构建的 Node.js 24 发行版本。输出文件名按构建环境自动生成，例如 Debian 11 ARM64 为 `military-command-server-debian11-arm64.tar.gz`，并同时生成 `.sha256` 文件。

全球和中国概览放在项目的 `public/offline-satellite/`。厦门和朱日和—赤峰源图分别保存在 `dist/offline-maps/highres-satellite/` 与 `dist/offline-maps/zhurihe-chifeng/`。打包时 `merge-maps.mjs` 用 SQLite 在线备份制作一致性快照，合并成 `combined-theaters/tiles.db`，按战区图层生成精确瓦片覆盖并检查完整性。若朱日和—赤峰数据库仍有非空 WAL 写入日志，脚本默认停止打包；确需先发布当前下载进度时设置 `ALLOW_INCOMPLETE_OFFLINE_MAP=1`，输出会标记为 `provisional`。正式图集应在下载结束、写入检查点完成后重新生成。板上由随包 Node.js 24 读取合并库。

若用 Docker，从 XLab 根目录执行以下命令。先建好输出目录，且 Docker 已能运行 `linux/arm64` 镜像。

```bash
mkdir -p dist/rk3588
docker run --rm --platform linux/arm64 \
  -v "$PWD:/workspace:ro" -v "$PWD/dist/rk3588:/out" ubuntu:20.04 \
  bash /workspace/00_Inbox/Work/military-command-server/tools/offline-rk3588/prepare.sh /out
```

Debian 11 目标板应使用 Debian 11 ARM64 构建环境，例如在 Debian 11 ARM64 板上直接执行；也可以把上面的镜像替换为内部可用的 `debian:11` ARM64 镜像。不要用 Ubuntu 生成的 DEB 包安装到 Debian，反之亦然。

若构建机本身也是离线的，先把目标发行版对应的 DEB 放入 `/var/cache/apt/archives/`，把 Node.js 发行包和校验文件放入 `NODE_CACHE_DIR`，并准备与 `package-lock.json` 匹配的 `node_modules` 目录，然后使用：

```bash
sudo env OFFLINE_BUILD=1 \
  NODE_CACHE_DIR=/var/cache/military-command-server \
  NODE_MODULES_SOURCE=/path/to/node_modules \
  EDGEAGENT_SOURCE=/path/to/EdgeAgent \
  bash tools/offline-rk3588/prepare.sh /path/to/output
```

联网构建时不需要设置这三个环境变量；脚本会按锁定版本下载 Node.js、安装网页依赖并收集目标系统的 DEB 闭包。

## 在离线板上一键安装

确认板卡系统时间正确。现场没有公网授时时，可使用板卡 RTC 或现场内网 NTP。把两个输出文件拷到目标板，在同一个目录执行。

```bash
sha256sum -c military-command-server-debian11-arm64.tar.gz.sha256
tar --touch -xzf military-command-server-debian11-arm64.tar.gz
cd military-command-server-offline
sudo bash install.sh
```

安装器检查系统发行版、版本和架构，只从包内的 APT 仓库装依赖。Mosquitto、EdgeAgent 和网页服务由 systemd 启动。程序装在 `/opt/jw/military-command-server`，站点配置在 `/etc/military-command-server`，阵位数据在 `/var/lib/military-command-server/scenarios`。重复安装会保留已配置的站点参数和阵位文件，并为旧 EdgeAgent 配置补上设备状态组播通道。初次安装默认启用离线底图。实际 RTSP 地址和现场来源需要配置。

从旧命名版本升级时，安装器暂停网页和 EdgeAgent，迁移配置、场景、数据库、服务账户和构建缓存。服务账户的 UID/GID 保持原值，启动检查失败时恢复原路径、账户和服务配置。EdgeAgent 使用默认节点名的部署会切换到 `military_command_server_edge`，其状态主题也随之更新。

| 现场项 | 初始值与后续配置 |
| --- | --- |
| 网页 | 默认在板上访问 `http://192.168.0.201:4177/`。用内网电脑访问时，把 `web.env` 的 `MILITARY_COMMAND_SERVER_BIND_HOST` 改为板卡的内网 IP，并限制访问来源，再重启 `military-command-server`。 |
| MQTT | 默认使用本机 `127.0.0.1:1883`。若板上已有 Broker，沿用其配置，并核对网页和 EdgeAgent 的账号及主题。 |
| ScoutMonitor | UDP 目标端口为 `28080`，接受 `192.168.0.0/24`、`192.168.10.0/24` 的主机地址和本机回环。打包时将网段展开为 EdgeAgent 支持的 `IP:0` 列表。 |
| 侦察与火力设备状态 | EdgeAgent 加入 `239.255.0.1:5678` 组播，接受同样两个局域网网段，发布到 `edge/v1/devices/device-status-device/data`。首次安装根据包内 `device-status-udp.allowed_peers` 生成 `web.env` 的 `DEVICE_SIM_SOURCE_IPS`。调整已有部署时需同步更新两处，再重启 `server-edge` 和 `military-command-server`。 |
| RTSP 视频 | 在 `video.json` 按无人机填写内网 RTSP 地址。随目标发行版打包的 FFmpeg 使用 `stimeout`，更换 FFmpeg 版本时按实际支持的选项调整。 |
| 无人机实时经纬高 | 由设备状态组播的 `飞机位置` 提供，可在地图上独立显示载荷位置与轨迹。 |
| 目标上报 | 在 `edgeagent.json` 填写外部接收端的 IP 和端口。设备 `fire-01-device` 连接 `192.168.10.102:5567`，设备 `fire-02-device` 通过 `fire-02-tcp` 连接 `192.168.0.102:5567`。 |
| 离线底图 | 页面默认离线，地图左上角可切换在线或离线。用 `/var/lib/military-command-server/scenarios/scenario-default.json` 中的 `mapMode` 配置初始模式。厦门、朱日和和赤峰从随包合并 SQLite 图集读取，各自按图层范围显示，覆盖外使用概览。影像瓦片只提供底图，实际地面高程需另有数据来源。 |
| 数据管理 | 从主界面的「设置 → 数据管理」查看实时记录和手工台账。设备上报目标及手工记录保存在 `/var/lib/military-command-server/management/records.sqlite`，设备最新快照保存在 `/var/lib/military-command-server/device-simulation/devices.sqlite`。重启后可恢复，设备收到新心跳后恢复在线。 |

### 鲁班猫板上的指挥界面

从板卡桌面会话运行 `/opt/jw/military-command-server/current/launch-military-command.sh`（源码目录中为 `bash tools/offline-rk3588/launch-military-command.sh`）。脚本在本地 X11 `:0` 会话使用板载 Mali EGL，在 XRDP 虚拟桌面使用 Mesa GLX。可在 `chrome://gpu` 查看 WebGL 渲染器。远程桌面里检测到软件渲染时，地图会降低绘图分辨率以改善交互。

从板卡桌面启动指挥界面时，启动脚本为当前用户注册工具入口。「即时通讯」启动 LANChat，「情报辅助分析系统」运行板上的 `/opt/jw/qbcl/binr/ScoutMonitor.sh`，「侦察车智能助手」在新标签页打开板上的 HTTPS 页面。桌面工具首次打开可能弹出外部应用确认。

也可从 Windows PowerShell 建立 SSH 转发，让 Windows 浏览器使用本机显卡渲染。保持终端开启，在浏览器打开 `http://127.0.0.1:14177/`：

```powershell
ssh -N -o ExitOnForwardFailure=yes -L 127.0.0.1:14177:192.168.0.201:4177 ztl@192.168.0.201
```

## 目标上报协议

目标上报与上图使用同一 MQTT Broker。网页的阵位「EdgeAgent 设备 ID」对应 `edgeagent.json` 中的 `device_id`。为每个外部接收端配置一个 TCP 客户端通道，`remote_host` 和 `remote_port` 指向接收端，使用换行分帧和 `message_format: "json"`。配置生效需重启 EdgeAgent。升级时安装器会用旧场景里的地址为尚无设备绑定的阵位补建通道。配置了 MQTT 权限时，网页服务需能向对应设备的 `commands` 发布并订阅 `command-results`、`data`。

每个目标发送一行 UTF-8 JSON，末尾追加 LF（`0x0A`）。经纬度为十进制度数值，四舍五入至六位小数，高程取整到米。请求中的 `name` 固定为 `Action`。接收端回执也以 LF 结尾。

发送前将 `lon`、`lat` 按标准 6° 高斯-克吕格投影为平面直角坐标，中央经线根据目标经度自动匹配对齐战区（如朱日和自动匹配第 19 带中央经线 111°E，赤峰/厦门自动匹配第 20 带中央经线 117°E）。`x` 为含 500,000 米假东偏的东坐标，`y` 为北坐标，数值四舍五入到 0.01 米。外部接收端（火力/指控终端）直接解析 `x`、`y` 字段即可接入打击指令。

```jsonl
{"name":"Action","params":{"lon":118.900024,"lat":42.263227,"elevate":50,"x":161689.49,"y":4689023.06},"request_id":"example-id"}
{"ok":true,"request_id":"example-id"}
```

接收端回传同一 `request_id`。平台收到来自同一设备且带布尔值 `ok: true` 的匹配 JSON 回执时确认成功。失败回执使用 `ok: false`，可带 `error` 说明。EdgeAgent 的发送回执用于报告转发失败。

接收端能够确认未处理时，可在失败回执中返回 `processed: false`。队列满用 `code: "queue_full"`，终端忙用 `code: "busy"`。缺少处理结果的失败回执归为「结果未知」。

上报记录保存在 `/var/lib/military-command-server/reporting.sqlite`。每个部署实例使用独立数据库，升级沿用已有记录。网页的「上报记录」可查询批次、核实结果未知的目标和差量重试。

## 现场验收

1. 在与公网断开的现场网段解包并安装，重启板卡后核对三项服务和网页。
2. 用 Qt 目标模拟器向板卡各发一条 033（1501）侦察目标和炸点目标，核对两个标记与列表坐标。确认无公网底图请求。
3. 配置现场 RTSP 地址，验证打开、切换、关闭视频。配置无人机位置和离线瓦片后，分别核对轨迹和底图覆盖。
4. 用测试数据与外部接收端核对目标报文和回执。

系统日志可用 `journalctl -u military-command-server -u server-edge -u mosquitto` 查看。请按 RK3588 上实际安装的系统镜像准备对应包，Debian 与 Ubuntu 的系统包分开验收。

## 已验证

已在 RK3588 上用 Ubuntu 20.04 ARM64 隔离环境执行包内安装脚本。环境只有回环网络，APT 从包内装齐依赖，真实 EdgeAgent 和 Mosquitto 将侦察目标、炸点目标两类 1501 测试报文转给网页服务。浏览器已验证离线模式无公网请求。

当前验证目标为 Debian 11 ARM64（`ztl@192.168.0.201`）：包应在 Debian 11 ARM64 构建环境生成，并使用包内 DEB 仓库安装，避免依赖联网 APT 源。

隔离环境用测试程序启动服务进程。原生 Ubuntu 20.04 镜像上的 systemd 开机自启、实际 RTSP 视频和外部接收端仍需现场验收。
