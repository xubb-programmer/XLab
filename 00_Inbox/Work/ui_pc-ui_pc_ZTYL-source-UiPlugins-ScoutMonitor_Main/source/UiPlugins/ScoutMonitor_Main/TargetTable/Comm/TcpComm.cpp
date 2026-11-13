/**
 * @file TcpComm.cpp
 * @brief TcpComm 实现：TCP 服务端/客户端收发、客户端断线自动重连
 */

#include "TcpComm.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QMessageBox>
#include <QTextCodec>
#include <QSettings>
#include <QApplication>

/**
 * @brief 构造，初始化成员并配置单次重连定时器
 */
TcpComm::TcpComm(QObject *parent)
    : QObject(parent)
    , m_role(RoleNone)
    , m_server(nullptr)
    , m_clientSocket(nullptr)
    , m_clientPort(0)
    , m_autoReconnect(true)
    , m_reconnectIntervalMs(3000)
    , m_reconnectTimer(new QTimer(this))
{
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &TcpComm::onReconnectTimeout);
}

TcpComm::~TcpComm()
{
    stop();
}

/**
 * @brief 启动服务端：先 stop 清场，再 listen
 */
bool TcpComm::startServer(quint16 port, const QHostAddress &address)
{
    stop();

    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &TcpComm::onNewConnection);

    if (!m_server->listen(address, port)) {
        const QString err = QStringLiteral("TCP listen failed [%1:%2]: %3")
                                .arg(address.toString())
                                .arg(port)
                                .arg(m_server->errorString());
        QMessageBox::critical(nullptr, "TcpComm error!", err);
        emit errorOccurred(err);
        m_server->deleteLater();
        m_server = nullptr;
        return false;
    }

    m_role = RoleServer;
    return true;
}

/**
 * @brief 启动客户端：记录 host/port，新建套接字并 connectToHost
 */
bool TcpComm::startClient(const QString &host, quint16 port)
{
    stop();

    m_clientHost = host;
    m_clientPort = port;
//    getConfig();
    m_role = RoleClient;
    return connectClient();
}

/**
 * @brief 立即用上次地址重连（不经过定时器）
 */
bool TcpComm::reconnectClient()
{
    if (m_role != RoleClient || m_clientHost.isEmpty()) {
        return false;
    }
    m_reconnectTimer->stop();
    return connectClient();
}

void TcpComm::setAutoReconnect(bool enable)
{
    m_autoReconnect = enable;
    if (!enable) {
        m_reconnectTimer->stop();
    }
}

void TcpComm::setReconnectInterval(int msec)
{
    m_reconnectIntervalMs = msec > 0 ? msec : 3000;
}

/**
 * @brief 停止所有 TCP 活动：关监听、断客户端、清地址、角色置 None
 */
void TcpComm::stop()
{
    m_reconnectTimer->stop();

    if (m_server) {
        m_server->close();
        clearServerClients();
        m_server->deleteLater();
        m_server = nullptr;
    }

    destroyClientSocket();
    m_clientHost.clear();
    m_clientPort = 0;
    m_role = RoleNone;
}

bool TcpComm::isClientConnected() const
{
    return m_clientSocket
           && m_clientSocket->state() == QAbstractSocket::ConnectedState;
}

int TcpComm::clientCount() const
{
    return m_serverClients.size();
}

/**
 * @brief 发送数据
 * 客户端：写入当前连接；服务端：向所有在线客户端广播
 */
qint64 TcpComm::sendData(const QByteArray &data)
{
    if (data.isEmpty()) {
        return 0;
    }

    if (m_role == RoleClient) {

        if (!isClientConnected()) {
            return -1;
        }
        const qint64 n = m_clientSocket->write(data);
        m_clientSocket->flush();
        return n;
    }

    if (m_role == RoleServer) {
        if (m_serverClients.isEmpty()) {
            return -1;
        }
        qint64 total = 0;
        for (QTcpSocket *socket : m_serverClients) {
            if (socket->state() == QAbstractSocket::ConnectedState) {
                total += socket->write(data);
                socket->flush();
            }
        }
        return total;
    }

    return -1;
}

/**
 * @brief 服务端单播：仅向指定 accept 连接发送
 */
qint64 TcpComm::sendDataTo(QTcpSocket *socket, const QByteArray &data)
{
    if (m_role != RoleServer || !socket || data.isEmpty()) {
        return data.isEmpty() ? 0 : -1;
    }
    if (!m_serverClients.contains(socket)
        || socket->state() != QAbstractSocket::ConnectedState) {
        return -1;
    }
    const qint64 n = socket->write(data);
    socket->flush();
    return n;
}

/**
 * @brief 销毁客户端套接字
 * 先将 m_clientSocket 置空再 deleteLater，避免 disconnected/error 槽里访问已删对象
 */
void TcpComm::destroyClientSocket()
{
    if (!m_clientSocket) {
        return;
    }

    QTcpSocket *socket = m_clientSocket;
    m_clientSocket = nullptr;
    socket->blockSignals(true);
    socket->disconnectFromHost();
    if (socket->state() != QAbstractSocket::UnconnectedState) {
        socket->abort();
    }
    socket->deleteLater();
}

/**
 * @brief 创建新的 QTcpSocket 并连接
 * 每次连接都新建套接字，解决断线后复用旧 socket 无法 connectToHost 的问题
 */
bool TcpComm::connectClient()
{
    if (m_clientHost.isEmpty()) {
        return false;
    }

    destroyClientSocket();

    m_clientSocket = new QTcpSocket(this);
    setupSocket(m_clientSocket);

    connect(m_clientSocket, &QTcpSocket::connected, this, [this]() {
        m_reconnectTimer->stop();
        emit connected();
    });

    m_clientSocket->connectToHost(m_clientHost, m_clientPort);
    return true;
}

/**
 * @brief 调度一次延迟自动重连（已激活则不再重复启动定时器）
 */
void TcpComm::scheduleClientReconnect()
{
    if (!m_autoReconnect || m_clientHost.isEmpty() || m_role != RoleClient) {
        return;
    }
    if (m_reconnectTimer->isActive()) {
        return;
    }
    m_reconnectTimer->start(m_reconnectIntervalMs);
}

/**
 * @brief 定时器到时：若仍未连接则再次 connectClient
 */
void TcpComm::onReconnectTimeout()
{
    if (m_role != RoleClient || isClientConnected()) {
        return;
    }
    connectClient();
}

/**
 * @brief 服务端新连接：循环 accept 并入队、绑定信号
 */
void TcpComm::onNewConnection()
{
    if (!m_server) {
        return;
    }

    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        setupSocket(socket);
        m_serverClients.append(socket);
        emit clientConnected(socket);
    }
}

/**
 * @brief 可读数据：readAll 后按角色分发信号（TCP 字节流，未拆包）
 */
void TcpComm::onSocketReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket) {
        return;
    }

    const QByteArray data = socket->readAll();
    if (data.isEmpty()) {
        return;
    }

    if (m_role == RoleClient) {
        emit dataArrive(data);
        return;
    }

    emit dataArriveFrom(socket, data);
    if (m_serverClients.size() == 1) {
        emit dataArrive(data);
    }
}

/**
 * @brief 连接断开
 * 客户端：通知 disconnected、销毁旧 socket、启动自动重连
 * 服务端：从列表移除并 deleteLater
 */
void TcpComm::onSocketDisconnected()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket) {
        return;
    }

    if (m_role == RoleClient && socket == m_clientSocket) {
        emit disconnected();
        destroyClientSocket();
        scheduleClientReconnect();
        return;
    }

    if (m_serverClients.removeOne(socket)) {
        emit clientDisconnected(socket);
        socket->deleteLater();
    }
}

/**
 * @brief 套接字错误
 * 客户端常见断线/拒连错误走重连逻辑；其它错误上报 errorOccurred
 */
void TcpComm::onSocketError(QAbstractSocket::SocketError socketError)
{
    Q_UNUSED(socketError)

    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket) {
        return;
    }

    if (m_role == RoleClient && socket == m_clientSocket) {
        const QAbstractSocket::SocketError err = socket->error();
        if (err == QAbstractSocket::ConnectionRefusedError
            || err == QAbstractSocket::RemoteHostClosedError
            || err == QAbstractSocket::NetworkError) {
            if (socket->state() != QAbstractSocket::ConnectedState) {
                scheduleClientReconnect();
            }
            return;
        }
    }

    const QString err = socket->errorString();
    emit errorOccurred(err);
}

/**
 * @brief 关闭服务端所有已 accept 的客户端连接
 */
void TcpComm::clearServerClients()
{
    for (QTcpSocket *socket : m_serverClients) {
        socket->disconnectFromHost();
        socket->abort();
        socket->deleteLater();
    }
    m_serverClients.clear();
}

/**
 * @brief 统一绑定 readyRead / disconnected / error（兼容 Qt5.15 前后 API）
 */
void TcpComm::setupSocket(QTcpSocket *socket)
{
    if (!socket) {
        return;
    }

    connect(socket, &QTcpSocket::readyRead, this, &TcpComm::onSocketReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, &TcpComm::onSocketDisconnected);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(socket, &QTcpSocket::errorOccurred, this, &TcpComm::onSocketError);
#else
    connect(socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
            this, &TcpComm::onSocketError);
#endif
}


/**
 * @brief initConfig
 * 插件初始化配置
 */
void TcpComm::getConfig()
{
    QSettings settings(QApplication::applicationDirPath() + "/config/communication.ini" , QSettings::IniFormat);
    settings.setIniCodec(QTextCodec::codecForName("GBK"));
    settings.beginGroup("Address");
    m_clientPort    =  settings.value("CzPort").toInt();
    m_clientHost =  settings.value("CzIp").toString();
//    QString host;
//    quint16 Port;
    settings.endGroup();
}
