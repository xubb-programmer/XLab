/**
 * @file CanChannel.cpp
 * @brief Qt Serial Bus CAN 通道实现。
 * @author xubb
 */

#include "CanChannel.h"

#include <QCanBus>
#include <QDateTime>
#include <QVariant>

namespace EdgeAgent {

CanChannel::CanChannel(const ChannelConfig &config, QObject *parent)
    : Channel(config, parent)
{
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &CanChannel::retryConnect);
}

void CanChannel::start()
{
    if (m_running) {
        return;
    }
    m_stopping = false;
    m_running = true;
    m_nextReconnectMs = m_config.can.reconnectInitialMs;
    if (!openDevice()) {
        scheduleReconnect();
    }
}

void CanChannel::stop()
{
    if (!m_running && m_device == nullptr) {
        return;
    }
    m_stopping = true;
    m_running = false;
    m_reconnectTimer.stop();
    closeDevice();
    setState(QStringLiteral("stopped"));
}

bool CanChannel::openDevice()
{
    if (!m_running || m_stopping || m_device != nullptr) {
        return m_device != nullptr;
    }

    QCanBus *bus = QCanBus::instance(); // 获取 Qt Serial Bus 工厂。
    if (!bus->plugins().contains(m_config.can.backend)) {
        reportFault(QStringLiteral("can_backend_unavailable"),
                    QStringLiteral("CAN 后端不可用：%1").arg(m_config.can.backend));
        setState(QStringLiteral("error"));
        return false;
    }

    QString error; // 创建 Qt CAN 设备时的错误信息。
    m_device = bus->createDevice(m_config.can.backend, m_config.can.interfaceName, &error);
    if (m_device == nullptr) {
        reportFault(QStringLiteral("can_device_create_failed"),
                    QStringLiteral("无法创建 CAN 设备 %1：%2").arg(m_config.can.interfaceName, error));
        setState(QStringLiteral("disconnected"));
        return false;
    }
    connect(m_device, &QCanBusDevice::framesReceived, this, &CanChannel::readFrames);
    connect(m_device, &QCanBusDevice::errorOccurred, this, &CanChannel::handleDeviceError);
    connect(m_device, &QCanBusDevice::stateChanged, this, &CanChannel::handleDeviceState);

    // Qt 5.12 的 SocketCAN 插件不支持通过 BitRateKey 设置接口波特率；
    // 真实 can0 的 bitrate 和 restart-ms 由 Linux ip link 在接口启动前配置。
    m_device->setConfigurationParameter(QCanBusDevice::ReceiveOwnKey, m_config.can.receiveOwnFrames);
    m_device->setConfigurationParameter(QCanBusDevice::LoopbackKey, true);
    if (!m_config.can.filters.isEmpty()) {
        QList<QCanBusDevice::Filter> filters; // Qt 过滤器列表。
        for (const CanFilterConfig &config : m_config.can.filters) {
            QCanBusDevice::Filter filter; // 当前 CAN ID 过滤器。
            filter.frameId = config.canId;
            filter.frameIdMask = config.canIdMask;
            filter.format = config.extended ? QCanBusDevice::Filter::MatchExtendedFormat
                                            : QCanBusDevice::Filter::MatchBaseFormat;
            filters.append(filter);
        }
        m_device->setConfigurationParameter(QCanBusDevice::RawFilterKey, QVariant::fromValue(filters));
    }

    QCanBusDevice *device = m_device; // 保存局部指针，连接失败时错误信号可能已经清空成员。
    if (!device->connectDevice()) {
        if (m_device != nullptr) {
            const QString detail = m_device->errorString();
            reportFault(QStringLiteral("can_connect_failed"),
                        QStringLiteral("CAN 接口 %1 连接失败：%2").arg(m_config.can.interfaceName, detail));
            closeDevice();
        }
        setState(QStringLiteral("disconnected"));
        return false;
    }

    m_nextReconnectMs = m_config.can.reconnectInitialMs;
    return true;
}

void CanChannel::closeDevice()
{
    if (m_device == nullptr) {
        return;
    }
    QCanBusDevice *device = m_device; // 先从成员中摘除，避免断开信号触发重复重连。
    m_device = nullptr;
    device->disconnectDevice();
    device->deleteLater();
}

void CanChannel::readFrames()
{
    if (m_device == nullptr) {
        return;
    }
    while (m_device->framesAvailable() > 0) {
        const QCanBusFrame frame = m_device->readFrame(); // 取出一条总线帧。
        if (frame.frameType() == QCanBusFrame::ErrorFrame) {
            reportFault(QStringLiteral("can_error_frame"),
                        QStringLiteral("收到 CAN 错误帧：%1").arg(m_device->interpretErrorFrame(frame)));
            continue;
        }
        if (!frame.isValid() || frame.hasFlexibleDataRateFormat() || frame.payload().size() > 8) {
            reportFault(QStringLiteral("can_frame_invalid"), QStringLiteral("收到不支持的 CAN FD 或非法帧"));
            continue;
        }
        emitFrame(frame);
    }
}

void CanChannel::handleDeviceError(QCanBusDevice::CanBusError error)
{
    if (m_stopping || m_device == nullptr || error == QCanBusDevice::NoError) {
        return;
    }
    QString code = QStringLiteral("can_device_error"); // 默认 CAN 错误码。
    if (error == QCanBusDevice::ConnectionError) {
        code = QStringLiteral("can_connection_error");
    } else if (error == QCanBusDevice::ReadError) {
        code = QStringLiteral("can_read_error");
    } else if (error == QCanBusDevice::WriteError) {
        code = QStringLiteral("can_write_error");
    }
    reportFault(code, QStringLiteral("CAN 设备错误：%1").arg(m_device->errorString()));
    if (error == QCanBusDevice::ConnectionError || error == QCanBusDevice::ReadError) {
        closeDevice();
        scheduleReconnect();
    }
}

void CanChannel::handleDeviceState(QCanBusDevice::CanBusDeviceState state)
{
    if (m_stopping) {
        return;
    }
    if (state == QCanBusDevice::UnconnectedState) {
        setState(QStringLiteral("disconnected"));
        if (m_running && m_device != nullptr) {
            closeDevice();
            scheduleReconnect();
        }
    } else if (state == QCanBusDevice::ConnectedState) {
        setState(QStringLiteral("ready"));
    }
}

void CanChannel::retryConnect()
{
    if (m_running && !m_stopping && !openDevice()) {
        scheduleReconnect();
    }
}

void CanChannel::scheduleReconnect()
{
    if (!m_running || m_stopping || m_reconnectTimer.isActive()) {
        return;
    }
    setState(QStringLiteral("reconnecting"));
    m_reconnectTimer.start(m_nextReconnectMs);
    m_nextReconnectMs = qMin(m_nextReconnectMs * 2, m_config.can.reconnectMaxMs);
}

bool CanChannel::sendFrame(quint32 canId, bool extended, bool remote, int dlc, const QByteArray &payload)
{
    const quint32 maxId = extended ? 0x1FFFFFFFU : 0x7FFU;
    if (canId > maxId || dlc < 0 || dlc > 8 || (remote ? !payload.isEmpty() : payload.size() != dlc)) {
        reportFault(QStringLiteral("can_send_invalid"), QStringLiteral("CAN 发送帧的 ID、DLC 或数据长度无效"));
        return false;
    }
    if (m_device == nullptr || m_device->state() != QCanBusDevice::ConnectedState) {
        reportFault(QStringLiteral("can_send_not_ready"), QStringLiteral("CAN 接口未连接，无法发送"));
        return false;
    }

    QCanBusFrame frame; // 构造待发送的经典 CAN 帧。
    frame.setFrameId(canId);
    frame.setExtendedFrameFormat(extended);
    frame.setFrameType(remote ? QCanBusFrame::RemoteRequestFrame : QCanBusFrame::DataFrame);
    frame.setPayload(remote ? QByteArray(dlc, '\0') : payload); // Qt 用 payload 长度表示 RTR 的 DLC。
    if (!frame.isValid() || frame.hasFlexibleDataRateFormat()) {
        reportFault(QStringLiteral("can_send_invalid"), QStringLiteral("CAN 发送帧不是合法的经典 CAN 帧"));
        return false;
    }
    if (!m_device->writeFrame(frame)) {
        reportFault(QStringLiteral("can_write_failed"), QStringLiteral("CAN 帧发送失败：%1").arg(m_device->errorString()));
        return false;
    }
    return true;
}

void CanChannel::emitFrame(const QCanBusFrame &frame)
{
    FrameEvent event; // 将 Qt CAN 帧转换为统一传输事件。
    event.channelId = m_config.channelId;
    event.deviceId = m_config.deviceId;
    event.transport = QStringLiteral("can");
    event.endpoint = m_config.can.interfaceName;
    event.canId = frame.frameId();
    event.canExtended = frame.hasExtendedFrameFormat();
    event.canRemote = frame.frameType() == QCanBusFrame::RemoteRequestFrame;
    event.canDlc = static_cast<quint8>(qMin(frame.payload().size(), 8));
    event.payload = event.canRemote ? QByteArray() : frame.payload();
    event.timestampMs = QDateTime::currentMSecsSinceEpoch();
    emit frameReceived(event);
}

} // 命名空间 EdgeAgent
