/**
 * @file CanChannel.h
 * @brief Qt Serial Bus CAN 通道接口。
 * @author xubb
 */

#pragma once

#include "Channel.h"

#include <QCanBusDevice>
#include <QPointer>
#include <QTimer>

namespace EdgeAgent {

/**
 * @brief 基于 Qt Serial Bus 的经典 CAN 通道。
 *
 * Linux 基线使用 socketcan 后端和 can0/vcan0 等接口。通道只处理经典 CAN，
 * 接收、发送前均校验 11/29 位 ID、DLC、RTR 和数据长度。
 */
class CanChannel final : public Channel {
    Q_OBJECT

public:
    /**
     * @brief 创建 CAN 通道。
     * @param config 已校验的 CAN 通道配置。
     * @param parent Qt 父对象。
     */
    explicit CanChannel(const ChannelConfig &config, QObject *parent = nullptr);

    /** @brief 创建 Qt CAN 设备并连接总线。 */
    void start() override;

    /** @brief 停止 CAN 设备和重连定时器。 */
    void stop() override;
    bool send(const TransportCommand &command) override
    { return sendFrame(command.canId, command.extended, command.remote, command.dlc, command.payload); }

    /**
     * @brief 发送一条经典 CAN 帧。
     * @param canId CAN ID。
     * @param extended 是否为扩展帧。
     * @param remote 是否为远程请求帧。
     * @param dlc 数据长度码。
     * @param payload 数据内容，远程帧必须为空。
     * @return 发送成功返回 true。
     */
    bool sendFrame(quint32 canId, bool extended, bool remote, int dlc, const QByteArray &payload);

private slots:
    /** @brief 读取 Qt CAN 设备缓存中的全部报文。 */
    void readFrames();

    /**
     * @brief 处理 Qt CAN 设备错误。
     * @param error Qt Serial Bus 错误类型。
     */
    void handleDeviceError(QCanBusDevice::CanBusError error);

    /**
     * @brief 处理 CAN 设备状态变化。
     * @param state Qt CAN 设备状态。
     */
    void handleDeviceState(QCanBusDevice::CanBusDeviceState state);

    /** @brief 按退避间隔重试连接。 */
    void retryConnect();

private:
    /** @brief 创建、配置并连接 CAN 设备。 */
    bool openDevice();

    /** @brief 关闭当前 CAN 设备但保留通道运行状态。 */
    void closeDevice();

    /** @brief 安排下一次 CAN 设备重连。 */
    void scheduleReconnect();

    /** @brief 将收到的 CAN 帧转换为统一事件。 */
    void emitFrame(const QCanBusFrame &frame);

    QPointer<QCanBusDevice> m_device; ///< Qt Serial Bus 设备对象。
    QTimer m_reconnectTimer; ///< CAN 重连定时器。
    int m_nextReconnectMs = 1000; ///< 下一次重连等待时间。
    bool m_running = false; ///< 通道是否处于运行或重试状态。
    bool m_stopping = false; ///< 是否正在主动停止通道。
};

} // 命名空间 EdgeAgent
