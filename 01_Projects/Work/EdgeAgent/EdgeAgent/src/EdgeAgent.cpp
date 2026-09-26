/**
 * @file EdgeAgent.cpp
 * @brief EdgeAgent 主协调器实现。
 * @author xubb
 */

#include "EdgeAgent.h"

#include "Channel.h"
#include "CanChannel.h"
#include "Logging.h"
#include "SerialChannel.h"
#include "TcpChannel.h"
#include "UdpChannel.h"

#include <nlohmann/json.hpp>

#include <QDateTime>
#include <QHostAddress>
#include <QUuid>

#include <stdexcept>

namespace EdgeAgent {
namespace {

using Json = nlohmann::json;

/** @brief 生成一个新的消息唯一标识。 */
QString newMessageId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

/** @brief 将 CAN ID 格式化为规范的 0x 前缀十六进制文本。 */
QString formatCanId(quint32 canId, bool extended)
{
    return QStringLiteral("0x%1").arg(canId, extended ? 8 : 3, 16, QLatin1Char('0')).toUpper().replace(0, 2, QStringLiteral("0x"));
}

/** @brief 解析 CAN JSON 中的 0x 前缀 ID。 */
bool parseCanId(const Json &command, quint32 *canId)
{
    if (canId == nullptr || !command.contains("can_id") || !command.at("can_id").is_string()) {
        return false;
    }
    const QString text = QString::fromStdString(command.at("can_id").get<std::string>());
    if (!text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) || text.size() <= 2) {
        return false;
    }
    bool ok = false; // 记录 CAN ID 转换结果。
    const qulonglong value = text.mid(2).toULongLong(&ok, 16);
    if (!ok || value > 0x1FFFFFFFULL) {
        return false;
    }
    *canId = static_cast<quint32>(value);
    return true;
}

/** @brief 将十六进制文本解码为原始字节。 */
bool decodeHex(const QString &text, QByteArray *result)
{
    if (result == nullptr || text.size() % 2 != 0) {
        return false;
    }
    const QByteArray upper = text.toLatin1().toUpper(); // 统一转为大写，便于校验和解码。
    for (char value : upper) { // 逐个检查十六进制字符。
        const bool digit = value >= '0' && value <= '9'; // 判断是否为数字。
        const bool letter = value >= 'A' && value <= 'F'; // 判断是否为十六进制字母。
        if (!digit && !letter) {
            return false;
        }
    }
    *result = QByteArray::fromHex(upper);
    return true;
}

/** @brief 从 JSON 对象读取并校验端口字段。 */
bool readJsonPort(const Json &object, const char *key, quint16 *port)
{
    if (port == nullptr || !object.contains(key) || !object.at(key).is_number_integer()) {
        return false;
    }
    const int value = object.at(key).get<int>(); // 先按整数读取，再检查端口范围。
    if (value < 1 || value > 65535) {
        return false;
    }
    *port = static_cast<quint16>(value);
    return true;
}

/** @brief 将 MQTT 消息正文解析为 JSON。 */
Json parsePayload(const QByteArray &payload)
{
    return Json::parse(payload.constData(), payload.constData() + payload.size());
}

} // 匿名命名空间

EdgeAgent::EdgeAgent(const AgentConfig &config, QObject *parent)
    : QObject(parent)
    , m_config(config)
{
    qRegisterMetaType<FrameEvent>();
    m_mqtt = new MqttClient(m_config.broker, m_config.nodeId, this);
    connect(m_mqtt, &MqttClient::connected, this, &EdgeAgent::onMqttConnected);
    connect(m_mqtt, &MqttClient::disconnected, this, &EdgeAgent::onMqttDisconnected);
    connect(m_mqtt, &MqttClient::messageReceived, this, &EdgeAgent::onMqttMessage);
    connect(m_mqtt, &MqttClient::fault, this, &EdgeAgent::onMqttFault);

    QStringList subscriptions; // 保存所有设备的 MQTT 下行命令 Topic。
    for (const ChannelConfig &channelConfig : m_config.channels) { // 根据配置创建传输通道。
        Channel *channel = nullptr; // 当前配置对应的通道对象。
        if (channelConfig.type == ChannelType::Udp) {
            channel = new UdpChannel(channelConfig, this);
        } else if (channelConfig.type == ChannelType::Tcp) {
            channel = new TcpChannel(channelConfig, this);
        } else if (channelConfig.type == ChannelType::Serial) {
            channel = new SerialChannel(channelConfig, this);
        } else if (channelConfig.type == ChannelType::Can) {
            channel = new CanChannel(channelConfig, this);
        }
        if (channel == nullptr) {
            continue;
        }
        // 所有通道统一连接到主协调器，由主协调器负责日志和 MQTT 转发。
        connect(channel, &Channel::frameReceived, this, &EdgeAgent::onFrameReceived);
        connect(channel, &Channel::stateChanged, this, &EdgeAgent::onStateChanged);
        connect(channel, &Channel::fault, this, &EdgeAgent::onFault);
        m_channels.append(channel);
        m_channelsByDevice.insert(channelConfig.deviceId, channel);
        subscriptions.append(topicForDevice(channelConfig.deviceId, QStringLiteral("commands")));
    }
    m_mqtt->setSubscriptions(subscriptions);
}

EdgeAgent::~EdgeAgent()
{
    stop();
}

void EdgeAgent::start()
{
    logging::write({{"event", "agent_starting"},
                    {"node_id", m_config.nodeId.toStdString()},
                    {"broker", QStringLiteral("%1:%2").arg(m_config.broker.host).arg(m_config.broker.port).toStdString()},
                    {"message", "EdgeAgent 开始启动"}});

    for (Channel *channel : m_channels) { // 先启动本地传输通道。
        channel->start();
    }
    if (!m_mqtt->start()) {
        logging::write({{"event", "mqtt_not_ready"},
                        {"node_id", m_config.nodeId.toStdString()},
                        {"message", "MQTT 未连接，继续运行本地通信通道"}}, true);
    }
}

void EdgeAgent::stop()
{
    for (Channel *channel : m_channels) { // 先停止本地传输通道。
        channel->stop();
    }
    if (m_mqtt != nullptr) {
        m_mqtt->stop();
    }
}

void EdgeAgent::onFrameReceived(const FrameEvent &event)
{
    logging::write({{"event", "rx"},
                    {"schema", "edge.transport.v1"},
                    {"node_id", m_config.nodeId.toStdString()},
                    {"channel_id", event.channelId.toStdString()},
                    {"device_id", event.deviceId.toStdString()},
                    {"transport", event.transport.toStdString()},
                    {"ts", event.timestampMs},
                    {"peer_ip", event.peerAddress.toStdString()},
                    {"peer_port", event.peerPort},
                    {"connection_id", event.connectionId.toStdString()},
                    {"endpoint", event.endpoint.toStdString()},
                    {"len", event.transport == QStringLiteral("can") ? event.canDlc : event.payload.size()},
                    {"data", event.payload.toHex().toUpper().toStdString()},
                    {"message", "收到通道数据"}});
    if (event.transport == QStringLiteral("can")) {
        logging::write({{"event", "can_rx"},
                        {"channel_id", event.channelId.toStdString()},
                        {"can_id", formatCanId(event.canId, event.canExtended).toStdString()},
                        {"extended", event.canExtended},
                        {"rtr", event.canRemote},
                        {"dlc", event.canDlc},
                        {"message", "收到 CAN 报文"}});
    }
    publishFrame(event);
}

void EdgeAgent::publishFrame(const FrameEvent &event)
{
    const Channel *channel = m_channelsByDevice.value(event.deviceId, nullptr); // 根据设备查找通道配置。
    if (channel == nullptr || m_mqtt == nullptr) {
        return;
    }

    const ChannelConfig &config = channel->config(); // 获取 MQTT QoS 等通道参数。
    const QString topic = topicForDevice(event.deviceId, QStringLiteral("data")); // 生成上行数据 Topic。
    QByteArray payload; // MQTT 发布正文。
    if (event.transport == QStringLiteral("udp")) {
        const Json object = { // UDP 使用 JSON 信封携带来源和重放信息。
            {"schema", "edge.udp.v1"},
            {"msg_id", newMessageId().toStdString()},
            {"src_ip", event.peerAddress.toStdString()},
            {"src_port", event.peerPort},
            {"ts", event.timestampMs},
            {"replay", false},
            {"len", event.payload.size()},
            {"data", event.payload.toHex().toUpper().toStdString()},
        };
        payload = QByteArray::fromStdString(object.dump());
    } else if (event.transport == QStringLiteral("can")) {
        const Json object = { // CAN 使用 JSON 信封携带 ID、帧类型和数据。
            {"schema", "edge.can.v1"},
            {"msg_id", newMessageId().toStdString()},
            {"can_id", formatCanId(event.canId, event.canExtended).toStdString()},
            {"extended", event.canExtended},
            {"rtr", event.canRemote},
            {"dlc", event.canDlc},
            {"ts", event.timestampMs},
            {"replay", false},
            {"data", event.canRemote ? std::string() : event.payload.toHex().toUpper().toStdString()},
        };
        payload = QByteArray::fromStdString(object.dump());
    } else {
        payload = event.payload;
    }

    if (!m_mqtt->publish(topic, payload, config.qos, false)) {
        logging::write({{"event", "mqtt_data_not_published"},
                        {"device_id", event.deviceId.toStdString()},
                        {"topic", topic.toStdString()},
                        {"message", "MQTT 数据发布失败"}}, true);
    }
}

void EdgeAgent::onMqttMessage(const QString &topic, const QByteArray &payload, int qos, bool retained)
{
    Q_UNUSED(qos)

    const QStringList parts = topic.split(QLatin1Char('/'), QString::KeepEmptyParts); // 按层级拆分 MQTT Topic。
    if (parts.size() != 5 || parts.at(0) != QStringLiteral("edge") || parts.at(1) != QStringLiteral("v1")
        || parts.at(2) != QStringLiteral("devices") || parts.at(4) != QStringLiteral("commands")) {
        onMqttFault(QStringLiteral("mqtt_topic_invalid"), QStringLiteral("MQTT Topic 格式无效：%1").arg(topic));
        return;
    }

    const QString deviceId = parts.at(3); // 从 Topic 中提取设备标识。
    Channel *channel = m_channelsByDevice.value(deviceId, nullptr); // 查找设备对应通道。
    if (channel == nullptr) {
        publishDeviceError(deviceId, QStringLiteral("unknown_device"), QStringLiteral("没有为该设备配置通道"));
        return;
    }
    if (retained) {
        publishCommandResult(deviceId, newMessageId(), QStringLiteral("failed"), QStringLiteral("retained_command"),
                             QStringLiteral("拒绝处理保留消息命令"));
        return;
    }

    if (channel->config().type == ChannelType::Udp) {
        try {
            const Json command = parsePayload(payload); // UDP 命令必须解析为 JSON 对象。
            if (!command.is_object()) {
                throw std::runtime_error("UDP 命令必须是 JSON 对象");
            }
            handleUdpCommand(channel, command);
        } catch (const std::exception &) {
            publishCommandResult(deviceId, newMessageId(), QStringLiteral("failed"), QStringLiteral("invalid_json"),
                                 QStringLiteral("UDP 命令 JSON 格式无效"));
        }
        return;
    }

    if (channel->config().type == ChannelType::Tcp) {
        handleTcpCommand(channel, payload);
    } else if (channel->config().type == ChannelType::Can) {
        handleCanCommand(channel, payload);
    } else {
        handleSerialCommand(channel, payload);
    }
}

void EdgeAgent::handleUdpCommand(Channel *channel, const Json &command)
{
    const QString deviceId = channel->config().deviceId; // 当前命令对应的设备。
    const QString messageId = QString::fromStdString(command.value("msg_id", newMessageId().toStdString())); // 命令消息标识。
    const QString targetIp = QString::fromStdString(command.value("target_ip", std::string())); // UDP 目标地址文本。
    QHostAddress targetAddress; // 解析后的 UDP 目标地址。
    QByteArray data; // 解码后的 UDP 原始数据。
    quint16 targetPort = 0; // UDP 目标端口。
    const QString dataText = QString::fromStdString(command.value("data", std::string())); // 十六进制数据文本。
    if (!targetAddress.setAddress(targetIp) || !readJsonPort(command, "target_port", &targetPort) || !decodeHex(dataText, &data)) {
        publishCommandResult(deviceId, messageId, QStringLiteral("failed"), QStringLiteral("invalid_command"),
                             QStringLiteral("必须提供 target_ip、target_port 和十六进制 data"));
        return;
    }

    auto *udp = qobject_cast<UdpChannel *>(channel); // 将基类指针转换为 UDP 通道。
    const bool sent = udp != nullptr && udp->sendDatagram(data, targetAddress, targetPort); // 执行实际发送。
    publishCommandResult(deviceId, messageId, sent ? QStringLiteral("sent") : QStringLiteral("failed"),
                         sent ? QStringLiteral("ok") : QStringLiteral("udp_send_failed"),
                         sent ? QStringLiteral("UDP 数据报发送成功") : QStringLiteral("UDP 数据报未发送"));
}

void EdgeAgent::handleTcpCommand(Channel *channel, const QByteArray &payload)
{
    const QString deviceId = channel->config().deviceId; // 当前命令对应的设备。
    QString messageId = newMessageId(); // 原始字节命令没有消息标识时使用自动生成值。
    QString connectionId; // TCP 目标连接标识，为空时由通道选择连接。
    QByteArray data = payload; // 默认将 MQTT 正文作为原始 TCP 数据。

    try {
        const Json command = parsePayload(payload); // 如果是 JSON，则尝试读取结构化命令字段。
        if (command.is_object() && command.contains("data")) {
            messageId = QString::fromStdString(command.value("msg_id", messageId.toStdString()));
            connectionId = QString::fromStdString(command.value("connection_id", std::string()));
            const QString dataText = QString::fromStdString(command.value("data", std::string())); // TCP 十六进制数据文本。
            if (!decodeHex(dataText, &data)) {
                publishCommandResult(deviceId, messageId, QStringLiteral("failed"), QStringLiteral("invalid_hex"),
                                     QStringLiteral("TCP JSON 命令的 data 必须是十六进制字符串"), connectionId);
                return;
            }
        }
    } catch (const Json::exception &) {
        // 非 JSON 的 TCP 命令按原始字节转发。
    }

    auto *tcp = qobject_cast<TcpChannel *>(channel); // 将基类指针转换为 TCP 通道。
    const bool sent = tcp != nullptr && tcp->sendFrame(data, connectionId); // 执行实际发送。
    publishCommandResult(deviceId, messageId, sent ? QStringLiteral("sent") : QStringLiteral("failed"),
                         sent ? QStringLiteral("ok") : QStringLiteral("tcp_send_failed"),
                         sent ? QStringLiteral("TCP 帧发送成功") : QStringLiteral("TCP 帧未发送"), connectionId);
}

void EdgeAgent::handleSerialCommand(Channel *channel, const QByteArray &payload)
{
    const QString deviceId = channel->config().deviceId; // 当前串口命令对应的设备。
    QString messageId = newMessageId(); // 原始字节命令没有消息标识时自动生成。
    QByteArray data = payload; // 默认将 MQTT 正文作为原始串口字节。

    try {
        const Json command = parsePayload(payload); // JSON 命令中的 data 字段按 HEX 解码。
        if (command.is_object() && command.contains("data")) {
            messageId = QString::fromStdString(command.value("msg_id", messageId.toStdString()));
            const QString dataText = QString::fromStdString(command.value("data", std::string())); // 串口 HEX 数据文本。
            if (!decodeHex(dataText, &data)) {
                publishCommandResult(deviceId, messageId, QStringLiteral("failed"), QStringLiteral("invalid_hex"),
                                     QStringLiteral("串口 JSON 命令的 data 必须是十六进制字符串"));
                return;
            }
        }
    } catch (const Json::exception &) {
        // 非 JSON 的 MQTT 正文按原始串口字节转发。
    }

    auto *serial = qobject_cast<SerialChannel *>(channel); // 将基类指针转换为串口通道。
    const bool sent = serial != nullptr && serial->sendBytes(data); // 将数据写入串口。
    publishCommandResult(deviceId, messageId, sent ? QStringLiteral("sent") : QStringLiteral("failed"),
                         sent ? QStringLiteral("ok") : QStringLiteral("serial_send_failed"),
                         sent ? QStringLiteral("串口数据发送成功") : QStringLiteral("串口数据未发送"));
}

void EdgeAgent::handleCanCommand(Channel *channel, const QByteArray &payload)
{
    const QString deviceId = channel->config().deviceId; // 当前 CAN 命令对应的设备。
    QString messageId = newMessageId(); // 缺少消息标识时自动生成。
    try {
        const Json command = parsePayload(payload); // CAN 下行必须使用 JSON 信封。
        if (!command.is_object() || command.value("schema", std::string()) != "edge.can.v1") {
            throw std::runtime_error("CAN 命令 schema 无效");
        }
        messageId = QString::fromStdString(command.value("msg_id", messageId.toStdString()));
        quint32 canId = 0; // 解析后的 CAN ID。
        const bool extended = command.value("extended", false); // 是否为扩展帧。
        const bool remote = command.value("rtr", false); // 是否为远程请求帧。
        if (!command.contains("extended") || !command.at("extended").is_boolean()
            || !command.contains("rtr") || !command.at("rtr").is_boolean() || !parseCanId(command, &canId)) {
            throw std::runtime_error("CAN ID 或帧类型字段无效");
        }
        const quint32 maxId = extended ? 0x1FFFFFFFU : 0x7FFU;
        if (canId > maxId || !command.contains("dlc") || !command.at("dlc").is_number_integer()) {
            throw std::runtime_error("CAN ID 或 DLC 超出范围");
        }
        const int dlc = command.at("dlc").get<int>(); // 经典 CAN 数据长度码。
        if (dlc < 0 || dlc > 8 || !command.contains("data") || !command.at("data").is_string()) {
            throw std::runtime_error("CAN DLC 或 data 字段无效");
        }
        QByteArray data; // 解码后的 CAN 数据。
        if (!decodeHex(QString::fromStdString(command.at("data").get<std::string>()), &data)) {
            throw std::runtime_error("CAN data 不是偶数个十六进制字符");
        }
        if ((remote && !data.isEmpty()) || (!remote && data.size() != dlc)) {
            throw std::runtime_error("CAN data 长度与 DLC 不匹配");
        }
        auto *can = qobject_cast<CanChannel *>(channel); // 将基类指针转换为 CAN 通道。
        const bool sent = can != nullptr && can->sendFrame(canId, extended, remote, dlc, data);
        publishCommandResult(deviceId, messageId, sent ? QStringLiteral("sent") : QStringLiteral("failed"),
                             sent ? QStringLiteral("ok") : QStringLiteral("can_send_failed"),
                             sent ? QStringLiteral("CAN 报文发送成功") : QStringLiteral("CAN 报文未发送"));
    } catch (const std::exception &exception) {
        Q_UNUSED(exception)
        publishCommandResult(deviceId, messageId, QStringLiteral("failed"), QStringLiteral("invalid_can_command"),
                             QStringLiteral("CAN JSON 命令格式无效"));
    }
}

void EdgeAgent::publishCommandResult(const QString &deviceId,
                                     const QString &messageId,
                                     const QString &stage,
                                     const QString &code,
                                     const QString &message,
                                     const QString &connectionId)
{
    if (m_mqtt == nullptr) {
        return;
    }
    Json object = { // 构造标准化命令结果消息。
        {"schema", "edge.command-result.v1"},
        {"msg_id", messageId.toStdString()},
        {"stage", stage.toStdString()},
        {"code", code.toStdString()},
        {"message", message.toStdString()},
        {"ts", QDateTime::currentMSecsSinceEpoch()},
    };
    if (!connectionId.isEmpty()) {
        object["connection_id"] = connectionId.toStdString();
    }
    m_mqtt->publish(topicForDevice(deviceId, QStringLiteral("command-results")), QByteArray::fromStdString(object.dump()), 1, false);
}

void EdgeAgent::publishDeviceError(const QString &deviceId, const QString &code, const QString &message)
{
    if (m_mqtt == nullptr) {
        return;
    }
    const Json object = { // 构造标准化设备错误消息。
        {"schema", "edge.error.v1"},
        {"code", code.toStdString()},
        {"message", message.toStdString()},
        {"ts", QDateTime::currentMSecsSinceEpoch()},
    };
    m_mqtt->publish(topicForDevice(deviceId, QStringLiteral("errors")), QByteArray::fromStdString(object.dump()), 1, false);
}

QString EdgeAgent::topicForDevice(const QString &deviceId, const QString &leaf) const
{
    return QStringLiteral("edge/v1/devices/%1/%2").arg(deviceId, leaf);
}

void EdgeAgent::onStateChanged(const QString &channelId, const QString &state)
{
    logging::write({{"event", "channel_state"},
                    {"node_id", m_config.nodeId.toStdString()},
                    {"channel_id", channelId.toStdString()},
                    {"state", state.toStdString()},
                    {"ts", QDateTime::currentMSecsSinceEpoch()},
                    {"message", "通道状态发生变化"}});
}

void EdgeAgent::onFault(const QString &channelId, const QString &code, const QString &message)
{
    logging::write({{"event", "fault"},
                    {"node_id", m_config.nodeId.toStdString()},
                    {"channel_id", channelId.toStdString()},
                    {"code", code.toStdString()},
                    {"message", QStringLiteral("通道故障：%1").arg(message).toStdString()},
                    {"ts", QDateTime::currentMSecsSinceEpoch()}}, true);
}

void EdgeAgent::onMqttConnected()
{
    logging::write({{"event", "mqtt_connected"},
                    {"node_id", m_config.nodeId.toStdString()},
                    {"broker", QStringLiteral("%1:%2").arg(m_config.broker.host).arg(m_config.broker.port).toStdString()},
                    {"message", "MQTT 连接成功"}});
}

void EdgeAgent::onMqttDisconnected(int reasonCode)
{
    logging::write({{"event", "mqtt_disconnected"},
                    {"node_id", m_config.nodeId.toStdString()},
                    {"reason_code", reasonCode},
                    {"message", "MQTT 连接断开"}}, true);
}

void EdgeAgent::onMqttFault(const QString &code, const QString &message)
{
    logging::write({{"event", "mqtt_fault"},
                    {"node_id", m_config.nodeId.toStdString()},
                    {"code", code.toStdString()},
                    {"message", QStringLiteral("MQTT 故障：%1").arg(message).toStdString()},
                    {"ts", QDateTime::currentMSecsSinceEpoch()}}, true);
}

} // 命名空间 EdgeAgent
