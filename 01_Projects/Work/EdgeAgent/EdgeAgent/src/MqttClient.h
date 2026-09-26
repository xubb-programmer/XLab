/**
 * @file MqttClient.h
 * @brief libmosquitto MQTT 客户端封装。
 * @author xubb
 */

#pragma once

#include "Config.h"

#include <QObject>
#include <QMutex>
#include <QStringList>

struct mosquitto;
struct mosquitto_message;

namespace EdgeAgent {

/**
 * @brief MQTT 3.1.1 客户端。
 *
 * 封装 libmosquitto 的线程循环、连接回调、订阅和发布操作。
 */
class MqttClient final : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 创建 MQTT 客户端。
     * @param config Broker 配置。
     * @param nodeId 网关节点标识，用于生成状态 Topic。
     * @param parent Qt 父对象。
     */
    explicit MqttClient(const BrokerConfig &config, const QString &nodeId, QObject *parent = nullptr);

    /** @brief 停止客户端并释放 libmosquitto 资源。 */
    ~MqttClient() override;

    /**
     * @brief 设置连接成功后的订阅列表。
     * @param topics MQTT Topic 列表。
     */
    void setSubscriptions(const QStringList &topics);

    /**
     * @brief 启动连接和 MQTT 网络循环。
     * @return 成功创建客户端并启动网络循环返回 true，否则返回 false。
     */
    bool start();

    /**
     * @brief 断开连接并停止网络循环。
     *
     * 函数会等待 libmosquitto 的网络线程退出，再销毁客户端句柄。
     */
    void stop();

    /**
     * @brief 发布 MQTT 消息。
     * @param topic 目标 Topic。
     * @param payload 消息内容。
     * @param qos MQTT QoS，目前支持 0 和 1。
     * @param retain 是否设置保留消息标志。
     * @return 发布请求提交成功返回 true，否则返回 false。
     */
    bool publish(const QString &topic, const QByteArray &payload, int qos, bool retain);

signals:
    /** @brief MQTT 连接成功。 */
    void connected();

    /**
     * @brief MQTT 连接断开。
     * @param reasonCode libmosquitto 返回的断开原因码。
     */
    void disconnected(int reasonCode);

    /**
     * @brief 收到 MQTT 消息。
     * @param topic 完整 MQTT Topic。
     * @param payload 消息正文。
     * @param qos 消息的 MQTT QoS。
     * @param retained 是否为保留消息。
     */
    void messageReceived(const QString &topic, const QByteArray &payload, int qos, bool retained);

    /**
     * @brief MQTT 操作发生故障。
     * @param code 机器可读错误码。
     * @param message 中文错误说明。
     */
    void fault(const QString &code, const QString &message);

private:
    /**
     * @brief libmosquitto 连接回调。
     * @param client libmosquitto 客户端句柄。
     * @param userdata 创建客户端时传入的 MqttClient 对象指针。
     * @param reasonCode 连接结果码。
     */
    static void onConnect(struct mosquitto *client, void *userdata, int reasonCode);

    /**
     * @brief libmosquitto 断开回调。
     * @param client libmosquitto 客户端句柄。
     * @param userdata 创建客户端时传入的 MqttClient 对象指针。
     * @param reasonCode 断开原因码。
     */
    static void onDisconnect(struct mosquitto *client, void *userdata, int reasonCode);

    /**
     * @brief libmosquitto 消息回调。
     * @param client libmosquitto 客户端句柄。
     * @param userdata 创建客户端时传入的 MqttClient 对象指针。
     * @param message libmosquitto 提供的消息对象。
     */
    static void onMessage(struct mosquitto *client, void *userdata, const struct mosquitto_message *message);

    /**
     * @brief 处理连接回调并完成订阅、上线状态发布。
     * @param reasonCode 连接结果码。
     */
    void handleConnect(int reasonCode);

    /**
     * @brief 处理断开回调。
     * @param reasonCode 断开原因码。
     */
    void handleDisconnect(int reasonCode);

    /**
     * @brief 将 libmosquitto 消息转换为 Qt 信号。
     * @param message libmosquitto 提供的消息对象。
     */
    void handleMessage(const struct mosquitto_message *message);

    BrokerConfig m_config; ///< Broker 连接配置。
    QString m_nodeId; ///< 网关节点标识。
    QStringList m_subscriptions; ///< 连接成功后要订阅的 Topic 列表。
    struct mosquitto *m_client = nullptr; ///< libmosquitto 客户端句柄。
    QMutex m_mutex; ///< 保护 MQTT 客户端句柄和订阅列表。
    bool m_started = false; ///< 网络循环是否已经启动。
};

} // 命名空间 EdgeAgent
