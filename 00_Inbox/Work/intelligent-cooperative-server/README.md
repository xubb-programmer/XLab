# 智能协同作业调度平台 (Intelligent Cooperative Server)

智能协同作业调度平台是面向复杂现场环境的轻量级协同调度与态势展现系统，提供基于 Cesium.js 的高精三维态势展现、多路无人机 RTSP 视频矩阵、地面作业单元组织架构整备、设备组播自动发现与任务协同指派功能。

系统服务端基于 Node.js 24 原生轻量架构构建，结合 C++ 边缘网关组件（EdgeAgent）与 Mosquitto MQTT 代理，实现软硬件一体化自主可控与全离线内网稳定运行。

---

## 一、工程目录结构

```text
intelligent-cooperative-server/
├── src/                      # 前端交互与三维展现源码
│   ├── index.html            # 平台单页面主应用入口
│   ├── js/
│   │   ├── core/             # CesiumViewer 三维地球底图、坐标系与事件总线
│   │   ├── engine/           # 任务协同分配策略计算与目标数据管道
│   │   ├── layers/           # 地图图层（监测载荷、作业单元、目标、协同引导线）
│   │   └── ui/               # 交互控制中心（地图设置、视频矩阵、架构整备、数据台账）
│   └── styles/               # 深色工业级视觉规范（layout.css, components.css）
├── server/                   # Node.js 24 后端服务源码
│   ├── serve.mjs             # 主 HTTP 路由、静态资源分发与 API 网关
│   ├── targets.mjs           # MQTT 目标订阅与实时数据分发
│   ├── video.mjs             # RTSP 视频拉流代理与推流转发
│   ├── reporting.mjs         # 任务协同下派与批次事务控制
│   └── operational-store.mjs # 现场台账持久化数据层
├── config/                   # 统一业务配置目录（单一入口体系）
│   ├── app.json              # 核心业务主配置（HTTP、MQTT、RTSP、超时配置）
│   ├── scenario-default.json # 场景基准数据（包含初始视口经纬高坐标）
│   ├── terms.json            # 行业专业术语词表（开发与中性化标准版本）
│   ├── edgeagent.json        # EdgeAgent 边缘硬件网关通信参数配置
│   ├── mosquitto/            # 本地 MQTT Broker 本地监听配置
│   └── examples/             # 历史配置模板与示例归档
├── public/                   # 浏览器静态资源与全国概览卫星底图
│   ├── offline-satellite/    # 0~5 级全国及全球宏观离线卫星瓦片
│   └── scenarios/            # 场景持久化映射目录（软链接至 /var/lib/）
├── dist/                     # 运行时持久化数据与高精度离线图集
│   └── offline-maps/         # 高精度离线瓦片图集
│       └── combined-theaters/# 福建沿海与朱日和·赤峰合并高精瓦片库 (tiles.db, 1.67GB)
├── tools/                    # 构建、合规检查与离线部署工具
│   ├── extended-scan.mjs     # 全工程 0 敏感词自动化合规扫描器
│   └── offline-rk3588/       # RK3588 ARM64 离线安装与打包脚本
├── deployment-terms.json     # 生产环境专业术语现场映射配置（Git 自动忽略）
├── package.json              # 项目依赖与启动脚本定义
└── README.md                 # 项目工程使用与维护说明文档
```

---

## 二、核心架构与特性

### 1. 单一配置文件体系 (`config/app.json`)
平台彻底剔除旧版本分散零碎配置（如 `targets.json`、`video.json`、`reporting.json`），确立以 **[config/app.json](config/app.json)** 为唯一业务主配置：
- `server`: HTTP 监听地址 `host`（如 `0.0.0.0`）、监听端口 `port`（默认 `4177`）、设备组播来源白名单 `deviceSimSourceIps`；
- `targets`: MQTT 通信代理地址 `brokerUrl`、目标上报主题 `topic`、设备状态主题 `deviceStatusTopic`；
- `video`: RTSP 视频流通道列表及拉流超时选项；
- `reporting`: 任务下派上报超时时长与处理并发限制。

### 2. 多区域多图层超清离线卫星地图
针对野外无网与强保密运行环境，系统内置高性能 SQLite 离线瓦片存储驱动（基于 Node.js 24 原生 `DatabaseSync` 零依赖读取）：
- **全国及全球概览层**：预置于 `public/offline-satellite/`，覆盖 0~5 级概览瓦片；
- **高精重点区域图集**：存储于 `dist/offline-maps/combined-theaters/tiles.db`（161,009 张瓦片，1.67 GB），覆盖：
  - **东南沿海区域**：6~16 级区域宏观层与 17~18 级亚米级超清核心层；
  - **朱日和·赤峰区域**：6~16 级区域宏观层与 17~18 级亚米级超清核心层；
- 服务端通过 `/api/map/tiles/{z}/{x}/{y}.jpg` 提供毫秒级瓦片缓存响应，前端 Cesium 视口缩放平滑流畅。

### 3. 系统与态势可视化设置中心
点击导航栏「设置」选项卡即可进入对称双卡片式设置面板：
- **初始地图显示位置**：
  - 精确数值输入：支持手动输入中心经度（-180°~180°）、纬度（-90°~90°）、相机视点高度（米）；
  - 交互地图点选：点击「从地图点选拾取」切至三维地球直接点击地面拾取坐标并自动回填；
  - 视窗视心提取：点击「获取当前视窗中心」提取当前相机视心经纬度与高度；
  - 飞向预览与快捷预设：提供「朱日和·赤峰」、「东南沿海」、「全国概览」一键预设与视口平滑飞向预览；
  - 双持久化保存：点击「保存初始位置」后同时写入服务端场景配置文件与浏览器本地缓存，重启、刷新或点击「态势视角」均以此为基准。
- **业务数据管理中枢**：
  - 监测席台账卡片（显示当前配置台数与快速入口）；
  - 作业单元台账卡片（显示当前点位数与快速入口）；
  - 目标与事件记录卡片（显示当前目标项数与检索入口）；
  - 全量数据管理控制台入口与场景数据状态刷新。

### 4. 源码零敏感词合规保障
全工程代码、注释、样式、配置文件与技术文档全面实施中性化重构，内置合规自动化校验脚本 [tools/extended-scan.mjs](tools/extended-scan.mjs)，确保开发环境与代码库零敏感词残留。

---

## 三、快速开始（本地开发）

### 环境要求
- Node.js >= 24.0.0
- npm >= 10.0.0

### 安装与运行
```bash
# 1. 安装项目依赖
npm install

# 2. 启动服务（默认监听 http://127.0.0.1:4177/）
npm start

# 3. 执行语法健全性检查
npm run check

# 4. 执行全工程敏感词合规扫描
npm run scan
```

---

## 四、RK3588 边缘主板离线部署

系统支持在香橙派、鲁班猫（LubanCat）等基于 RK3588 芯片的 ARM64 边缘主板（Ubuntu 20.04 ARM64 / Debian 11 ARM64）上离线独立部署。

### 1. 目标板卡服务与部署路径
- **服务名称**：`intelligent-cooperative-server`
- **运行用户**：`intelligent-cooperative-server`（系统守护用户）
- **发布目录**：`/opt/jw/intelligent-cooperative-server/releases/`
- **当前软链**：`/opt/jw/intelligent-cooperative-server/current/`
- **业务主配置**：`/etc/intelligent-cooperative-server/app.json`
- **数据与地图持久化目录**：`/var/lib/intelligent-cooperative-server/`
  - 场景文件：`/var/lib/intelligent-cooperative-server/scenarios/scenario-default.json`
  - 离线地图：`/var/lib/intelligent-cooperative-server/offline-maps/combined-theaters/`

### 2. 常用运维与服务管理指令
```bash
# 查看主服务运行状态
systemctl status intelligent-cooperative-server

# 重启业务平台服务
sudo systemctl restart intelligent-cooperative-server

# 查看实时运行日志
journalctl -u intelligent-cooperative-server -f

# 验证核心接口连通性
curl -fsS http://192.168.0.201:4177/api/targets
curl -fsS http://192.168.0.201:4177/api/scenario/center
curl -I http://192.168.0.201:4177/offline-maps/combined-theaters/manifest.json
```

### 3. 板卡桌面端一键启动
在已接显示屏的板卡图形桌面环境中，可通过以下指令直接启动全屏界面：
```bash
/opt/jw/intelligent-cooperative-server/current/launch-intelligent-cooperative.sh
```
或双击桌面图标 **智能协同作业平台** 即可快速拉起应用。

---

## 五、南向边缘网关与通信指令架构 (EdgeAgent & Network Architecture)

全系统南向设备数据采集与控制指令已彻底完成统一收敛，均由 **EdgeAgent** 守护进程（C++ 边缘网关组件）与本地 Mosquitto MQTT 代理协同接管，全面提升战场网络鲁棒性与即插即用体验。

### 1. 南向通信指令与通道全景

| 业务指令 / 数据流 | 传输协议 | 端口规格 | EdgeAgent 渠道与主题 | 业务机制说明 |
| :--- | :--- | :--- | :--- | :--- |
| **态势目标输入 (1501)** | UDP | `28080` (监听) | `edge/v1/devices/target-input-device/data` | 接收外部侦察设备发送的目标与炸点报文，入库并动态上图。 |
| **设备心跳自发现** | UDP 组播 | `239.255.0.1:5678` | `edge/v1/devices/device-status-device/data` | 局域网广播被动感知，自动提取设备单播源 IP，支持即插即用自发现。 |
| **态势上报触发指令** | UDP | `28081` (下发) | `edge/v1/devices/target-input-device/commands` | 控制外部侦察载荷开启定时上报，携带同网段智能自适应回传 IP。 |
| **火力打击指派指令** | TCP | `5567` (默认可配) | `edge/v1/devices/<deviceId>/commands` | 协同裁决后将高精坐标下发给地面火力终端执行打击，带结果回执。 |

### 2. 关键网络自适应与高可用保障机制

1. **同网段回传地址智能自适应 (Subnet Auto-Matching)**：
   - 彻底清除静态开发机残留 IP，下发态势触发指令时，平台自动探测与目标设备同属一个子网的本机真实物理网卡 IP 作为回传地址（如目标为 `192.168.0.x` 则自动回传 `192.168.0.201`，为 `192.168.10.x` 则自动回传 `192.168.10.201`），杜绝数据走偏黑洞。
2. **侦察席手动单播 IP 兜底机制**：
   - 当现场交换机开启 IGMP Snooping 或跨网段导致组播心跳受阻时，操作员可在侦察席配置弹窗中手动输入目标设备单播 IP，系统优先采纳手动 IP 进行指令下发与通信唤醒。
3. **火力点配置前置防呆校验**：
   - 若火力点 IP 仍为本地回环（`127.0.0.1`）且未挂载外部在线设备，界面提供醒目的黄色警示标识，并在下发打击任务前弹窗拦截提示确认，避免无谓的发送超时。
4. **板端双物理网口（eth0 / eth1）盲插冗余热备**：
   - RK3588 主板双千兆网口 `eth0` 与 `eth1` 均已配置统一的静态业务 IP（`192.168.0.201/24`、`192.168.10.201/24`），现场网线插拔切换任一网口均可稳定连接，无需重启或重新配置。

---

## 六、发布与离线部署打包指南 (Release & Packaging Guide)

为确保现场部署交付的完整性与更新效率，工程提供全量与增量两种自动化打包流水线：

### 1. 全量完整安装包打包（默认推荐，包含 1.67GB 离线高精地图库）
```bash
npm run pack
# 或执行: npm run pack:full
```
- **产物**：`intelligent-cooperative-server-full.tar.gz`
- **内容**：涵盖完整前后端源码、SQLite 数据中台、统一业务配置，以及 **`data/offline-maps/combined-theaters/` 完整离线高清多战区卫星瓦片数据库（16.1 万张瓦片，1.67GB，涵盖朱日和·赤峰、东南沿海、南京句容等战区）**。现场新设备交付或整机刷机升级时使用。

### 2. 轻量代码增量更新包（适用于已有地图的在线快速迭代）
```bash
npm run pack:lite
```
- **产物**：`intelligent-cooperative-server-lite.tar.gz` (~165 KB)
- **内容**：仅包含前端静态资源、后端核心路由与配置文件，在板端已有大型离线地图库的前提下，秒级打包并快速热更新。
