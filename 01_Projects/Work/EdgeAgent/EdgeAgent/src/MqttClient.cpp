/**
 * @file MqttClient.cpp
 * @brief libmosquitto MQTT 客户端实现。
 * @author xubb
 */

#include "MqttClient.h"

#include <mosquitto.h>
#include <nlohmann/json.hpp>

#include <QMutexLocker>
#include <QNetworkInterface>

namespace EdgeAgent {

MqttClient::MqttClient(const BrokerConfig &config, const QString &nodeId, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_nodeId(nodeId)
{
    mosquitto_lib_init();
}

MqttClient::~MqttClient()
{
    stop();
    mosquitto_lib_cleanup();
}

void MqttClient::setSubscriptions(const QStringList &topics)
{
    QMutexLocker locker(&m_mutex); // 保护订阅列表，避免与 MQTT 网络线程并发访问。
    m_subscriptions = topics;
}

bool MqttClient::start()
{
    QMutexLocker locker(&m_mutex); // 保护客户端句柄，避免重复启动。
    if (m_started) {
        return true;
    }

    m_client = mosquitto_new(m_config.clientId.toUtf8().constData(), m_config.cleanSession, this);
    if (m_client == nullptr) {
        emit fault(QStringLiteral("mqtt_create_failed"), QStringLiteral("创建 MQTT 客户端失败"));
        return false;
    }
    mosquitto_connect_callback_set(m_client, &MqttClient::onConnect);
    mosquitto_disconnect_callback_set(m_client, &MqttClient::onDisconnect);
    mosquitto_message_callback_set(m_client, &MqttClient::onMessage);

    if (!m_config.username.isEmpty()) {
        // 配置认证信息，并保存 libmosquitto 返回的错误码。
        const int rc = mosquitto_username_pw_set(m_client,
                                                 m_config.username.toUtf8().constData(),
                                                 m_config.password.toUtf8().constData());
        if (rc != MOSQ_ERR_SUCCESS) {
            emit fault(QStringLiteral("mqtt_auth_config_failed"), QStringLiteral("配置 MQTT 认证失败，错误码：%1").arg(rc));
            mosquitto_destroy(m_client);
            m_client = nullptr;
            return false;
        }
    }

    const QByteArray willTopic = QStringLiteral("edge/v1/gateways/%1/status").arg(m_nodeId).toUtf8(); // 遗嘱状态 Topic。
    const QByteArray willPayload = QByteArrayLiteral("offline"); // 异常断开时发布的状态。
    int rc = mosquitto_will_set(m_client, willTopic.constData(), willPayload.size(), willPayload.constData(), 1, true); // 配置保留遗嘱。
    if (rc != MOSQ_ERR_SUCCESS) {
        emit fault(QStringLiteral("mqtt_will_config_failed"), QStringLiteral("配置 MQTT 遗嘱失败，错误码：%1").arg(rc));
        mosquitto_destroy(m_client);
        m_client = nullptr;
        return false;
    }

    mosquitto_reconnect_delay_set(m_client, 1, 60, true);
    rc = mosquitto_connect_async(m_client, m_config.host.toUtf8().constData(), m_config.port, m_config.keepaliveSeconds);
    if (rc != MOSQ_ERR_SUCCESS) {
        emit fault(QStringLiteral("mqtt_connect_start_failed"), QStringLiteral("启动 MQTT 连接失败，错误码：%1").arg(rc));
        mosquitto_destroy(m_client);
        m_client = nullptr;
        return false;
    }
    rc = mosquitto_loop_start(m_client);
    if (rc != MOSQ_ERR_SUCCESS) {
        emit fault(QStringLiteral("mqtt_loop_start_failed"), QStringLiteral("启动 MQTT 网络循环失败，错误码：%1").arg(rc));
        mosquitto_destroy(m_client);
        m_client = nullptr;
        return false;
    }
    m_started = true;
    return true;
}

void MqttClient::stop()
{
    struct mosquitto *client = nullptr; // 临时保存客户端句柄，锁外执行停止操作。
    {
        QMutexLocker locker(&m_mutex); // 先在锁内摘取句柄并标记为未启动。
        if (!m_started && m_client == nullptr) {
            return;
        }
        client = m_client;
        m_started = false;
    }

    if (client != nullptr) {
        mosquitto_disconnect(client);
        mosquitto_loop_stop(client, true);
        QMutexLocker locker(&m_mutex); // 网络线程停止后再安全销毁客户端句柄。
        mosquitto_destroy(client);
        m_client = nullptr;
    }
}

bool MqttClient::publish(const QString &topic, const QByteArray &payload, int qos, bool retain)
{
    if (qos < 0 || qos > 1) {
        emit fault(QStringLiteral("mqtt_invalid_qos"), QStringLiteral("当前仅支持 QoS 0 和 QoS 1"));
        return false;
    }
    QMutexLocker locker(&m_mutex); // 保护发布调用使用的客户端句柄。
    if (m_client == nullptr || !m_started) {
        return false;
    }
    const QByteArray topicBytes = topic.toUtf8(); // MQTT Topic 的 UTF-8 字节表示。
    const int rc = mosquitto_publish(m_client, nullptr, topicBytes.constData(), payload.size(), payload.constData(), qos, retain); // 提交发布请求。
    if (rc != MOSQ_ERR_SUCCESS) {
        emit fault(QStringLiteral("mqtt_publish_failed"), QStringLiteral("MQTT 发布失败，错误码：%1").arg(rc));
        return false;
    }
    return true;
}

void MqttClient::onConnect(struct mosquitto *client, void *userdata, int reasonCode)
{
    Q_UNUSED(client)
    auto *self = static_cast<MqttClient *>(userdata); // 从回调上下文恢复 C++ 对象。
    if (self != nullptr) {
        self->handleConnect(reasonCode);
    }
}

void MqttClient::onDisconnect(struct mosquitto *client, void *userdata, int reasonCode)
{
    Q_UNUSED(client)
    auto *self = static_cast<MqttClient *>(userdata); // 从回调上下文恢复 C++ 对象。
    if (self != nullptr) {
        self->handleDisconnect(reasonCode);
    }
}

void MqttClient::onMessage(struct mosquitto *client, void *userdata, const struct mosquitto_message *message)
{
    Q_UNUSED(client)
    auto *self = static_cast<MqttClient *>(userdata); // 从回调上下文恢复 C++ 对象。
    if (self != nullptr && message != nullptr) {
        self->handleMessage(message);
    }
}

void MqttClient::handleConnect(int reasonCode)
{
    if (reasonCode != 0) {
        emit fault(QStringLiteral("mqtt_connect_rejected"), QStringLiteral("MQTT 服务端拒绝连接，返回码：%1").arg(reasonCode));
        return;
    }

    {
        QMutexLocker locker(&m_mutex);
        for (const QString &topic : m_subscriptions) { // 连接成功后重新订阅全部 Topic。
            const QByteArray topicBytes = topic.toUtf8(); // 当前订阅 Topic 的 UTF-8 字节表示。
            const int rc = mosquitto_subscribe(m_client, nullptr, topicBytes.constData(), 1); // 提交 QoS 1 订阅请求。
            if (rc != MOSQ_ERR_SUCCESS) {
                emit fault(QStringLiteral("mqtt_subscribe_failed"), QStringLiteral("MQTT 订阅失败，错误码：%1").arg(rc));
            }
        }
        const QString statusTopic = QStringLiteral("edge/v1/gateways/%1/status").arg(m_nodeId); // 网关状态 Topic。
        const QByteArray statusPayload = QByteArrayLiteral("online"); // 连接成功状态。
        const QByteArray topicBytes = statusTopic.toUtf8(); // 状态 Topic 的 UTF-8 字节表示。
        const int rc = mosquitto_publish(m_client, nullptr, topicBytes.constData(), statusPayload.size(), statusPayload.constData(), 1, true); // 发布保留状态。
        if (rc != MOSQ_ERR_SUCCESS) {
            emit fault(QStringLiteral("mqtt_status_publish_failed"), QStringLiteral("发布网关状态失败，错误码：%1").arg(rc));
        }

        const QString ifaceTopic = QStringLiteral("edge/v1/gateways/%1/interfaces").arg(m_nodeId);
        nlohmann::json ifaces = nlohmann::json::array();
        for (const auto &iface : QNetworkInterface::allInterfaces()) {
            if (iface.flags().testFlag(QNetworkInterface::IsUp) &&
                iface.flags().testFlag(QNetworkInterface::IsRunning) &&
                !iface.flags().testFlag(QNetworkInterface::IsLoopBack)) {
                for (const auto &entry : iface.addressEntries()) {
                    if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                        ifaces.push_back({
                            {"name", iface.name().toStdString()},
                            {"address", entry.ip().toString().toStdString()},
                            {"netmask", entry.netmask().toString().toStdString()}
                        });
                    }
                }
            }
        }
        const std::string ifacePayload = nlohmann::json({
            {"gateway", m_nodeId.toStdString()},
            {"interfaces", ifaces}
        }).dump();
        mosquitto_publish(m_client, nullptr, ifaceTopic.toUtf8().constData(),
                          static_cast<int>(ifacePayload.size()), ifacePayload.data(), 1, true);
    }
    emit connected();
}

void MqttClient::handleDisconnect(int reasonCode)
{
    emit disconnected(reasonCode);
}

void MqttClient::handleMessage(const struct mosquitto_message *message)
{
    const QString topic = QString::fromUtf8(message->topic == nullptr ? "" : message->topic); // 转换消息 Topic。
    const QByteArray payload(static_cast<const char *>(message->payload), message->payloadlen); // 复制消息正文，避免回调结束后失效。
    emit messageReceived(topic, payload, message->qos, message->retain != 0);
}

} // 命名空间 EdgeAgent
