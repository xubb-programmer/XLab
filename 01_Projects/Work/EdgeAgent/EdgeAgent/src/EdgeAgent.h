/**
 * @file EdgeAgent.h
 * @brief EdgeAgent 主协调器接口。
 * @author xubb
 */

#pragma once

#include "Config.h"
#include "FrameEvent.h"
#include "MqttClient.h"

#include <nlohmann/json.hpp>

#include <QHash>
#include <QObject>
#include <QVector>

namespace EdgeAgent {

class Channel;

/**
 * @brief 协调 UDP、TCP、串口、CAN 通道和 MQTT 的主对象。
 *
 * 负责创建通道、转换统一事件、发布 MQTT 上行数据，并处理 MQTT 下行命令。
 */
class EdgeAgent final : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 创建 EdgeAgent。
     * @param config 已校验的全局配置。
     * @param parent Qt 父对象。
     */
    explicit EdgeAgent(const AgentConfig &config, QObject *parent = nullptr);

    /** @brief 停止所有通道和 MQTT 客户端。 */
    ~EdgeAgent() override;

    /**
     * @brief 启动所有传输通道和 MQTT 客户端。
     *
     * 本地通信通道先启动；MQTT 连接失败时仍保留 UDP、TCP、串口和 CAN 通道运行。
     */
    void start();

    /**
     * @brief 停止所有传输通道和 MQTT 客户端。
     *
     * 停止过程可重复调用，适合由析构函数和 Qt 退出信号共同触发。
     */
    void stop();

private slots:
    /**
     * @brief 处理通道收到的数据帧。
     * @param event UDP、TCP、串口或 CAN 统一格式的数据事件。
     */
    void onFrameReceived(const FrameEvent &event);

    /**
     * @brief 处理通道状态变化。
     * @param channelId 发生变化的通道标识。
     * @param state 新状态名称。
     */
    void onStateChanged(const QString &channelId, const QString &state);

    /**
     * @brief 处理通道故障。
     * @param channelId 发生故障的通道标识。
     * @param code 机器可读错误码。
     * @param message 中文错误说明。
     */
    void onFault(const QString &channelId, const QString &code, const QString &message);

    /** @brief 处理 MQTT 连接成功事件。 */
    void onMqttConnected();

    /**
     * @brief 处理 MQTT 连接断开事件。
     * @param reasonCode libmosquitto 返回的断开原因码。
     */
    void onMqttDisconnected(int reasonCode);

    /**
     * @brief 处理 MQTT 下行消息。
     * @param topic 收到消息的完整 Topic。
     * @param payload 收到的消息正文。
     * @param qos 消息使用的 MQTT QoS。
     * @param retained 是否为 Broker 保留消息。
     */
    void onMqttMessage(const QString &topic, const QByteArray &payload, int qos, bool retained);

    /**
     * @brief 处理 MQTT 故障。
     * @param code 机器可读错误码。
     * @param message 中文错误说明。
     */
    void onMqttFault(const QString &code, const QString &message);

private:
    /**
     * @brief 将统一数据帧发布到 MQTT。
     * @param event 要发布的数据事件。
     */
    void publishFrame(const FrameEvent &event);

    /**
     * @brief 解析并执行 UDP 下行命令。
     * @param channel 目标 UDP 通道。
     * @param command 已解析的 JSON 命令对象。
     */
    void handleUdpCommand(Channel *channel, const nlohmann::json &command);

    /**
     * @brief 解析并执行 TCP 下行命令。
     * @param channel 目标 TCP 通道。
     * @param payload MQTT 命令正文，可以是 JSON 或原始字节。
     */
    void handleTcpCommand(Channel *channel, const QByteArray &payload);

    /**
     * @brief 解析并执行串口下行命令。
     * @param channel 目标串口通道。
     * @param payload MQTT 命令正文，可以是包含 HEX data 的 JSON 或原始字节。
     */
    void handleSerialCommand(Channel *channel, const QByteArray &payload);

    /**
     * @brief 解析并执行 CAN 下行命令。
     * @param channel 目标 CAN 通道。
     * @param payload 符合 edge.can.v1 的 JSON 命令正文。
     */
    void handleCanCommand(Channel *channel, const QByteArray &payload);

    /**
     * @brief 发布 MQTT 命令执行结果。
     * @param deviceId 设备标识。
     * @param messageId 命令消息标识。
     * @param stage 执行阶段，例如 sent 或 failed。
     * @param code 机器可读结果码。
     * @param message 面向调用方的中文结果说明。
     * @param connectionId TCP 目标连接标识，UDP 或未指定连接时为空。
     */
    void publishCommandResult(const QString &deviceId,
                              const QString &messageId,
                              const QString &stage,
                              const QString &code,
                              const QString &message,
                              const QString &connectionId = {});

    /**
     * @brief 发布设备级错误消息。
     * @param deviceId 设备标识。
     * @param code 机器可读错误码。
     * @param message 面向调用方的中文错误说明。
     */
    void publishDeviceError(const QString &deviceId, const QString &code, const QString &message);

    /**
     * @brief 生成设备级 Topic。
     * @param deviceId 设备标识。
     * @param leaf Topic 末级名称。
     * @return 完整 MQTT Topic。
     */
    QString topicForDevice(const QString &deviceId, const QString &leaf) const;

    AgentConfig m_config; ///< EdgeAgent 全局配置。
    QVector<Channel *> m_channels; ///< 所有已创建的传输通道。
    QHash<QString, Channel *> m_channelsByDevice; ///< 设备标识到通道对象的映射。
    MqttClient *m_mqtt = nullptr; ///< MQTT 客户端对象。
};

} // 命名空间 EdgeAgent
