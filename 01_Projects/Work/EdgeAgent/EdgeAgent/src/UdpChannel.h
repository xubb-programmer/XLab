/**
 * @file UdpChannel.h
 * @brief UDP 通道接口。
 * @author xubb
 */

#pragma once

#include "Channel.h"

#include <QUdpSocket>

namespace EdgeAgent {

/**
 * @brief UDP 单播、广播和组播通道。
 *
 * 通道只接收 allowed_peers 中允许的来源，也只向允许的目标发送数据。
 */
class UdpChannel final : public Channel {
    Q_OBJECT

public:
    /**
     * @brief 创建 UDP 通道。
     * @param config 通道配置。
     * @param parent Qt 父对象。
     */
    explicit UdpChannel(const ChannelConfig &config, QObject *parent = nullptr);

    /** @brief 绑定 UDP 地址并启动接收。 */
    void start() override;

    /** @brief 停止接收并关闭 UDP 套接字。 */
    void stop() override;
    bool send(const TransportCommand &command) override
    { return sendDatagram(command.payload, command.address, command.port); }

    /**
     * @brief 向指定目标发送 UDP 数据报。
     * @param payload 要发送的原始数据。
     * @param address 目标 IP 地址。
     * @param port 目标端口。
     * @return 发送成功返回 true，否则返回 false。
     */
    bool sendDatagram(const QByteArray &payload, const QHostAddress &address, quint16 port);

    /**
     * @brief 判断目标是否通过对端白名单校验。
     * @param address 对端 IP 地址。
     * @param port 对端端口。
     * @return 允许通信返回 true，否则返回 false。
     */
    bool isAllowedPeer(const QHostAddress &address, quint16 port) const;

private slots:
    /** @brief 读取并处理套接字中的所有待接收数据报。 */
    void readPendingDatagrams();

private:
    /**
     * @brief 将 UDP 数据报转换为统一帧事件。
     * @param payload 数据报内容。
     * @param address 来源地址。
     * @param port 来源端口。
     */
    void emitDatagram(const QByteArray &payload, const QHostAddress &address, quint16 port);

    QUdpSocket m_socket; ///< UDP 套接字。
    bool m_running = false; ///< 通道是否已经启动。
};

} // 命名空间 EdgeAgent
