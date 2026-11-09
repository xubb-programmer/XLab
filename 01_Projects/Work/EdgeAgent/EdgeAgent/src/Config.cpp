/**
 * @file Config.cpp
 * @brief JSON 配置解析和校验实现。
 * @author xubb
 */

#include "Config.h"

#include <nlohmann/json.hpp>

#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace EdgeAgent {
namespace {

using Json = nlohmann::json; // 配置文件使用的 JSON 类型别名。
const QRegularExpression kIdPattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$")); // 标识字段校验规则。

/** @brief 写入错误信息并返回失败结果。 */
bool fail(QString *error, const QString &message)
{
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

/** @brief 读取必填字符串字段。 */
bool readRequiredString(const Json &object, const char *key, QString *value, QString *error)
{
    if (!object.contains(key) || !object.at(key).is_string()) {
        return fail(error, QStringLiteral("缺少字符串字段：%1").arg(QString::fromLatin1(key)));
    }
    *value = QString::fromStdString(object.at(key).get<std::string>());
    if (value->isEmpty()) {
        return fail(error, QStringLiteral("字段不能为空：%1").arg(QString::fromLatin1(key)));
    }
    return true;
}

/** @brief 读取并校验端口字段。 */
bool readPort(const Json &object, const char *key, quint16 *value, QString *error)
{
    if (!object.contains(key) || !object.at(key).is_number_integer()) {
        return fail(error, QStringLiteral("缺少数字字段：%1").arg(QString::fromLatin1(key)));
    }
    const int port = object.at(key).get<int>(); // 先使用整数校验，避免浮点端口被接受。
    if (port < 1 || port > 65535) {
        return fail(error, QStringLiteral("端口超出范围：%1=%2").arg(QString::fromLatin1(key)).arg(port));
    }
    *value = static_cast<quint16>(port);
    return true;
}

/** @brief 读取整数配置并同时校验其类型和取值范围。 */
bool readIntegerField(const Json &object,
                      const char *key,
                      int defaultValue,
                      int minimum,
                      int maximum,
                      int *value,
                      QString *error,
                      const QString &fieldName)
{
    const auto it = object.find(key); // 查找可选整数配置字段。
    if (it == object.end()) {
        *value = defaultValue;
        return true;
    }
    if (!it->is_number_integer()) {
        return fail(error, QStringLiteral("%1 必须是整数").arg(fieldName));
    }

    qint64 number = 0; // 使用 64 位中间值，避免窄化时溢出。
    if (it->is_number_unsigned()) {
        const quint64 unsignedNumber = it->get<quint64>();
        if (unsignedNumber > static_cast<quint64>(maximum)) {
            return fail(error, QStringLiteral("%1 必须在 %2 到 %3 之间").arg(fieldName).arg(minimum).arg(maximum));
        }
        number = static_cast<qint64>(unsignedNumber);
    } else {
        number = it->get<qint64>();
    }
    if (number < minimum || number > maximum) {
        return fail(error, QStringLiteral("%1 必须在 %2 到 %3 之间").arg(fieldName).arg(minimum).arg(maximum));
    }
    *value = static_cast<int>(number);
    return true;
}

/** @brief 解析统一 JSON 或旧格式输出选项。 */
bool parseMessageFormat(const Json &object, DeviceConfig *device, QString *error)
{
    const Json format = object.value("message_format", Json("json"));
    if (format == "json") device->messageFormat = MessageFormat::Unified;
    else if (format == "legacy") device->messageFormat = MessageFormat::Legacy;
    else return fail(error, QStringLiteral("message_format 必须为 json 或 legacy"));
    return true;
}

/** @brief 将字符串解析为 IP 地址。 */
bool parseAddress(const QString &text, QHostAddress *address, QString *error, const QString &field)
{
    QHostAddress parsed; // 临时地址对象，解析成功后再写入输出参数。
    if (!parsed.setAddress(text)) {
        return fail(error, QStringLiteral("IPv4 地址无效：%1=%2").arg(field, text));
    }
    *address = parsed;
    return true;
}

/** @brief 解析带 0x 前缀的 CAN ID 或掩码。 */
bool parseCanIdText(const QString &text, quint32 *value, QString *error, const QString &field)
{
    if (value == nullptr || !text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) || text.size() <= 2) {
        return fail(error, QStringLiteral("%1 必须是带 0x 前缀的十六进制字符串").arg(field));
    }
    bool ok = false; // 记录十六进制文本转换结果。
    const qulonglong number = text.mid(2).toULongLong(&ok, 16); // 读取 CAN ID 数值。
    if (!ok || number > 0x1FFFFFFFULL) {
        return fail(error, QStringLiteral("%1 超出 29 位 CAN ID 范围").arg(field));
    }
    *value = static_cast<quint32>(number);
    return true;
}

/** @brief 解析 allowed_peers 中的 IP:端口文本。 */
bool parsePeer(const QString &text, PeerConfig *peer, QString *error)
{
    const int separator = text.lastIndexOf(QLatin1Char(':')); // 使用最后一个冒号分隔 IP 和端口。
    if (separator <= 0 || separator == text.size() - 1) {
        return fail(error, QStringLiteral("对端地址必须是 IP:端口：%1").arg(text));
    }

    QHostAddress address; // 解析出的对端地址。
    if (!parseAddress(text.left(separator), &address, error, QStringLiteral("allowed_peers"))) {
        return false;
    }
    bool ok = false; // 记录端口文本是否成功转换为整数。
    const int port = text.mid(separator + 1).toInt(&ok); // 对端端口。
    if (!ok || port < 0 || port > 65535) {
        return fail(error, QStringLiteral("对端端口无效：%1").arg(text));
    }
    peer->address = address;
    peer->port = static_cast<quint16>(port);
    return true;
}

/** @brief 解析 TCP 或串口字节流的分帧配置。 */
bool parseFrame(const Json &object, FrameConfig *frame, QString *error)
{
    if (!object.contains("frame") || !object.at("frame").is_object()) {
        return fail(error, QStringLiteral("缺少 frame 配置对象"));
    }
    const Json &frameObject = object.at("frame"); // frame 配置对象。
    const QString kind = QString::fromStdString(frameObject.value("kind", std::string())); // 分帧类型名称。
    if (kind == QStringLiteral("delimiter")) {
        frame->kind = FrameKind::Delimiter;
        frame->delimiter = QString::fromStdString(frameObject.value("delimiter", std::string())).toUtf8();
        if (frame->delimiter.isEmpty()) {
            return fail(error, QStringLiteral("delimiter 分帧必须配置非空分隔符"));
        }
    } else if (kind == QStringLiteral("fixed_length")) {
        frame->kind = FrameKind::FixedLength;
        frame->fixedLength = frameObject.value("length", 0);
        if (frame->fixedLength < 1) {
            return fail(error, QStringLiteral("fixed_length 分帧的 length 必须大于 0"));
        }
    } else if (kind == QStringLiteral("length_prefix")) {
        frame->kind = FrameKind::LengthPrefix;
        frame->lengthBytes = frameObject.value("length_bytes", 0);
        if (frame->lengthBytes != 1 && frame->lengthBytes != 2 && frame->lengthBytes != 4) {
            return fail(error, QStringLiteral("length_prefix 分帧的 length_bytes 必须是 1、2 或 4"));
        }
        const QString byteOrder = QString::fromStdString(frameObject.value("byte_order", std::string("big_endian"))); // 字节序名称。
        if (byteOrder == QStringLiteral("big_endian")) {
            frame->byteOrder = ByteOrder::BigEndian;
        } else if (byteOrder == QStringLiteral("little_endian")) {
            frame->byteOrder = ByteOrder::LittleEndian;
        } else {
            return fail(error, QStringLiteral("不支持的字节序：%1").arg(byteOrder));
        }
    } else if (kind == QStringLiteral("idle_timeout")) {
        frame->kind = FrameKind::IdleTimeout;
        frame->idleMs = frameObject.value("idle_ms", 0);
        if (frame->idleMs < 1) {
            return fail(error, QStringLiteral("idle_timeout 分帧的 idle_ms 必须大于 0"));
        }
    } else {
        return fail(error, QStringLiteral("不支持的分帧类型：%1").arg(kind));
    }

    frame->maxBytes = frameObject.value("max_bytes", 65536);
    if (frame->maxBytes < 1 || frame->maxBytes > 16 * 1024 * 1024) {
        return fail(error, QStringLiteral("分帧 max_bytes 必须在 1 到 16777216 之间"));
    }
    if (frame->kind == FrameKind::FixedLength && frame->fixedLength > frame->maxBytes) {
        return fail(error, QStringLiteral("固定帧长度不能超过 max_bytes"));
    }
    return true;
}

/** @brief 解析单个通道配置。 */
bool parseChannel(const Json &object, ChannelConfig *channel, QString *error)
{
    const Json enabled = object.value("enabled", Json(true));
    if (!enabled.is_boolean()) return fail(error, QStringLiteral("通道 enabled 必须是布尔值"));
    channel->enabled = enabled.get<bool>();
    if (!readRequiredString(object, "channel_id", &channel->channelId, error)) {
        return false;
    }
    if (!kIdPattern.match(channel->channelId).hasMatch()) {
        return fail(error, QStringLiteral("channel_id 不符合标识格式"));
    }

    const QString type = QString::fromStdString(object.value("type", std::string())); // 通道类型名称。
    QString mode = QString::fromStdString(object.value("mode", std::string()));
    bool storeForward = false;
    if (object.contains("store_forward") && object.at("store_forward").is_object()) {
        storeForward = object.at("store_forward").value("enabled", false);
    }
    if (storeForward) {
        return fail(error, QStringLiteral("当前 UDP/TCP/串口/CAN 版本暂未实现 store_forward"));
    }

    if (type == QStringLiteral("udp")) {
        channel->type = ChannelType::Udp;
        mode = mode.isEmpty() ? QStringLiteral("json") : mode;
        if (mode != QStringLiteral("json")) {
            return fail(error, QStringLiteral("UDP 通道必须使用 mode=json"));
        }
        QString addressText; // UDP 本地绑定地址文本。
        if (!readRequiredString(object, "bind_address", &addressText, error)
            || !parseAddress(addressText, &channel->udp.bindAddress, error, QStringLiteral("bind_address"))
            || !readPort(object, "bind_port", &channel->udp.bindPort, error)) {
            return false;
        }
        channel->udp.maxDatagramBytes = object.value("max_datagram_bytes", 65507);
        if (channel->udp.maxDatagramBytes < 1 || channel->udp.maxDatagramBytes > 65507) {
            return fail(error, QStringLiteral("max_datagram_bytes 必须在 1 到 65507 之间"));
        }
        if (!object.contains("allowed_peers") || !object.at("allowed_peers").is_array()) {
            return fail(error, QStringLiteral("UDP 必须配置 allowed_peers，端口 0 表示通配任意源端口"));
        }
        for (const Json &value : object.at("allowed_peers")) { // 逐项解析来源白名单。
            if (!value.is_string()) {
                return fail(error, QStringLiteral("allowed_peers 中的每一项必须是字符串"));
            }
            PeerConfig peer; // 当前正在解析的 UDP 对端白名单项。
            if (!parsePeer(QString::fromStdString(value.get<std::string>()), &peer, error)) {
                return false;
            }
            channel->udp.allowedPeers.append(peer);
        }

        channel->udp.broadcast = object.value("broadcast", false);
        const Json multicast = object.value("multicast", Json::object()); // 组播配置对象，可为空。
        channel->udp.multicastEnabled = multicast.value("enabled", false);
        if (channel->udp.multicastEnabled) {
            const QString group = QString::fromStdString(multicast.value("group", std::string())); // 组播地址文本。
            if (!parseAddress(group, &channel->udp.multicastGroup, error, QStringLiteral("multicast.group"))) {
                return false;
            }
            channel->udp.multicastInterfaceName = QString::fromStdString(multicast.value("interface", std::string()));
        }
        return true;
    }

    if (type == QStringLiteral("tcp")) {
        channel->type = ChannelType::Tcp;
        mode = mode.isEmpty() ? QStringLiteral("raw") : mode;
        if (mode != QStringLiteral("raw")) {
            return fail(error, QStringLiteral("v1 版本的 TCP 通道必须使用 mode=raw"));
        }
        if (!parseFrame(object, &channel->frame, error)) {
            return false;
        }
        channel->tcp.role = QString::fromStdString(object.value("role", std::string()));
        if (channel->tcp.role != QStringLiteral("server") && channel->tcp.role != QStringLiteral("client")) {
            return fail(error, QStringLiteral("TCP role 必须是 server 或 client"));
        }
        channel->tcp.connectTimeoutMs = object.value("connect_timeout_ms", 5000);
        channel->tcp.reconnectInitialMs = object.value("reconnect_initial_ms", 1000);
        channel->tcp.reconnectMaxMs = object.value("reconnect_max_ms", 60000);
        if (channel->tcp.connectTimeoutMs < 100 || channel->tcp.reconnectInitialMs < 100
            || channel->tcp.reconnectInitialMs > channel->tcp.reconnectMaxMs) {
            return fail(error, QStringLiteral("TCP 超时或重连参数无效"));
        }

        if (channel->tcp.role == QStringLiteral("server")) {
            QString addressText; // TCP 服务端监听地址文本。
            if (!readRequiredString(object, "listen_address", &addressText, error)
                || !parseAddress(addressText, &channel->tcp.listenAddress, error, QStringLiteral("listen_address"))
                || !readPort(object, "listen_port", &channel->tcp.listenPort, error)) {
                return false;
            }
            channel->tcp.maxClients = object.value("max_clients", 8);
            if (channel->tcp.maxClients < 1 || channel->tcp.maxClients > 1024) {
                return fail(error, QStringLiteral("max_clients 必须在 1 到 1024 之间"));
            }
        } else {
            if (!readRequiredString(object, "remote_host", &channel->tcp.remoteHost, error)
                || !readPort(object, "remote_port", &channel->tcp.remotePort, error)) {
                return false;
            }
        }
        return true;
    }

    if (type == QStringLiteral("serial")) {
        channel->type = ChannelType::Serial;
        mode = mode.isEmpty() ? QStringLiteral("raw") : mode;
        if (mode != QStringLiteral("raw")) {
            return fail(error, QStringLiteral("串口通道必须使用 mode=raw"));
        }
        if (!readRequiredString(object, "port_name", &channel->serial.portName, error)) {
            return false;
        }

        const Json stopBits = object.value("stop_bits", Json(1.0)); // 读取串口停止位配置。
        if (!stopBits.is_number()) {
            return fail(error, QStringLiteral("串口 stop_bits 必须是数字"));
        }
        if (!readIntegerField(object, "baud_rate", 9600, 1200, 921600, &channel->serial.baudRate,
                              error, QStringLiteral("串口 baud_rate"))
            || !readIntegerField(object, "data_bits", 8, 5, 8, &channel->serial.dataBits,
                                 error, QStringLiteral("串口 data_bits"))
            || !readIntegerField(object, "reconnect_initial_ms", 1000, 100, 3600000,
                                 &channel->serial.reconnectInitialMs, error,
                                 QStringLiteral("串口 reconnect_initial_ms"))
            || !readIntegerField(object, "reconnect_max_ms", 60000, 100, 3600000,
                                 &channel->serial.reconnectMaxMs, error,
                                 QStringLiteral("串口 reconnect_max_ms"))) {
            return false;
        }
        channel->serial.stopBits = stopBits.get<double>();
        if (channel->serial.stopBits != 1.0 && channel->serial.stopBits != 1.5 && channel->serial.stopBits != 2.0) {
            return fail(error, QStringLiteral("串口 stop_bits 必须是 1、1.5 或 2"));
        }
        if (channel->serial.stopBits == 1.5 && channel->serial.dataBits != 5) {
            return fail(error, QStringLiteral("1.5 个停止位仅支持 5 个数据位"));
        }

        const Json parity = object.value("parity", Json("none")); // 读取校验方式。
        const Json flowControl = object.value("flow_control", Json("none")); // 读取流控方式。
        if (!parity.is_string() || !flowControl.is_string()) {
            return fail(error, QStringLiteral("串口 parity 和 flow_control 必须是字符串"));
        }
        channel->serial.parity = QString::fromStdString(parity.get<std::string>());
        channel->serial.flowControl = QString::fromStdString(flowControl.get<std::string>());
        if (channel->serial.parity != QStringLiteral("none") && channel->serial.parity != QStringLiteral("even")
            && channel->serial.parity != QStringLiteral("odd") && channel->serial.parity != QStringLiteral("space")
            && channel->serial.parity != QStringLiteral("mark")) {
            return fail(error, QStringLiteral("串口 parity 必须是 none、even、odd、space 或 mark"));
        }
        if (channel->serial.flowControl != QStringLiteral("none")
            && channel->serial.flowControl != QStringLiteral("hardware")
            && channel->serial.flowControl != QStringLiteral("software")) {
            return fail(error, QStringLiteral("串口 flow_control 必须是 none、hardware 或 software"));
        }

        if (channel->serial.reconnectInitialMs > channel->serial.reconnectMaxMs) {
            return fail(error, QStringLiteral("串口重连间隔必须在 100 到 3600000 毫秒之间，且最大间隔不小于初始间隔"));
        }
        return parseFrame(object, &channel->frame, error);
    }

    if (type == QStringLiteral("can")) {
        channel->type = ChannelType::Can;
        mode = mode.isEmpty() ? QStringLiteral("json") : mode;
        if (mode != QStringLiteral("json")) {
            return fail(error, QStringLiteral("CAN 通道必须使用 mode=json"));
        }
        if (!readRequiredString(object, "interface", &channel->can.interfaceName, error)) {
            return false;
        }
        const Json backend = object.value("backend", Json("socketcan")); // 读取 Qt Serial Bus 后端。
        if (!backend.is_string()) {
            return fail(error, QStringLiteral("CAN backend 必须是字符串"));
        }
        channel->can.backend = QString::fromStdString(backend.get<std::string>());
        if (channel->can.backend.isEmpty()) {
            return fail(error, QStringLiteral("CAN backend 不能为空"));
        }
        if (!readIntegerField(object, "bitrate", 500000, 10000, 1000000, &channel->can.bitrate,
                              error, QStringLiteral("CAN bitrate"))
            || !readIntegerField(object, "reconnect_initial_ms", 1000, 100, 3600000,
                                 &channel->can.reconnectInitialMs, error,
                                 QStringLiteral("CAN reconnect_initial_ms"))
            || !readIntegerField(object, "reconnect_max_ms", 60000, 100, 3600000,
                                 &channel->can.reconnectMaxMs, error,
                                 QStringLiteral("CAN reconnect_max_ms"))) {
            return false;
        }
        if (channel->can.reconnectInitialMs > channel->can.reconnectMaxMs) {
            return fail(error, QStringLiteral("CAN 重连最大间隔必须不小于初始间隔"));
        }
        const Json receiveOwn = object.value("receive_own_frames", false); // 是否接收自身回环帧。
        const Json restartOnBusOff = object.value("restart_on_bus_off", true); // 是否自动恢复 Bus-Off。
        if (!receiveOwn.is_boolean() || !restartOnBusOff.is_boolean()) {
            return fail(error, QStringLiteral("CAN receive_own_frames 和 restart_on_bus_off 必须是布尔值"));
        }
        channel->can.receiveOwnFrames = receiveOwn.get<bool>();
        channel->can.restartOnBusOff = restartOnBusOff.get<bool>();

        const Json filters = object.value("filters", Json::array()); // CAN 接收过滤器列表。
        if (!filters.is_array()) {
            return fail(error, QStringLiteral("CAN filters 必须是数组"));
        }
        for (const Json &filterObject : filters) {
            if (!filterObject.is_object() || !filterObject.contains("can_id") || !filterObject.at("can_id").is_string()) {
                return fail(error, QStringLiteral("CAN filters 每项必须包含字符串 can_id"));
            }
            const Json extended = filterObject.value("extended", false); // 当前过滤器是否匹配扩展帧。
            if (!extended.is_boolean()) {
                return fail(error, QStringLiteral("CAN 过滤器 extended 必须是布尔值"));
            }
            CanFilterConfig filter; // 当前正在解析的 CAN 过滤器。
            filter.extended = extended.get<bool>();
            if (!parseCanIdText(QString::fromStdString(filterObject.at("can_id").get<std::string>()),
                                &filter.canId, error, QStringLiteral("CAN filters.can_id"))) {
                return false;
            }
            const QString defaultMask = filter.extended ? QStringLiteral("0x1FFFFFFF") : QStringLiteral("0x7FF");
            const Json mask = filterObject.value("can_id_mask", defaultMask.toStdString());
            if (!mask.is_string() || !parseCanIdText(QString::fromStdString(mask.get<std::string>()),
                                                     &filter.canIdMask, error,
                                                     QStringLiteral("CAN filters.can_id_mask"))) {
                return false;
            }
            const quint32 maxId = filter.extended ? 0x1FFFFFFFU : 0x7FFU;
            if (filter.canId > maxId || filter.canIdMask > maxId) {
                return fail(error, QStringLiteral("CAN 过滤器 ID 与 extended 标志不匹配"));
            }
            channel->can.filters.append(filter);
        }
        return true;
    }

    return fail(error, QStringLiteral("不支持的通道类型：%1").arg(type));
}

} // 匿名命名空间

bool loadConfig(const QString &path, AgentConfig *config, QString *error)
{
    if (config == nullptr) {
        return fail(error, QStringLiteral("配置输出对象为空"));
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(error, QStringLiteral("无法打开配置文件：%1").arg(path));
    }

    const QByteArray bytes = file.readAll(); // 读取完整配置文件内容。
    try {
        const Json root = Json::parse(bytes.constData(), bytes.constData() + bytes.size()); // 解析 JSON 根对象。
        const int version = root.value("config_version", 1); // 读取配置版本。
        if (version != 1 && version != 2) {
            return fail(error, QStringLiteral("不支持的 config_version：%1").arg(version));
        }
        QString nodeId; // 网关节点标识。
        if (!readRequiredString(root, "node_id", &nodeId, error) || !kIdPattern.match(nodeId).hasMatch()) {
            return fail(error, QStringLiteral("node_id 不符合标识格式"));
        }
        if (!root.contains("channels") || !root.at("channels").is_array()) {
            return fail(error, QStringLiteral("channels 必须是数组"));
        }

        AgentConfig loaded; // 暂存完整配置，全部校验通过后再输出。
        loaded.configVersion = version;
        loaded.nodeId = nodeId;
        const Json broker = root.value("broker", Json::object()); // MQTT Broker 配置对象。
        loaded.broker.host = QString::fromStdString(broker.value("host", std::string("127.0.0.1")));
        if (loaded.broker.host.isEmpty()) {
            return fail(error, QStringLiteral("broker.host 不能为空"));
        }
        const int brokerPort = broker.value("port", 1883); // MQTT Broker 端口。
        if (brokerPort < 1 || brokerPort > 65535) {
            return fail(error, QStringLiteral("broker.port 超出范围：%1").arg(brokerPort));
        }
        loaded.broker.port = static_cast<quint16>(brokerPort);
        const QString protocol = QString::fromStdString(broker.value("protocol", std::string("mqtt311"))); // MQTT 协议版本。
        if (protocol != QStringLiteral("mqtt311")) {
            return fail(error, QStringLiteral("当前版本仅支持 MQTT 3.1.1"));
        }
        loaded.broker.clientId = QString::fromStdString(broker.value("client_id", std::string()));
        if (loaded.broker.clientId.isEmpty()) {
            loaded.broker.clientId = QStringLiteral("edgeagent-") + nodeId;
        }
        loaded.broker.keepaliveSeconds = broker.value("keepalive_s", 30);
        if (loaded.broker.keepaliveSeconds < 10 || loaded.broker.keepaliveSeconds > 120) {
            return fail(error, QStringLiteral("broker.keepalive_s 必须在 10 到 120 之间"));
        }
        loaded.broker.cleanSession = broker.value("clean_session", true);
        loaded.broker.username = QString::fromStdString(broker.value("username", std::string()));
        const QString passwordEnv = QString::fromStdString(broker.value("password_env", std::string())); // 密码环境变量名称。
        if (!passwordEnv.isEmpty()) {
            loaded.broker.password = qEnvironmentVariable(passwordEnv.toLatin1().constData());
            if (loaded.broker.password.isEmpty()) {
                return fail(error, QStringLiteral("broker 密码环境变量为空：%1").arg(passwordEnv));
            }
        }

        QSet<QString> channelIds; // 已使用的通道标识集合。
        QSet<QString> deviceIds; // 已使用的设备标识集合。
        for (const Json &value : root.at("channels")) { // 逐项加载并检查通道唯一性。
            if (!value.is_object()) {
                return fail(error, QStringLiteral("channels 中的每一项必须是对象"));
            }
            ChannelConfig channel; // 当前正在校验的通道配置。
            if (!parseChannel(value, &channel, error)) {
                return fail(error, QStringLiteral("通道校验失败：%1").arg(error == nullptr ? QString() : *error));
            }
            if (channelIds.contains(channel.channelId)) {
                return fail(error, QStringLiteral("channel_id 重复：%1").arg(channel.channelId));
            }
            channelIds.insert(channel.channelId);
            loaded.channels.append(channel);
            if (version == 1) {
                DeviceConfig device;
                if (!readRequiredString(value, "device_id", &device.deviceId, error)
                    || !kIdPattern.match(device.deviceId).hasMatch()
                    || deviceIds.contains(device.deviceId)
                    || !readIntegerField(value, "qos", 0, 0, 1, &device.qos, error, "qos")) {
                    return fail(error, QStringLiteral("v1 device_id 或 qos 无效/重复"));
                }
                if (!parseMessageFormat(value, &device, error)) return false;
                device.channelId = channel.channelId;
                deviceIds.insert(device.deviceId);
                loaded.devices.append(device);
            }
        }
        if (version == 2) {
            if (!root.contains("devices") || !root.at("devices").is_array() || root.at("devices").empty()) {
                return fail(error, QStringLiteral("v2 必须提供非空 devices 数组"));
            }
            QSet<QString> boundChannels;
            for (const Json &value : root.at("devices")) {
                DeviceConfig device;
                if (!readRequiredString(value, "device_id", &device.deviceId, error)
                    || !readRequiredString(value, "channel_id", &device.channelId, error)
                    || !kIdPattern.match(device.deviceId).hasMatch() || deviceIds.contains(device.deviceId)
                    || !channelIds.contains(device.channelId)) {
                    return fail(error, QStringLiteral("设备标识重复/无效或引用了不存在的通道"));
                }
                if (value.value("store_forward", Json::object()).value("enabled", false)) {
                    return fail(error, QStringLiteral("尚未实现 store_forward"));
                }
                // 兼容既有的透明绑定声明，不再选择或加载设备协议。
                if (value.contains("protocol") && value.at("protocol") != "transparent") {
                    return fail(error, QStringLiteral("设备仅支持透明收发绑定"));
                }
                const QSet<QString> allowedFields = {"device_id", "channel_id", "qos", "protocol", "store_forward", "message_format"};
                for (auto field = value.cbegin(); field != value.cend(); ++field) {
                    if (!allowedFields.contains(QString::fromStdString(field.key()))) {
                        return fail(error, QStringLiteral("不支持的设备配置字段：%1").arg(QString::fromStdString(field.key())));
                    }
                }
                if (!readIntegerField(value, "qos", 0, 0, 1, &device.qos, error, "qos")
                    || !parseMessageFormat(value, &device, error)) {
                    return false;
                }
                if (boundChannels.contains(device.channelId)) {
                    return fail(error, QStringLiteral("每个物理通道只能绑定一个设备 Topic"));
                }
                boundChannels.insert(device.channelId);
                deviceIds.insert(device.deviceId);
                loaded.devices.append(device);
            }
        }

        // 所有定义均先校验，随后仅输出启用通道及其 Topic 绑定。
        loaded.channels.erase(std::remove_if(loaded.channels.begin(), loaded.channels.end(),
            [](const ChannelConfig &channel) { return !channel.enabled; }), loaded.channels.end());
        QSet<QString> activeChannels;
        for (const auto &channel : loaded.channels) activeChannels.insert(channel.channelId);
        loaded.devices.erase(std::remove_if(loaded.devices.begin(), loaded.devices.end(),
            [&activeChannels](const DeviceConfig &device) { return !activeChannels.contains(device.channelId); }), loaded.devices.end());

        *config = loaded;
        return true;
    } catch (const Json::exception &) {
        return fail(error, QStringLiteral("配置文件 JSON 格式无效：%1").arg(path));
    }
}

} // 命名空间 EdgeAgent
