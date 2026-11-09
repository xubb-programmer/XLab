#pragma once
#include "Channel.h"
#include "MqttClient.h"
#include "TransportCodec.h"
#include <QHash>
namespace EdgeAgent {
/** 将物理通道与设备 Topic 双向绑定，不解释设备业务报文。 */
class EdgeAgent final : public QObject {
    Q_OBJECT
public:
    explicit EdgeAgent(const AgentConfig &config, QObject *parent = nullptr);
    ~EdgeAgent() override;
    void start();
    void stop();
private:
    void onMqttMessage(const QString &topic, const QByteArray &payload, int qos, bool retained);
    void publishResult(const QString &deviceId, const SendRequest &request, bool sent,
                       const QString &code, const QString &message);
    void publishTransparent(const QString &deviceId, const FrameEvent &event);
    QString topicForDevice(const QString &deviceId, const QString &leaf) const;
    AgentConfig m_config;
    QHash<QString, Channel *> m_channels;
    QHash<QString, DeviceConfig> m_devices;
    QHash<QString, QString> m_channelDevices;
    MqttClient *m_mqtt = nullptr;
    bool m_running = false;
};
}
