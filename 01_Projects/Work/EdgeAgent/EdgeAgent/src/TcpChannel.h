/**
 * @file TcpChannel.h
 * @brief TCP 客户端和服务端通道接口。
 * @author xubb
 */

#pragma once

#include "Channel.h"
#include "FrameCodec.h"

#include <QHash>
#include <QSharedPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

namespace EdgeAgent {

/** @brief 单个 TCP 连接的运行时上下文。 */
struct TcpPeerContext {
    FrameDecoder decoder; ///< 当前连接独立使用的分帧器。
    QTimer *idleTimer = nullptr; ///< 空闲分帧定时器。
    QString connectionId; ///< 当前连接的内部标识。
};

/**
 * @brief TCP 客户端或服务端通道。
 *
 * 服务端可同时管理多个连接；每个连接有独立的缓存、分帧器和连接标识。
 */
class TcpChannel final : public Channel {
    Q_OBJECT

public:
    /**
     * @brief 创建 TCP 通道。
     * @param config 通道配置。
     * @param parent Qt 父对象。
     */
    explicit TcpChannel(const ChannelConfig &config, QObject *parent = nullptr);

    /** @brief 启动 TCP 监听或发起客户端连接。 */
    void start() override;

    /** @brief 停止 TCP 连接并释放所有套接字。 */
    void stop() override;
    bool send(const TransportCommand &command) override;

    /**
     * @brief 向 TCP 对端发送一帧数据。
     * @param payload 要发送的数据。
     * @param connectionId 目标连接标识，服务端为空时选择第一个连接。
     * @return 发送成功返回 true，否则返回 false。
     */
    bool sendFrame(const QByteArray &payload, const QString &connectionId = {});

private slots:
    /** @brief 接收服务端待处理的新连接。 */
    void acceptPendingConnections();

    /** @brief 发起一次 TCP 客户端连接。 */
    void startClientConnection();

    /** @brief 处理客户端连接超时。 */
    void connectTimeout();

    /** @brief 处理重连定时器到期事件。 */
    void reconnectTimeout();

private:
    /**
     * @brief 为套接字建立连接级上下文和信号连接。
     * @param socket 要接管的 TCP 套接字。
     * @param connectionId 为套接字分配的内部连接标识。
     */
    void attachSocket(QTcpSocket *socket, const QString &connectionId);

    /**
     * @brief 读取并分帧处理 TCP 字节流。
     * @param socket 有新数据可读的 TCP 套接字。
     */
    void handleReadyRead(QTcpSocket *socket);

    /**
     * @brief 处理 TCP 连接断开。
     * @param socket 已断开的 TCP 套接字。
     */
    void handleDisconnected(QTcpSocket *socket);

    /**
     * @brief 处理 TCP 套接字错误。
     * @param socket 发生错误的 TCP 套接字。
     * @param error Qt 提供的套接字错误枚举值。
     */
    void handleSocketError(QTcpSocket *socket, QAbstractSocket::SocketError error);

    /**
     * @brief 处理空闲超时产生的末尾数据。
     * @param socket 触发空闲超时的 TCP 套接字。
     */
    void handleIdleTimeout(QTcpSocket *socket);

    /** @brief 按指数退避策略安排下一次重连。 */
    void scheduleReconnect();

    /**
     * @brief 将分帧结果转换为统一帧事件。
     * @param socket 数据来源 TCP 套接字。
     * @param frames 当前批次解析出的完整帧列表。
     */
    void emitFrames(QTcpSocket *socket, const QVector<QByteArray> &frames);

    /**
     * @brief 发布连接状态变化。
     * @param socket 要发布状态的 TCP 套接字。
     * @param state 状态名称，例如 connected 或 disconnected。
     */
    void setSocketState(QTcpSocket *socket, const QString &state);

    QTcpServer m_server; ///< TCP 服务端监听器。
    QTcpSocket *m_clientSocket = nullptr; ///< TCP 客户端套接字。
    QHash<QTcpSocket *, QSharedPointer<TcpPeerContext>> m_peers; ///< 套接字到连接上下文的映射。
    QTimer m_connectTimer; ///< 客户端连接超时定时器。
    QTimer m_reconnectTimer; ///< 客户端重连定时器。
    int m_nextReconnectMs = 1000; ///< 下一次重连等待时间。
    bool m_running = false; ///< 通道是否正在运行。
    bool m_stopping = false; ///< 是否正在主动停止。
};

} // 命名空间 EdgeAgent
