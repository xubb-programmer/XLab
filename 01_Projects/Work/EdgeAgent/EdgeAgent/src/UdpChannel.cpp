/**
 * @file UdpChannel.cpp
 * @brief UDP 通道实现。
 * @author xubb
 */

#include "UdpChannel.h"

#include <QDateTime>
#include <QNetworkInterface>

namespace EdgeAgent {

UdpChannel::UdpChannel(const ChannelConfig &config, QObject *parent)
    : Channel(config, parent)
{
    connect(&m_socket, &QUdpSocket::readyRead, this, &UdpChannel::readPendingDatagrams);
}

void UdpChannel::start()
{
    if (m_running) {
        return;
    }

    const auto bindMode = QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint; // 允许共享地址并复用端口。
    if (!m_socket.bind(m_config.udp.bindAddress, m_config.udp.bindPort, bindMode)) {
        reportFault(QStringLiteral("udp_bind_failed"), QStringLiteral("UDP 绑定失败"));
        setState(QStringLiteral("error"));
        return;
    }

    if (m_config.udp.broadcast) {
        m_socket.setSocketOption(QAbstractSocket::MulticastTtlOption, 1);
    }

    if (m_config.udp.multicastEnabled) {
        bool joined = false; // 记录是否成功加入组播组。
        if (m_config.udp.multicastInterfaceName.isEmpty()) {
            joined = m_socket.joinMulticastGroup(m_config.udp.multicastGroup);
        } else {
            const QNetworkInterface interface = QNetworkInterface::interfaceFromName(m_config.udp.multicastInterfaceName); // 查找指定网卡。
            if (!interface.isValid()) {
                reportFault(QStringLiteral("udp_multicast_interface_invalid"), QStringLiteral("组播网卡无效：%1").arg(m_config.udp.multicastInterfaceName));
                m_socket.close();
                setState(QStringLiteral("error"));
                return;
            }
            joined = m_socket.joinMulticastGroup(m_config.udp.multicastGroup, interface);
        }
        if (!joined) {
            reportFault(QStringLiteral("udp_multicast_join_failed"), QStringLiteral("加入 UDP 组播失败"));
            m_socket.close();
            setState(QStringLiteral("error"));
            return;
        }
    }

    m_running = true;
    setState(QStringLiteral("ready"));
}

void UdpChannel::stop()
{
    if (!m_running && !m_socket.isOpen()) {
        return;
    }
    if (m_config.udp.multicastEnabled) {
        m_socket.leaveMulticastGroup(m_config.udp.multicastGroup);
    }
    m_socket.close();
    m_running = false;
    setState(QStringLiteral("stopped"));
}

bool UdpChannel::sendDatagram(const QByteArray &payload, const QHostAddress &address, quint16 port)
{
    if (!m_running) {
        reportFault(QStringLiteral("udp_send_not_ready"), QStringLiteral("UDP 通道未就绪"));
        return false;
    }
    if (payload.size() > m_config.udp.maxDatagramBytes) {
        reportFault(QStringLiteral("udp_payload_too_large"), QStringLiteral("数据长度超过 max_datagram_bytes"));
        return false;
    }
    if (!isAllowedPeer(address, port)) {
        reportFault(QStringLiteral("udp_target_rejected"), QStringLiteral("目标地址不在 allowed_peers 中"));
        return false;
    }
    const qint64 written = m_socket.writeDatagram(payload, address, port); // 返回实际写入的字节数。
    if (written != payload.size()) {
        reportFault(QStringLiteral("udp_send_failed"), QStringLiteral("UDP 发送失败"));
        return false;
    }
    return true;
}

bool UdpChannel::isAllowedPeer(const QHostAddress &address, quint16 port) const
{
    for (const PeerConfig &peer : m_config.udp.allowedPeers) { // 逐项匹配地址和端口白名单。
        if (peer.address == address && (peer.port == 0 || peer.port == port)) {
            return true;
        }
    }
    return false;
}

void UdpChannel::readPendingDatagrams()
{
    while (m_socket.hasPendingDatagrams()) {
        const qint64 pendingSize = m_socket.pendingDatagramSize(); // 当前待接收数据报长度。
        const qint64 readSize = qMin<qint64>(pendingSize, static_cast<qint64>(m_config.udp.maxDatagramBytes) + 1); // 多读一个字节以检测超长报文。
        QByteArray payload(static_cast<int>(qMax<qint64>(readSize, 0)), '\0'); // UDP 数据缓冲区。
        QHostAddress peerAddress; // 数据来源地址。
        quint16 peerPort = 0; // 数据来源端口。
        const qint64 received = m_socket.readDatagram(payload.data(), payload.size(), &peerAddress, &peerPort); // 读取数据报。
        if (received < 0) {
            reportFault(QStringLiteral("udp_read_failed"), QStringLiteral("UDP 接收失败"));
            continue;
        }
        payload.resize(static_cast<int>(received));

        if (!isAllowedPeer(peerAddress, peerPort)) {
            reportFault(QStringLiteral("udp_peer_rejected"), QStringLiteral("拒绝未授权 UDP 对端：%1:%2").arg(peerAddress.toString()).arg(peerPort));
            continue;
        }
        if (pendingSize > m_config.udp.maxDatagramBytes) {
            reportFault(QStringLiteral("udp_payload_too_large"), QStringLiteral("UDP 数据报超过 max_datagram_bytes"));
            continue;
        }
        emitDatagram(payload, peerAddress, peerPort);
    }
}

void UdpChannel::emitDatagram(const QByteArray &payload, const QHostAddress &address, quint16 port)
{
    FrameEvent event; // 组装统一传输事件。
    event.channelId = m_config.channelId;
    event.transport = QStringLiteral("udp");
    event.payload = payload;
    event.peerAddress = address.toString();
    event.peerPort = port;
    event.timestampMs = QDateTime::currentMSecsSinceEpoch();
    emit frameReceived(event);
}

} // 命名空间 EdgeAgent
