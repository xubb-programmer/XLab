/**
 * @file SerialChannel.cpp
 * @brief Qt5 串口通道实现。
 * @author xubb
 */

#include "SerialChannel.h"

#include <QDateTime>

namespace EdgeAgent {

SerialChannel::SerialChannel(const ChannelConfig &config, QObject *parent)
    : Channel(config, parent)
    , m_decoder(config.frame)
{
    m_idleTimer.setSingleShot(true);
    m_reconnectTimer.setSingleShot(true);
    connect(&m_port, &QSerialPort::readyRead, this, &SerialChannel::readAvailable);
    connect(&m_port, &QSerialPort::errorOccurred, this, &SerialChannel::handleSerialError);
    connect(&m_idleTimer, &QTimer::timeout, this, &SerialChannel::handleIdleTimeout);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &SerialChannel::retryOpen);
}

void SerialChannel::start()
{
    if (m_running) {
        return;
    }

    m_running = true;
    m_nextReconnectMs = m_config.serial.reconnectInitialMs;
    if (!openPort()) {
        scheduleReconnect();
    }
}

void SerialChannel::stop()
{
    if (!m_running && !m_port.isOpen()) {
        return;
    }

    m_running = false; // 先标记停止，避免关闭串口触发的错误被当作断线处理。
    m_idleTimer.stop();
    m_reconnectTimer.stop();
    m_port.close();
    m_decoder.reset();
    setState(QStringLiteral("stopped"));
}

bool SerialChannel::sendBytes(const QByteArray &payload)
{
    if (!m_running || !m_port.isOpen()) {
        reportFault(QStringLiteral("serial_send_not_ready"), QStringLiteral("串口未连接，无法发送数据"));
        return false;
    }

    const qint64 written = m_port.write(payload); // 将字节提交到 Qt 串口发送队列。
    if (written != payload.size()) {
        reportFault(QStringLiteral("serial_send_failed"), QStringLiteral("串口发送数据失败：%1").arg(m_port.errorString()));
        return false;
    }
    return true;
}

void SerialChannel::readAvailable()
{
    const QByteArray incoming = m_port.readAll(); // 一次读取当前串口缓冲区内的全部新字节。
    if (incoming.isEmpty()) {
        return;
    }

    QVector<QByteArray> frames; // 当前读取产生的完整帧。
    QString error; // 分帧失败时的中文错误说明。
    if (!m_decoder.feed(incoming, &frames, &error)) {
        reportFault(QStringLiteral("serial_frame_invalid"), error);
        m_idleTimer.stop();
        m_port.close();
        m_decoder.reset();
        scheduleReconnect();
        return;
    }

    if (m_config.frame.kind == FrameKind::IdleTimeout && m_decoder.hasBuffered()) {
        m_idleTimer.start(m_config.frame.idleMs); // 新字节到达后重新计算空闲间隔。
    }
    for (const QByteArray &frame : frames) {
        emitFrame(frame);
    }
}

void SerialChannel::handleIdleTimeout()
{
    if (!m_decoder.hasBuffered()) {
        return;
    }
    emitFrame(m_decoder.takeBuffered());
}

void SerialChannel::handleSerialError(QSerialPort::SerialPortError error)
{
    if (!m_running || !m_port.isOpen() || error == QSerialPort::NoError || error == QSerialPort::NotOpenError) {
        return;
    }

    const QString detail = m_port.errorString(); // 记录 Qt 提供的串口错误详情。
    reportFault(QStringLiteral("serial_io_error"), QStringLiteral("串口通信错误：%1").arg(detail));
    if (error == QSerialPort::TimeoutError || error == QSerialPort::ParityError
        || error == QSerialPort::FramingError || error == QSerialPort::BreakConditionError
        || error == QSerialPort::UnsupportedOperationError) {
        return;
    }

    m_idleTimer.stop();
    m_port.close();
    m_decoder.reset();
    scheduleReconnect();
}

void SerialChannel::retryOpen()
{
    if (m_running && !m_port.isOpen() && !openPort()) {
        scheduleReconnect();
    }
}

bool SerialChannel::openPort()
{
    m_port.setPortName(m_config.serial.portName);

    const auto dataBits = static_cast<QSerialPort::DataBits>(m_config.serial.dataBits); // 配置中的 5 到 8 位可直接映射到 Qt 枚举。
    QSerialPort::Parity parity = QSerialPort::NoParity; // 默认不使用奇偶校验。
    if (m_config.serial.parity == QStringLiteral("even")) {
        parity = QSerialPort::EvenParity;
    } else if (m_config.serial.parity == QStringLiteral("odd")) {
        parity = QSerialPort::OddParity;
    } else if (m_config.serial.parity == QStringLiteral("space")) {
        parity = QSerialPort::SpaceParity;
    } else if (m_config.serial.parity == QStringLiteral("mark")) {
        parity = QSerialPort::MarkParity;
    }

    const QSerialPort::StopBits stopBits = m_config.serial.stopBits == 1.5
        ? QSerialPort::OneAndHalfStop
        : (m_config.serial.stopBits == 2.0 ? QSerialPort::TwoStop : QSerialPort::OneStop); // 转换配置中的停止位数。
    QSerialPort::FlowControl flowControl = QSerialPort::NoFlowControl; // 默认关闭硬件和软件流控。
    if (m_config.serial.flowControl == QStringLiteral("hardware")) {
        flowControl = QSerialPort::HardwareControl;
    } else if (m_config.serial.flowControl == QStringLiteral("software")) {
        flowControl = QSerialPort::SoftwareControl;
    }

    const bool settingsApplied = m_port.setBaudRate(m_config.serial.baudRate)
        && m_port.setDataBits(dataBits)
        && m_port.setParity(parity)
        && m_port.setStopBits(stopBits)
        && m_port.setFlowControl(flowControl); // 任一参数被当前平台或串口设备拒绝，就不继续打开。
    if (!settingsApplied) {
        reportFault(QStringLiteral("serial_config_failed"), QStringLiteral("串口参数设置失败：%1").arg(m_port.errorString()));
        setState(QStringLiteral("disconnected"));
        return false;
    }

    if (!m_port.open(QIODevice::ReadWrite)) {
        reportFault(QStringLiteral("serial_open_failed"), QStringLiteral("无法打开串口 %1：%2")
                                                        .arg(m_config.serial.portName, m_port.errorString()));
        setState(QStringLiteral("disconnected"));
        return false;
    }

    m_decoder.reset();
    m_idleTimer.stop();
    m_nextReconnectMs = m_config.serial.reconnectInitialMs;
    setState(QStringLiteral("ready"));
    return true;
}

void SerialChannel::scheduleReconnect()
{
    if (!m_running || m_reconnectTimer.isActive()) {
        return;
    }

    setState(QStringLiteral("disconnected"));
    m_reconnectTimer.start(m_nextReconnectMs);
    m_nextReconnectMs = qMin(m_nextReconnectMs * 2, m_config.serial.reconnectMaxMs);
}

void SerialChannel::emitFrame(const QByteArray &payload)
{
    FrameEvent event; // 将完整串口帧转换为各通道共用的数据事件。
    event.channelId = m_config.channelId;
    event.deviceId = m_config.deviceId;
    event.transport = QStringLiteral("serial");
    event.payload = payload;
    event.endpoint = m_config.serial.portName;
    event.timestampMs = QDateTime::currentMSecsSinceEpoch();
    emit frameReceived(event);
}

} // 命名空间 EdgeAgent
