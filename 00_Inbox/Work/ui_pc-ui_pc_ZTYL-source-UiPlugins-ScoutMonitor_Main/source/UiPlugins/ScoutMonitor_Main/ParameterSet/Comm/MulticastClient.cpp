// 套接字头文件要在 Qt 头之前：winsock2.h 必须先于任何 windows.h。
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  ifdef _MSC_VER
#    pragma comment(lib, "ws2_32.lib")
#  endif
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#endif
#include <cstring>

#include "MulticastClient.h"
#include <QUdpSocket>
#include <QTimer>
#include <QMetaObject>
#include <QAbstractSocket>
#include <QStringList>
// 获取当前毫秒时间戳（用于心跳超时判断）
static qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

// 网卡的第一个 IPv4 地址；没有 IPv4 地址时返回空地址。
static QHostAddress firstIpv4Address(const QNetworkInterface &iface)
{
    const QList<QNetworkAddressEntry> entries = iface.addressEntries();
    for (int i = 0; i < entries.size(); ++i) {
        const QHostAddress ip = entries.at(i).ip();
        if (ip.protocol() == QAbstractSocket::IPv4Protocol)
            return ip;
    }
    return QHostAddress();
}

// 可以入组的网卡：启用、运行中、支持组播、不是回环(Qt 把 Mihomo/Tailscale 这类 TUN 隧道也标成回环)。
static bool canJoinOn(const QNetworkInterface &iface)
{
    return iface.flags().testFlag(QNetworkInterface::IsUp)
            && iface.flags().testFlag(QNetworkInterface::IsRunning)
            && iface.flags().testFlag(QNetworkInterface::CanMulticast)
            && !iface.flags().testFlag(QNetworkInterface::IsLoopBack);
}
MulticastWorker::MulticastWorker(QObject *parent)
    : QObject(parent),
      m_socket(0),
      m_heartbeatTimer(0),
      m_healthCheckTimer(0),
      m_reconnectTimer(0),
      m_groupPort(0),
      m_localPort(0),
      m_heartbeatMs(3000),
      m_heartbeatTimeoutMs(10000),
      m_reconnectBaseMs(1000),
      m_reconnectMaxMs(15000),
      m_currentReconnectMs(1000),
      m_lastRxMs(0),
      m_running(false),
      m_joined(false),
      m_joinedDefault(false)
{
    // 创建定时器（都挂在 worker 对象下，生命周期自动管理）
    m_heartbeatTimer = new QTimer(this);
    m_healthCheckTimer = new QTimer(this);
    m_reconnectTimer = new QTimer(this);
    // 心跳、健康检查是周期定时器；重连定时器是单次触发
    m_heartbeatTimer->setSingleShot(false);
    m_healthCheckTimer->setSingleShot(false);
    m_reconnectTimer->setSingleShot(true);
    // 绑定定时器回调
    connect(m_heartbeatTimer, SIGNAL(timeout()), this, SLOT(onHeartbeatTick()));
    connect(m_healthCheckTimer, SIGNAL(timeout()), this, SLOT(onHealthCheckTick()));
    connect(m_reconnectTimer, SIGNAL(timeout()), this, SLOT(onReconnectTick()));
}
MulticastWorker::~MulticastWorker()
{
    // 析构时兜底释放资源
    closeSocket();
}
void MulticastWorker::configure(QString groupIp, quint16 groupPort, quint16 localPort,
                                QString ifaceName, int heartbeatMs, int heartbeatTimeoutMs,
                                int reconnectBaseMs, int reconnectMaxMs,
                                QString communicationIp)
{
    const QHostAddress newGroupAddr(groupIp);
    const QHostAddress newCommunicationAddr(communicationIp.trimmed());
    const bool membershipChanged = m_groupAddr != newGroupAddr
            || m_groupPort != groupPort
            || m_localPort != localPort
            || m_ifaceName != ifaceName
            || m_communicationAddr != newCommunicationAddr;

    // closeSocket() 必须使用旧的组播地址和接口离组，因此先关闭再覆盖配置。
    if (m_running && membershipChanged) {
        m_heartbeatTimer->stop();
        m_healthCheckTimer->stop();
        m_reconnectTimer->stop();
        closeSocket();
    }

    // 保存基础网络参数
    m_groupAddr = newGroupAddr;
    m_groupPort = groupPort;
    m_localPort = localPort;
    m_ifaceName = ifaceName;
    m_communicationAddr = newCommunicationAddr;
    // 保存心跳与重连策略参数
    m_heartbeatMs = heartbeatMs;
    m_heartbeatTimeoutMs = heartbeatTimeoutMs;
    m_reconnectBaseMs = reconnectBaseMs;
    m_reconnectMaxMs = reconnectMaxMs;
    m_currentReconnectMs = m_reconnectBaseMs;

    if (m_running && membershipChanged) {
        restartMembership("configuration changed");
    }
}
void MulticastWorker::setCommunicationAddress(QString communicationIp)
{
    const QHostAddress newCommunicationAddr(communicationIp.trimmed());
    if (m_communicationAddr == newCommunicationAddr) return;

    // 地址变化可能跨越子网。先用旧接口离组，再保存新地址并重新选卡入组。
    if (m_running) {
        m_heartbeatTimer->stop();
        m_healthCheckTimer->stop();
        m_reconnectTimer->stop();
        closeSocket();
    }
    m_communicationAddr = newCommunicationAddr;
    m_currentReconnectMs = m_reconnectBaseMs;
    if (m_running) {
        restartMembership("communication address changed");
    }
}
void MulticastWorker::startWork()
{
    // 避免重复启动
    if (m_running) return;
    m_running = true;
    emit stateChanged("starting");
    // 首次尝试建链成功：进入 running
    if (openSocketAndJoin()) {
        m_lastRxMs = nowMs();
        m_heartbeatTimer->start(m_heartbeatMs);
        m_healthCheckTimer->start(1000); // 每秒检查一次健康状态
        m_currentReconnectMs = m_reconnectBaseMs;
        emit started();
        emit stateChanged("running");
    } else {
        // 失败则进入重连流程
        scheduleReconnect("initial start failed");
    }
}
void MulticastWorker::stopWork()
{
    // 未运行则无需处理
    if (!m_running) return;
    m_running = false;
    // 停掉所有定时器并关闭 socket
    m_heartbeatTimer->stop();
    m_healthCheckTimer->stop();
    m_reconnectTimer->stop();
    closeSocket();
    emit stopped();
    emit stateChanged("stopped");
}
void MulticastWorker::sendPayload(QByteArray payload)
{
    // 使用局部快照，避免 writeDatagram 失败时同步触发 onSocketError()
    // 导致 m_socket 在本函数中途被 closeSocket() 置空。
    QUdpSocket *socket = m_socket;
    // socket 不存在或未绑定时不发送
    if (!socket) return;
    if (socket->state() != QAbstractSocket::BoundState) return;
    // 发到当前组播地址和端口
    qint64 n = socket->writeDatagram(payload, m_groupAddr, m_groupPort);
    if (n < 0) {
        // 这里必须使用局部 socket，不能再次读取 m_socket（它可能已在错误回调里被清空）
        emit errorOccurred(QString::fromLocal8Bit("writeDatagram failed: %1").arg(socket->errorString()));
    }
}
bool MulticastWorker::openSocketAndJoin()
{
    // 地址合法性检查
    if (m_groupAddr.isNull()) {
        emit errorOccurred("group address is invalid");
        return false;
    }
    if (!isIpv4Multicast(m_groupAddr) && !isIpv6Multicast(m_groupAddr)) {
        emit errorOccurred("group address is not multicast");
        return false;
    }
    // 打开前先清理旧 socket，确保状态干净
    closeSocket();
    // 每次建链（包括重连）都重新枚举网卡，避免沿用已失效的接口。
    m_iface = chooseInterface(m_ifaceName, m_communicationAddr);
    m_socket = new QUdpSocket(this);
    connect(m_socket, SIGNAL(readyRead()), this, SLOT(onReadyRead()));
#if (QT_VERSION >= QT_VERSION_CHECK(5, 0, 0))
    // Qt5 下 error 信号可用于监听底层异常
    connect(m_socket, SIGNAL(error(QAbstractSocket::SocketError)),
            this, SLOT(onSocketError()));
#endif
    // 根据组播类型选择绑定地址族
    QHostAddress bindAddr = isIpv6Multicast(m_groupAddr)
                            ? QHostAddress::AnyIPv6
                            : QHostAddress::AnyIPv4;
    // bind 本地端口，开启地址复用以支持多实例监听同端口
    if (!m_socket->bind(bindAddr, m_localPort,
                        QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        emit errorOccurred(QString::fromLocal8Bit("bind failed: %1").arg(m_socket->errorString()));
        closeSocket();
        return false;
    }
    // 指定组播发送接口（发心跳用；IPv6 尤其建议显式设置）
    if (m_iface.isValid()) {
        m_socket->setMulticastInterface(m_iface);
    }
    // 加入组播组：自动模式下所有可用网卡 + 系统默认网卡，显式指定网卡名时只在那块网卡
    if (!joinGroups()) {
        emit errorOccurred(QString::fromLocal8Bit("joinMulticastGroup failed on all interfaces: %1").arg(m_socket->errorString()));
        closeSocket();
        return false;
    }
    m_joined = true;
    //显式开启组播回环
//    m_socket->setSocketOption(QAbstractSocket::MulticastLoopbackOption, 1);
    return true;
}
bool MulticastWorker::setIpv4Membership(const QHostAddress &localAddress, bool join)
{
    if (!m_socket || localAddress.protocol() != QAbstractSocket::IPv4Protocol)
        return false;
    const qintptr fd = m_socket->socketDescriptor();
    if (fd == -1)
        return false;
    struct ip_mreq mreq;
    std::memset(&mreq, 0, sizeof(mreq));
    mreq.imr_multiaddr.s_addr = htonl(m_groupAddr.toIPv4Address());
    mreq.imr_interface.s_addr = htonl(localAddress.toIPv4Address());
    const int option = join ? IP_ADD_MEMBERSHIP : IP_DROP_MEMBERSHIP;
#ifdef _WIN32
    return ::setsockopt(static_cast<SOCKET>(fd), IPPROTO_IP, option,
                        reinterpret_cast<const char *>(&mreq), int(sizeof(mreq))) == 0;
#else
    return ::setsockopt(int(fd), IPPROTO_IP, option, &mreq, socklen_t(sizeof(mreq))) == 0;
#endif
}
bool MulticastWorker::joinGroups()
{
    m_joinedIfaces.clear();
    m_joinedDefault = false;
    const bool ipv4 = isIpv4Multicast(m_groupAddr);
    QStringList joinedNames;
    QStringList failedNames;

    /*
     * 以前只在 chooseInterface 选出的一块网卡入组：设备通信地址不在本机任何网段时退回枚举到的
     * 第一块网卡(可能是 VMware/Hyper-V 虚拟网卡)，其它网段上的设备也收不到。现在自动模式下
     * 所有可用网卡逐一入组(2.0 同样的修法)；显式指定了网卡名时仍只在那块网卡入组。
     *
     * IPv4 不用 Qt 的 joinMulticastGroup(group, iface)：Qt 5.7 拿网卡的第一个地址当入组接口，
     * Windows 上开着 IPv6 的网卡第一个是 fe80::，转成 IPv4 是 0.0.0.0，于是退化成在系统默认网卡
     * 入组，而且照样返回成功；默认网卡是虚拟网卡时 32 位客户端就收不到设备组播(2026-10-21 实测；
     * Qt 5.10 取的是 IPv4，没这个问题)。所以这里直接用网卡自己的 IPv4 地址入组。IPv6 走 Qt，
     * 它按网卡序号入组，两个版本都对。
     */
    QList<QNetworkInterface> candidates;
    if (!m_ifaceName.isEmpty()) {
        if (m_iface.isValid())
            candidates << m_iface;
    } else {
        const QList<QNetworkInterface> allIfaces = QNetworkInterface::allInterfaces();
        for (int i = 0; i < allIfaces.size(); ++i) {
            if (canJoinOn(allIfaces.at(i)))
                candidates << allIfaces.at(i);
        }
    }
    for (int i = 0; i < candidates.size(); ++i) {
        const QNetworkInterface &iface = candidates.at(i);
        JoinedInterface joined;
        joined.iface = iface;
        bool success = false;
        QString name = iface.humanReadableName();
        if (ipv4) {
            joined.localAddress = firstIpv4Address(iface);
            if (joined.localAddress.isNull())
                continue; // 没有 IPv4 地址的网卡收不到 IPv4 组播
            name += QLatin1Char(' ') + joined.localAddress.toString();
            success = setIpv4Membership(joined.localAddress, true);
        } else {
            success = m_socket->joinMulticastGroup(m_groupAddr, iface);
        }
        if (success) {
            m_joinedIfaces.append(joined);
            joinedNames << name;
        } else {
            failedNames << name;
        }
    }

    // 自动模式(或指定的网卡找不到)时再在系统默认网卡入组兜底；默认网卡上面已入过组时这里会失败，属正常。
    if ((m_ifaceName.isEmpty() || candidates.isEmpty()) && m_socket->joinMulticastGroup(m_groupAddr))
        m_joinedDefault = true;

    // 入组网卡有变化时报一行(首次入组也算)，能看出到底登记在哪几块网卡上。
    QString log = QString::fromUtf8(u8"组播 %1 入组网卡: %2")
            .arg(m_groupAddr.toString())
            .arg(joinedNames.isEmpty() ? QString::fromUtf8(u8"无") : joinedNames.join(QStringLiteral(", ")));
    if (!failedNames.isEmpty())
        log += QString::fromUtf8(u8"；入组失败: ") + failedNames.join(QStringLiteral(", "));
    if (m_joinedDefault)
        log += QString::fromUtf8(u8"；另在系统默认网卡入组");
    if (log != m_lastMembershipLog) {
        m_lastMembershipLog = log;
        emit stateChanged(log);
    }
    return !m_joinedIfaces.isEmpty() || m_joinedDefault;
}
void MulticastWorker::leaveGroups()
{
    if (!m_socket) return;
    const bool ipv4 = isIpv4Multicast(m_groupAddr);
    for (int i = 0; i < m_joinedIfaces.size(); ++i) {
        const JoinedInterface &joined = m_joinedIfaces.at(i);
        // 用入组时的地址离组：期间网卡地址变了也能对上同一条成员关系。
        if (ipv4)
            setIpv4Membership(joined.localAddress, false);
        else
            m_socket->leaveMulticastGroup(m_groupAddr, joined.iface);
    }
    if (m_joinedDefault)
        m_socket->leaveMulticastGroup(m_groupAddr);
    m_joinedIfaces.clear();
    m_joinedDefault = false;
}
void MulticastWorker::closeSocket()
{
    if (!m_socket) return;
    // 如果已经入组，先离组再关闭
    if (m_joined) {
        leaveGroups();
        m_joined = false;
    }
    disconnect(m_socket, 0, this, 0);
    m_socket->close();
    m_socket->deleteLater();
    m_socket = 0;
}
void MulticastWorker::restartMembership(QString reason)
{
    if (!m_running) return;

    emit stateChanged(QString::fromLocal8Bit("reconnecting: %1").arg(reason));
    if (openSocketAndJoin()) {
        m_lastRxMs = nowMs();
        m_heartbeatTimer->start(m_heartbeatMs);
        m_healthCheckTimer->start(1000);
        m_currentReconnectMs = m_reconnectBaseMs;
        emit stateChanged("running");
    } else {
        scheduleReconnect(reason);
    }
}
void MulticastWorker::scheduleReconnect(QString reason)
{
    // 只有在运行态才安排重连
    if (!m_running) return;
    emit stateChanged(QString::fromLocal8Bit("reconnecting: %1 in %2 ms").arg(reason).arg(m_currentReconnectMs));
    // 重连期间停止业务心跳与健康检查，避免重复触发
    m_heartbeatTimer->stop();
    m_healthCheckTimer->stop();
    closeSocket();
    // 启动一次重连定时器，并采用指数退避
    m_reconnectTimer->start(m_currentReconnectMs);
    m_currentReconnectMs = qMin(m_currentReconnectMs * 2, m_reconnectMaxMs);
}
void MulticastWorker::onReconnectTick()
{
    if (!m_running) return;
    // 到点尝试重连；成功恢复 running，失败继续退避重试
    if (openSocketAndJoin()) {
        m_lastRxMs = nowMs();
        m_heartbeatTimer->start(m_heartbeatMs);
        m_healthCheckTimer->start(1000);
        m_currentReconnectMs = m_reconnectBaseMs;
        emit stateChanged("running");
    } else {
        scheduleReconnect("reconnect attempt failed");
    }
}
void MulticastWorker::onReadyRead()
{
    if (!m_socket) return;
    // 循环读取所有待收包（防止只读一个导致积压）
    while (m_socket->hasPendingDatagrams()) {
        QByteArray d;
        d.resize(int(m_socket->pendingDatagramSize()));
        QHostAddress sender;
        quint16 port = 0;
        m_socket->readDatagram(d.data(), d.size(), &sender, &port);
        // 更新最近收包时间，用于健康检查
        m_lastRxMs = nowMs();
        emit datagramReceived(d, sender, port);
    }
}
void MulticastWorker::onSocketError()
{
    if (!m_socket) return;
    emit errorOccurred(QString::fromLocal8Bit("socket error: %1").arg(m_socket->errorString()));
    // 出现 socket 层错误，直接切入重连
    scheduleReconnect("socket error");
}
void MulticastWorker::onHeartbeatTick()
{
    // 周期心跳；可替换成你自己的业务协议包
    sendPayload(QByteArray("HEARTBEAT"));
}
void MulticastWorker::onHealthCheckTick()
{
    if (!m_running) return;
    if (m_heartbeatTimeoutMs <= 0) return; // <=0 表示禁用超时判定
    // 超过阈值未收到任何包，判定链路异常
    const qint64 idle = nowMs() - m_lastRxMs;
    if (idle > m_heartbeatTimeoutMs) {
        emit errorOccurred(QString("heartbeat timeout: idle=%1 ms").arg(idle));
        scheduleReconnect("heartbeat timeout");
    }
}
QNetworkInterface MulticastWorker::chooseInterface(
        const QString &ifaceName, const QHostAddress &communicationAddr) const
{
    // 遍历系统网卡，筛选 up/running/canMulticast。保留第一个可用网卡作为兜底。
    const QList<QNetworkInterface> list = QNetworkInterface::allInterfaces();
    QNetworkInterface fallback;
    QNetworkInterface bestMatch;
    int bestPrefixLength = -1;

    for (int i = 0; i < list.size(); ++i) {
        const QNetworkInterface &iface = list.at(i);
        const bool ok = iface.flags().testFlag(QNetworkInterface::IsUp) &&
                        iface.flags().testFlag(QNetworkInterface::IsRunning) &&
                        iface.flags().testFlag(QNetworkInterface::CanMulticast);
        if (!ok) continue;
        // 指定网卡名：精确匹配
        if (!ifaceName.isEmpty()) {
            if (iface.name() == ifaceName) return iface;
            continue;
        }

        if (iface.flags().testFlag(QNetworkInterface::IsLoopBack)) continue;
        if (!fallback.isValid()) fallback = iface;

        // 自动模式：在该网卡的所有地址中查找与设备通信 IP 同网段的最精确前缀。
        if (communicationAddr.isNull()) continue;
        const QList<QNetworkAddressEntry> entries = iface.addressEntries();
        for (int j = 0; j < entries.size(); ++j) {
            const QNetworkAddressEntry &entry = entries.at(j);
            const QHostAddress localAddr = entry.ip();
            const int prefixLength = entry.prefixLength();
            if (prefixLength < 0 || localAddr.protocol() != communicationAddr.protocol()) continue;
            if (communicationAddr.isInSubnet(localAddr, prefixLength)
                    && prefixLength > bestPrefixLength) {
                bestMatch = iface;
                bestPrefixLength = prefixLength;
            }
        }
    }

    // 只有找不到同网段网卡时，才回退到原来的首个可用非回环网卡。
    return bestMatch.isValid() ? bestMatch : fallback;
}
bool MulticastWorker::isIpv6Multicast(const QHostAddress &addr) const
{
    // IPv6 + multicast 标志
    return addr.protocol() == QAbstractSocket::IPv6Protocol && addr.isMulticast();
}
bool MulticastWorker::isIpv4Multicast(const QHostAddress &addr) const
{
    // IPv4 + multicast 标志
    return addr.protocol() == QAbstractSocket::IPv4Protocol && addr.isMulticast();
}
/* ---------- facade ---------- */
MulticastClient::MulticastClient(QObject *parent)
    : QObject(parent),
      m_worker(new MulticastWorker)
{
    // 将 worker 放到独立线程，确保 socket 全部在同线程内操作
    m_worker->moveToThread(&m_thread);
    // 线程结束时自动删除 worker
    connect(&m_thread, SIGNAL(finished()), m_worker, SLOT(deleteLater()));
    // 将 worker 信号透传给外部使用者
    connect(m_worker, SIGNAL(datagramReceived(QByteArray,QHostAddress,quint16)),
            this, SIGNAL(datagramReceived(QByteArray,QHostAddress,quint16)));
    connect(m_worker, SIGNAL(errorOccurred(QString)),
            this, SIGNAL(errorOccurred(QString)));
    connect(m_worker, SIGNAL(stateChanged(QString)),
            this, SIGNAL(stateChanged(QString)));
    m_thread.start();
}
MulticastClient::~MulticastClient()
{
    // 先请求停止业务，再安全结束线程
    stop();
    m_thread.quit();
    m_thread.wait();
}
void MulticastClient::configure(const QString &groupIp, quint16 groupPort, quint16 localPort,
                                const QString &ifaceName, int heartbeatMs, int heartbeatTimeoutMs,
                                int reconnectBaseMs, int reconnectMaxMs,
                                const QString &communicationIp)
{
    // 通过 QueuedConnection 投递到 worker 线程，避免跨线程直接调用
    QMetaObject::invokeMethod(m_worker, "configure", Qt::QueuedConnection,
                              Q_ARG(QString, groupIp),
                              Q_ARG(quint16, groupPort),
                              Q_ARG(quint16, localPort),
                              Q_ARG(QString, ifaceName),
                              Q_ARG(int, heartbeatMs),
                              Q_ARG(int, heartbeatTimeoutMs),
                              Q_ARG(int, reconnectBaseMs),
                              Q_ARG(int, reconnectMaxMs),
                              Q_ARG(QString, communicationIp));
}
void MulticastClient::setCommunicationAddress(const QString &communicationIp)
{
    QMetaObject::invokeMethod(m_worker, "setCommunicationAddress", Qt::QueuedConnection,
                              Q_ARG(QString, communicationIp));
}
void MulticastClient::start()
{
    // 异步启动 worker
    QMetaObject::invokeMethod(m_worker, "startWork", Qt::QueuedConnection);
}
void MulticastClient::stop()
{
    // 异步停止 worker
    QMetaObject::invokeMethod(m_worker, "stopWork", Qt::QueuedConnection);
}
void MulticastClient::send(const QByteArray &payload)
{
    // 异步发送，线程安全
    QMetaObject::invokeMethod(m_worker, "sendPayload", Qt::QueuedConnection,
                              Q_ARG(QByteArray, payload));
}
