#include "EdgeAgent.h"
#include "CanChannel.h"
#include "Logging.h"
#include "SerialChannel.h"
#include "TcpChannel.h"
#include "UdpChannel.h"
#include <QDateTime>
#include <QUuid>

namespace EdgeAgent {
namespace {
QString messageId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
}

EdgeAgent::EdgeAgent(const AgentConfig &config, QObject *parent)
    : QObject(parent), m_config(config)
{
    qRegisterMetaType<FrameEvent>();
    for (const DeviceConfig &device : config.devices) {
        m_devices.insert(device.deviceId, device);
        m_channelDevices.insert(device.channelId, device.deviceId);
    }
    for (const ChannelConfig &c : config.channels) {
        Channel *channel = nullptr;
        switch (c.type) {
        case ChannelType::Udp: channel = new UdpChannel(c, this); break;
        case ChannelType::Tcp: channel = new TcpChannel(c, this); break;
        case ChannelType::Serial: channel = new SerialChannel(c, this); break;
        case ChannelType::Can: channel = new CanChannel(c, this); break;
        }
        m_channels.insert(c.channelId, channel);
        connect(channel, &Channel::stateChanged, this, [](const QString &id, const QString &state) {
            logging::write({{"event", "channel_state"}, {"channel_id", id.toStdString()},
                            {"state", state.toStdString()}, {"ts", QDateTime::currentMSecsSinceEpoch()}});
        });
        connect(channel, &Channel::fault, this, [](const QString &id, const QString &code, const QString &message) {
            logging::write({{"event", "fault"}, {"channel_id", id.toStdString()},
                            {"code", code.toStdString()}, {"message", message.toStdString()}}, true);
        });
        connect(channel, &Channel::frameReceived, this, [this](const FrameEvent &event) {
            logging::write({{"event", "rx"}, {"schema", "edge.transport.v1"},
                            {"channel_id", event.channelId.toStdString()}, {"transport", event.transport.toStdString()},
                            {"connection_id", event.connectionId.toStdString()}, {"ts", event.timestampMs},
                            {"len", event.payload.size()}, {"data", event.payload.toHex().toUpper().toStdString()}});
            if (m_running && m_channelDevices.contains(event.channelId))
                publishTransparent(m_channelDevices.value(event.channelId), event);
        });
    }
    m_mqtt = new MqttClient(config.broker, config.nodeId, this);
    QStringList subscriptions;
    for (const DeviceConfig &device : config.devices)
        subscriptions.append(topicForDevice(device.deviceId, "commands"));
    m_mqtt->setSubscriptions(subscriptions);
    connect(m_mqtt, &MqttClient::messageReceived, this, &EdgeAgent::onMqttMessage);
    connect(m_mqtt, &MqttClient::fault, this, [](const QString &code, const QString &message) {
        logging::write({{"event", "mqtt_fault"}, {"code", code.toStdString()}, {"message", message.toStdString()}}, true);
    });
    connect(m_mqtt, &MqttClient::connected, this, [] { logging::write({{"event", "mqtt_connected"}}); });
    connect(m_mqtt, &MqttClient::disconnected, this, [](int reason) {
        logging::write({{"event", "mqtt_disconnected"}, {"reason_code", reason}}, true);
    });
}

EdgeAgent::~EdgeAgent()
{
    stop();
}

void EdgeAgent::start()
{
    if (m_running) return;
    m_running = true;
    logging::write({{"event", "agent_starting"}, {"node_id", m_config.nodeId.toStdString()},
                    {"channels", m_config.channels.size()}, {"devices", m_config.devices.size()}});
    for (Channel *channel : m_channels) channel->start();
    m_mqtt->start();
}

void EdgeAgent::stop()
{
    if (!m_running) return;
    m_running = false;
    for (Channel *channel : m_channels) channel->stop();
    m_mqtt->stop();
}

QString EdgeAgent::topicForDevice(const QString &deviceId, const QString &leaf) const
{
    return QStringLiteral("edge/v1/devices/%1/%2").arg(deviceId, leaf);
}

void EdgeAgent::onMqttMessage(const QString &topic, const QByteArray &payload, int qos, bool retained)
{
    Q_UNUSED(qos)
    const QStringList parts = topic.split('/');
    if (parts.size() != 5 || parts[0] != "edge" || parts[1] != "v1" || parts[2] != "devices" || parts[4] != "commands") return;
    const QString deviceId = parts[3];
    SendRequest request;
    request.messageId = messageId();
    auto reject = [this, &request, &deviceId](const QString &code, const QString &message) {
        publishResult(deviceId, request, false, code, message);
    };
    if (!m_running) { reject("stopped", "代理未运行"); return; }
    const auto device = m_devices.constFind(deviceId);
    if (device == m_devices.cend()) { reject("unknown_device", "未配置目标设备"); return; }
    Channel *channel = m_channels.value(device->channelId);
    QString code, error;
    const bool valid = decodeSendRequest(channel->config().type, payload, &request, &code, &error);
    if (retained) { reject("retained_command", "拒绝保留消息命令"); return; }
    if (!valid) {
        reject(code, error);
        return;
    }
    if (!matchesLocalEndpoint(channel->config(), request.command)) {
        reject("invalid_request", "meta.endpoint.address 与配置的本地接口不一致");
        return;
    }
    const bool sent = channel->send(request.command);
    publishResult(deviceId, request, sent, sent ? "ok" : "send_failed",
                  sent ? "数据已提交到传输接口" : "传输接口未接受数据");
}

void EdgeAgent::publishResult(const QString &deviceId, const SendRequest &request, bool sent,
                              const QString &code, const QString &message)
{
    const auto format = m_devices.value(deviceId).messageFormat;
    m_mqtt->publish(topicForDevice(deviceId, "command-results"),
                   encodeSendResult(deviceId, request, sent, code, message, format), 1, false);
}

void EdgeAgent::publishTransparent(const QString &deviceId, const FrameEvent &event)
{
    const auto device = m_devices.constFind(deviceId);
    if (device == m_devices.cend()) return;
    const auto payload = encodeReceivedFrame(event, device->messageFormat);
    if (!m_mqtt->publish(topicForDevice(deviceId, "data"), payload, device->qos, false))
        logging::write({{"event", "mqtt_data_not_published"}, {"device_id", deviceId.toStdString()}}, true);
}
}
