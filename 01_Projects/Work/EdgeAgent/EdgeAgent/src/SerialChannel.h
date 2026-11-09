/**
 * @file SerialChannel.h
 * @brief Qt5 串口通道接口。
 * @author xubb
 */

#pragma once

#include "Channel.h"
#include "FrameCodec.h"

#include <QSerialPort>
#include <QTimer>

namespace EdgeAgent {

/**
 * @brief 串口收发通道。
 *
 * 串口按连续字节流处理，使用 FrameDecoder 按配置拆帧；设备暂不可用时按退避间隔重试打开。
 */
class SerialChannel final : public Channel {
    Q_OBJECT

public:
    /**
     * @brief 创建串口通道。
     * @param config 已校验的通道配置。
     * @param parent Qt 父对象。
     */
    explicit SerialChannel(const ChannelConfig &config, QObject *parent = nullptr);

    /** @brief 打开串口并启动接收；打开失败时继续安排重试。 */
    void start() override;

    /** @brief 停止重试并关闭串口。 */
    void stop() override;
    bool send(const TransportCommand &command) override
    { return sendBytes(command.payload); }

    /**
     * @brief 向串口写入原始字节。
     * @param payload 要发送的字节，不会自动添加帧分隔符。
     * @return 数据已提交给串口发送队列时返回 true。
     */
    bool sendBytes(const QByteArray &payload);

private slots:
    /** @brief 读取新到达的串口字节并执行分帧。 */
    void readAvailable();

    /** @brief 将空闲超时后仍留在缓存中的字节作为一帧输出。 */
    void handleIdleTimeout();

    /**
     * @brief 处理 Qt 串口错误并在设备断开时安排重连。
     * @param error Qt SerialPort 错误枚举值。
     */
    void handleSerialError(QSerialPort::SerialPortError error);

    /** @brief 尝试重新打开串口。 */
    void retryOpen();

private:
    /** @brief 按当前配置设置串口参数并打开设备。 */
    bool openPort();

    /** @brief 进入断开状态并按指数退避策略安排重连。 */
    void scheduleReconnect();

    /**
     * @brief 将完整串口帧发布为统一数据事件。
     * @param payload 完整帧内容。
     */
    void emitFrame(const QByteArray &payload);

    QSerialPort m_port; ///< Qt 串口对象。
    FrameDecoder m_decoder; ///< 当前串口独立使用的分帧器。
    QTimer m_idleTimer; ///< 空闲分帧定时器。
    QTimer m_reconnectTimer; ///< 串口重新打开定时器。
    int m_nextReconnectMs = 1000; ///< 下一次串口重试等待时间。
    bool m_running = false; ///< 通道是否处于运行或重试状态。
};

} // 命名空间 EdgeAgent
