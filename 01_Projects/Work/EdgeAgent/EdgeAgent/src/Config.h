/**
 * @file Config.h
 * @brief EdgeAgent 配置模型和配置加载接口。
 * @author xubb
 */

#pragma once

#include <QHostAddress>
#include <QString>
#include <QVector>

namespace EdgeAgent {

/** @brief 通道传输类型。 */
enum class ChannelType {
    Udp, ///< UDP 通道。
    Tcp, ///< TCP 通道。
    Serial, ///< 串口通道。
    Can, ///< CAN 通道。
};

/** MQTT 数据及回执格式；legacy 仅用于兼容旧客户端。 */
enum class MessageFormat { Unified, Legacy };

/** @brief TCP 和串口字节流的数据分帧方式。 */
enum class FrameKind {
    Delimiter, ///< 使用固定分隔符分帧。
    FixedLength, ///< 使用固定长度分帧。
    LengthPrefix, ///< 使用长度前缀分帧。
    IdleTimeout, ///< 空闲超时后将缓存视为一帧。
};

/** @brief 长度前缀的字节序。 */
enum class ByteOrder {
    BigEndian, ///< 大端字节序。
    LittleEndian, ///< 小端字节序。
};

/** @brief UDP 允许通信的对端。 */
struct PeerConfig {
    QHostAddress address; ///< 对端 IP 地址。
    quint16 port = 0; ///< 对端端口，0 表示允许此地址的任意源端口。
    int prefixLength = -1; ///< CIDR 前缀掩码长度，-1 表示精确主机地址，0 表示通配所有地址。
    bool isWildcard = false; ///< 是否通配任意 IP。
};

/** @brief TCP 或串口分帧参数。 */
struct FrameConfig {
    FrameKind kind = FrameKind::Delimiter; ///< 分帧类型。
    QByteArray delimiter; ///< 分隔符内容。
    int fixedLength = 0; ///< 固定分帧长度。
    int lengthBytes = 0; ///< 长度前缀占用的字节数。
    ByteOrder byteOrder = ByteOrder::BigEndian; ///< 长度前缀字节序。
    int idleMs = 0; ///< 空闲分帧超时时间，单位为毫秒。
    int maxBytes = 65536; ///< 单帧最大长度。
};

/** @brief UDP 通道参数。 */
struct UdpConfig {
    QHostAddress bindAddress; ///< 本地绑定地址。
    quint16 bindPort = 0; ///< 本地绑定端口。
    int maxDatagramBytes = 65507; ///< UDP 数据报最大长度。
    QVector<PeerConfig> allowedPeers; ///< 允许通信的对端列表。
    bool broadcast = false; ///< 是否启用广播选项。
    bool multicastEnabled = false; ///< 是否加入组播组。
    QHostAddress multicastGroup; ///< 组播地址。
    QString multicastInterfaceName; ///< 组播使用的网卡名称。
};

/** @brief TCP 通道参数。 */
struct TcpConfig {
    QString role; ///< 角色："server" 或 "client"。
    QHostAddress listenAddress; ///< 服务端监听地址。
    quint16 listenPort = 0; ///< 服务端监听端口。
    int maxClients = 1; ///< 服务端最大连接数。
    QString remoteHost; ///< 客户端远端主机地址。
    quint16 remotePort = 0; ///< 客户端远端端口。
    int connectTimeoutMs = 5000; ///< 客户端连接超时时间，单位为毫秒。
    int reconnectInitialMs = 1000; ///< 首次重连等待时间，单位为毫秒。
    int reconnectMaxMs = 60000; ///< 最大重连等待时间，单位为毫秒。
};

/** @brief 串口通信参数。 */
struct SerialConfig {
    QString portName; ///< 串口名称，例如 COM3 或 /dev/ttyUSB0。
    int baudRate = 9600; ///< 波特率，范围为 1200 到 921600。
    int dataBits = 8; ///< 数据位数，支持 5、6、7 或 8。
    QString parity = QStringLiteral("none"); ///< 校验方式：none、even、odd、space 或 mark。
    double stopBits = 1.0; ///< 停止位数，支持 1、1.5 或 2。
    QString flowControl = QStringLiteral("none"); ///< 流控方式：none、hardware 或 software。
    int reconnectInitialMs = 1000; ///< 串口断开后的首次重试间隔，单位为毫秒。
    int reconnectMaxMs = 60000; ///< 串口断开后的最大重试间隔，单位为毫秒。
};

/** @brief CAN 接收过滤器配置。 */
struct CanFilterConfig {
    quint32 canId = 0; ///< 过滤器 CAN ID。
    quint32 canIdMask = 0; ///< 过滤器掩码，掩码为 0 表示对应位不参与匹配。
    bool extended = false; ///< 是否只匹配扩展帧。
};

/** @brief SocketCAN 通道参数。 */
struct CanConfig {
    QString backend = QStringLiteral("socketcan"); ///< Qt Serial Bus 后端名称。
    QString interfaceName; ///< CAN 网卡名称，例如 can0 或 vcan0。
    int bitrate = 500000; ///< CAN 仲裁段波特率，单位为 bit/s。
    QVector<CanFilterConfig> filters; ///< 接收过滤器列表，空列表表示接收全部帧。
    bool receiveOwnFrames = false; ///< 是否接收本通道自己发出的回环帧。
    bool restartOnBusOff = true; ///< CAN Bus-Off 后是否请求自动恢复。
    int reconnectInitialMs = 1000; ///< CAN 设备重连初始间隔，单位为毫秒。
    int reconnectMaxMs = 60000; ///< CAN 设备重连最大间隔，单位为毫秒。
};

/** @brief 物理通信通道配置。 */
struct ChannelConfig {
    QString channelId; ///< 通道唯一标识。
    bool enabled = true; ///< 关闭后不创建接口，也不订阅或发布对应设备 Topic。
    ChannelType type = ChannelType::Udp; ///< 通道类型。
    FrameConfig frame; ///< TCP 或串口分帧配置。
    UdpConfig udp; ///< UDP 配置。
    TcpConfig tcp; ///< TCP 配置。
    SerialConfig serial; ///< 串口配置。
    CanConfig can; ///< CAN 配置。
};

/** @brief 将一个物理通道绑定到设备 Topic；每个通道最多一个绑定。 */
struct DeviceConfig {
    QString deviceId;
    QString channelId;
    int qos = 0;
    MessageFormat messageFormat = MessageFormat::Unified;
};

/** @brief MQTT Broker 连接配置。 */
struct BrokerConfig {
    QString host = QStringLiteral("127.0.0.1"); ///< Broker 主机地址。
    quint16 port = 1883; ///< Broker 端口。
    QString clientId; ///< MQTT 客户端标识。
    QString username; ///< MQTT 用户名。
    QString password; ///< MQTT 密码。
    int keepaliveSeconds = 30; ///< MQTT 保活时间，单位为秒。
    bool cleanSession = true; ///< 是否使用清理会话。
};

/** @brief EdgeAgent 全局配置。 */
struct AgentConfig {
    int configVersion = 1; ///< 配置版本号。
    QString nodeId; ///< 网关节点唯一标识。
    BrokerConfig broker; ///< MQTT Broker 配置。
    QVector<ChannelConfig> channels; ///< 通道配置列表。
    QVector<DeviceConfig> devices; ///< 通道到设备 Topic 的绑定。
};

/**
 * @brief 从 JSON 文件加载并校验配置。
 * @param path 配置文件路径。
 * @param config 配置输出对象。
 * @param error 错误信息输出对象，可传入 nullptr。
 * @return 配置加载成功返回 true，否则返回 false。
 */
bool loadConfig(const QString &path, AgentConfig *config, QString *error);

} // 命名空间 EdgeAgent
