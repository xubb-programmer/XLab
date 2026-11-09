/**
 * @file TcpChannel.cpp
 * @brief TCP 客户端和服务端通道实现。
 * @author xubb
 */

#include "TcpChannel.h"

#include <QDateTime>
#include <QUuid>

namespace EdgeAgent {

TcpChannel::TcpChannel(const ChannelConfig &config, QObject *parent)
    : Channel(config, parent)
{
    m_connectTimer.setSingleShot(true);
    m_reconnectTimer.setSingleShot(true);
    connect(&m_server, &QTcpServer::newConnection, this, &TcpChannel::acceptPendingConnections);
    connect(&m_connectTimer, &QTimer::timeout, this, &TcpChannel::connectTimeout);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &TcpChannel::reconnectTimeout);
}

void TcpChannel::start()
{
    if (m_running) {
        return;
    }
    m_stopping = false;
    m_running = true;
    m_nextReconnectMs = m_config.tcp.reconnectInitialMs;

    if (m_config.tcp.role == QStringLiteral("server")) {
        if (!m_server.listen(m_config.tcp.listenAddress, m_config.tcp.listenPort)) {
            reportFault(QStringLiteral("tcp_listen_failed"), QStringLiteral("TCP 监听失败"));
            m_running = false;
            setState(QStringLiteral("error"));
            return;
        }
        setState(QStringLiteral("ready"));
        return;
    }

    setState(QStringLiteral("reconnecting"));
    startClientConnection();
}

void TcpChannel::stop()
{
    if (!m_running && !m_server.isListening() && m_peers.isEmpty() && m_clientSocket == nullptr) {
        return;
    }
    m_stopping = true;
    m_connectTimer.stop();
    m_reconnectTimer.stop();
    m_server.close();

    const auto peers = m_peers.keys(); // 复制连接列表，避免删除过程中修改哈希表。
    for (QTcpSocket *socket : peers) { // 逐个关闭现有连接。
        if (socket != nullptr) {
            socket->disconnectFromHost();
            if (socket->state() != QAbstractSocket::UnconnectedState) {
                socket->abort();
            }
            handleDisconnected(socket);
        }
    }
    m_peers.clear();
    m_clientSocket = nullptr;
    m_running = false;
    setState(QStringLiteral("stopped"));
}

bool TcpChannel::send(const TransportCommand &command)
{
    if (!command.requireUniqueTarget && command.endpoint.isEmpty())
        return sendFrame(command.payload, command.connectionId);
    QString connection;
    int matches = 0;
    for (auto it = m_peers.cbegin(); it != m_peers.cend(); ++it) {
        if (it.key()->state() != QAbstractSocket::ConnectedState) continue;
        if (!command.connectionId.isEmpty() && it.value()->connectionId != command.connectionId) continue;
        if (!command.endpoint.isEmpty()
            && (it.key()->peerAddress() != command.address || it.key()->peerPort() != command.port)) continue;
        connection = it.value()->connectionId;
        ++matches;
    }
    if (matches != 1) {
        reportFault(matches ? QStringLiteral("tcp_target_ambiguous") : QStringLiteral("tcp_send_not_connected"),
                    matches ? QStringLiteral("多个 TCP 对端匹配，请指定 meta.connection 或 meta.endpoint")
                            : QStringLiteral("没有匹配的已连接 TCP 对端"));
        return false;
    }
    return sendFrame(command.payload, connection);
}

bool TcpChannel::sendFrame(const QByteArray &payload, const QString &connectionId)
{
    QTcpSocket *target = nullptr; // 最终选中的发送目标。
    if (m_config.tcp.role == QStringLiteral("client")) {
        target = m_clientSocket;
    } else {
        for (auto it = m_peers.cbegin(); it != m_peers.cend(); ++it) { // 查找指定连接或第一个可用连接。
            if (connectionId.isEmpty() || it.value()->connectionId == connectionId) {
                target = it.key();
                break;
            }
        }
    }

    if (target == nullptr || target->state() != QAbstractSocket::ConnectedState) {
        reportFault(QStringLiteral("tcp_send_not_connected"), QStringLiteral("没有已连接的 TCP 对端"));
        return false;
    }
    if (target->write(payload) != payload.size()) {
        reportFault(QStringLiteral("tcp_send_failed"), QStringLiteral("TCP 发送失败"));
        return false;
    }
    return true;
}

void TcpChannel::acceptPendingConnections()
{
    while (m_server.hasPendingConnections()) {
        QTcpSocket *socket = m_server.nextPendingConnection(); // 取出一个待处理连接。
        if (m_peers.size() >= m_config.tcp.maxClients) {
            reportFault(QStringLiteral("tcp_max_clients"), QStringLiteral("已达到 max_clients，拒绝新的 TCP 连接"));
            socket->disconnectFromHost();
            socket->deleteLater();
            continue;
        }
        const QString connectionId = QStringLiteral("server-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)); // 每次接入独立标识，避免描述符复用后旧目标误匹配。
        attachSocket(socket, connectionId);
    }
}

void TcpChannel::startClientConnection()
{
    if (!m_running || m_stopping || m_clientSocket != nullptr) {
        return;
    }

    m_clientSocket = new QTcpSocket(this); // Qt 父对象负责最终释放客户端套接字。
    attachSocket(m_clientSocket, QStringLiteral("client-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    m_connectTimer.start(m_config.tcp.connectTimeoutMs);
    m_clientSocket->connectToHost(m_config.tcp.remoteHost, m_config.tcp.remotePort);
}

void TcpChannel::connectTimeout()
{
    if (m_clientSocket != nullptr && m_clientSocket->state() != QAbstractSocket::ConnectedState) {
        reportFault(QStringLiteral("tcp_connect_timeout"), QStringLiteral("TCP 连接超时"));
        QTcpSocket *socket = m_clientSocket;
        socket->abort();
        handleDisconnected(socket);
    }
}

void TcpChannel::reconnectTimeout()
{
    startClientConnection();
}

void TcpChannel::attachSocket(QTcpSocket *socket, const QString &connectionId)
{
    auto context = QSharedPointer<TcpPeerContext>::create(); // 为该连接创建独立运行时上下文。
    context->decoder = FrameDecoder(m_config.frame);
    context->connectionId = connectionId;
    if (m_config.frame.kind == FrameKind::IdleTimeout) {
        context->idleTimer = new QTimer(socket);
        context->idleTimer->setSingleShot(true);
        context->idleTimer->setInterval(m_config.frame.idleMs);
        connect(context->idleTimer, &QTimer::timeout, this, [this, socket]() { handleIdleTimeout(socket); });
    }
    m_peers.insert(socket, context);

    connect(socket, &QTcpSocket::connected, this, [this, socket]() {
        if (socket == m_clientSocket) {
            m_connectTimer.stop();
            m_nextReconnectMs = m_config.tcp.reconnectInitialMs;
            setState(QStringLiteral("ready"));
        }
        setSocketState(socket, QStringLiteral("connected"));
    });

    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { handleReadyRead(socket); });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket]() { handleDisconnected(socket); });
    connect(socket, static_cast<void (QAbstractSocket::*)(QAbstractSocket::SocketError)>(&QAbstractSocket::error), this, [this, socket](QAbstractSocket::SocketError error) {
        handleSocketError(socket, error);
    });
}

void TcpChannel::handleReadyRead(QTcpSocket *socket)
{
    const auto context = m_peers.value(socket); // 获取连接对应的分帧上下文。
    if (context.isNull()) {
        return;
    }
    if (context->idleTimer != nullptr) {
        context->idleTimer->start();
    }

    QVector<QByteArray> frames; // 本次读取解析出的完整帧。
    QString error; // 分帧失败时的中文错误说明。
    if (!context->decoder.feed(socket->readAll(), &frames, &error)) {
        reportFault(QStringLiteral("tcp_frame_invalid"), error);
        socket->abort();
        return;
    }
    emitFrames(socket, frames);
}

void TcpChannel::handleDisconnected(QTcpSocket *socket)
{
    if (!m_peers.contains(socket)) return;
    const bool wasClient = socket == m_clientSocket; // 判断断开的是否为客户端连接。
    const auto context = m_peers.take(socket); // 从连接表移除并取出上下文。
    if (!context.isNull()) {
        if (context->idleTimer) context->idleTimer->stop();
        emit stateChanged(m_config.channelId, QStringLiteral("disconnected:%1").arg(context->connectionId));
    }
    if (wasClient) {
        m_connectTimer.stop();
        m_clientSocket = nullptr;
        if (!m_stopping) {
            scheduleReconnect();
        }
    }
    socket->disconnect(this);
    socket->deleteLater();
}

void TcpChannel::handleSocketError(QTcpSocket *socket, QAbstractSocket::SocketError error)
{
    if (socket == nullptr || m_stopping) {
        return;
    }
    if (error == QAbstractSocket::RemoteHostClosedError) {
        return;
    }
    reportFault(QStringLiteral("tcp_socket_error"), QStringLiteral("TCP 套接字发生错误"));
    socket->abort();
    handleDisconnected(socket);
}

void TcpChannel::handleIdleTimeout(QTcpSocket *socket)
{
    const auto context = m_peers.value(socket); // 获取空闲超时对应的连接上下文。
    if (context.isNull() || !context->decoder.hasBuffered()) {
        return;
    }
    const QByteArray frame = context->decoder.takeBuffered(); // 取出空闲时仍未遇到分隔符的数据。
    if (!frame.isEmpty()) {
        QVector<QByteArray> frames; // 将空闲缓存包装成单帧输出。
        frames.append(frame);
        emitFrames(socket, frames);
    }
}

void TcpChannel::scheduleReconnect()
{
    if (m_stopping || !m_running || m_config.tcp.role != QStringLiteral("client")) {
        return;
    }
    setState(QStringLiteral("reconnecting"));
    m_reconnectTimer.start(m_nextReconnectMs);
    m_nextReconnectMs = static_cast<int>(qMin<qint64>(static_cast<qint64>(m_nextReconnectMs) * 2, m_config.tcp.reconnectMaxMs));
}

void TcpChannel::emitFrames(QTcpSocket *socket, const QVector<QByteArray> &frames)
{
    const auto context = m_peers.value(socket); // 获取待发布状态的连接上下文。
    if (context.isNull()) {
        return;
    }
    for (const QByteArray &payload : frames) {
        FrameEvent event; // 为当前完整 TCP 帧组装统一事件。
        event.channelId = m_config.channelId;
        event.transport = QStringLiteral("tcp");
        event.payload = payload;
        event.peerAddress = socket->peerAddress().toString();
        event.peerPort = socket->peerPort();
        event.connectionId = context->connectionId;
        event.timestampMs = QDateTime::currentMSecsSinceEpoch();
        emit frameReceived(event);
    }
}

void TcpChannel::setSocketState(QTcpSocket *socket, const QString &state)
{
    const auto context = m_peers.value(socket); // 获取连接标识用于状态消息。
    if (!context.isNull()) {
        emit stateChanged(m_config.channelId, QStringLiteral("%1:%2").arg(state, context->connectionId));
    }
}

} // 命名空间 EdgeAgent
