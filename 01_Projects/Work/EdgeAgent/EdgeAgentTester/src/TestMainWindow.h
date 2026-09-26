/**
 * @file TestMainWindow.h
 * @brief EdgeAgent 可视化测试客户端主窗口。
 * @author xubb
 */

#pragma once

#include "TestFrameDecoder.h"

#include <QCanBusDevice>
#include <QCanBusFrame>
#include <QHostAddress>
#include <QMainWindow>
#include <QPointer>

#include <QSerialPort>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QTimer;

namespace edgeagent {

/** @brief 测试客户端使用的传输类型。 */
enum class TesterTransport {
    Udp, ///< UDP 数据报。
    Tcp, ///< TCP 字节流。
    Serial, ///< 串口字节流。
    Can ///< 经典 CAN 总线帧。
};

/** @brief TCP 和串口的分帧运行参数。 */
struct TesterFrameSettings {
    TestFrameKind kind = TestFrameKind::Delimiter; ///< 分帧类型。
    QByteArray delimiter = QByteArrayLiteral("\n"); ///< 分隔符字节。
    int fixedLength = 8; ///< 固定帧长度。
    int lengthBytes = 2; ///< 长度前缀字节数。
    bool littleEndian = false; ///< 长度前缀是否使用小端。
    int idleMs = 20; ///< 空闲分帧等待时间。
    int maxBytes = 65536; ///< 最大帧长度。
};

/**
 * @brief UDP、TCP、串口一体化测试窗口。
 *
 * 窗口负责建立测试连接、发送原始字节、接收并按分帧配置解析报文，
 * 同时以表格和详细文本两种方式展示 HEX、ASCII、JSON 结果。
 */
class TestMainWindow final : public QMainWindow {
    Q_OBJECT

public:
    /** @brief 创建测试客户端主窗口。 */
    explicit TestMainWindow(QWidget *parent = nullptr);

    /** @brief 关闭所有通信资源并销毁窗口。 */
    ~TestMainWindow() override;

private slots:
    /** @brief 切换 UDP、TCP、串口配置页面。 */
    void updateTransportPage(int index);

    /** @brief 打开当前协议的接收通道。 */
    void openTransport();

    /** @brief 关闭当前协议的接收通道。 */
    void closeTransport();

    /** @brief 发送输入框中的报文。 */
    void sendPayload();

    /** @brief 清除接收表格和详细日志。 */
    void clearView();

    /** @brief 刷新系统可用串口列表。 */
    void refreshSerialPorts();

    /** @brief 读取 UDP 数据报。 */
    void readUdpDatagrams();

    /** @brief 读取 TCP 字节流。 */
    void readTcpBytes();

    /** @brief 接受 TCP 服务端的新连接。 */
    void acceptTcpConnection();

    /** @brief 处理 TCP 客户端连接成功。 */
    void onTcpConnected();

    /** @brief 处理 TCP 连接断开。 */
    void onTcpDisconnected();

    /** @brief 处理 TCP 套接字错误。 */
    void onTcpError(QAbstractSocket::SocketError error);

    /** @brief 读取串口字节流。 */
    void readSerialBytes();

    /** @brief 处理串口错误。 */
    void onSerialError(QSerialPort::SerialPortError error);

    /** @brief 读取 CAN 总线帧。 */
    void readCanFrames();

    /** @brief 处理 CAN 总线错误。 */
    void onCanError(QCanBusDevice::CanBusError error);

    /** @brief 处理 CAN 设备状态变化。 */
    void onCanStateChanged(QCanBusDevice::CanBusDeviceState state);

    /** @brief 发送 CAN 帧。 */
    void sendCanFrame();

    /** @brief 空闲超时后输出 TCP 或串口缓存。 */
    void flushIdleFrame();

private:
    /** @brief 创建窗口控件和布局。 */
    void buildUi();

    /** @brief 连接窗口控件信号。 */
    void connectUiSignals();

    /** @brief 创建 UDP 配置页面。 */
    QWidget *createUdpPage();

    /** @brief 创建 TCP 配置页面。 */
    QWidget *createTcpPage();

    /** @brief 创建串口配置页面。 */
    QWidget *createSerialPage();

    /** @brief 创建 CAN 配置页面。 */
    QWidget *createCanPage();

    /** @brief 获取当前选择的传输类型。 */
    TesterTransport currentTransport() const;

    /** @brief 从界面读取当前分帧参数。 */
    TesterFrameSettings frameSettings() const;

    /** @brief 将当前分帧设置同步给解码器。 */
    void configureDecoder();

    /** @brief 将转义文本转换为实际字节，例如 \n 或 \x0D。 */
    QByteArray decodeEscapedText(const QString &text, bool *ok) const;

    /** @brief 将输入框内容转换为待发送字节。 */
    QByteArray payloadFromEditor(bool *ok, QString *error) const;

    /** @brief 按当前分帧设置自动封装发送数据。 */
    QByteArray frameForSending(const QByteArray &payload, bool *ok, QString *error) const;

    /** @brief 对收到或发出的完整报文做可视化展示。 */
    void displayFrame(const QString &direction, const QString &endpoint, const QByteArray &payload);

    /** @brief 对收到或发出的 CAN 帧做可视化展示。 */
    void displayCanFrame(const QString &direction, const QString &endpoint, const QCanBusFrame &frame);

    /** @brief 将字节流交给分帧器并展示完整帧。 */
    void processStreamBytes(const QByteArray &data, const QString &endpoint);

    /** @brief 启动空闲分帧定时器。 */
    void restartIdleTimer();

    /** @brief 关闭 TCP 和串口相关资源。 */
    void closeStreamResources();

    /** @brief 关闭 CAN 总线设备。 */
    void closeCanDevice();

    /** @brief 在日志区增加一行中文说明。 */
    void appendLog(const QString &message, bool error = false);

    /** @brief 设置连接状态文本。 */
    void setConnectionState(const QString &text, bool error = false);

    /** @brief 协议下拉框。 */
    QComboBox *m_transportCombo = nullptr;
    /** @brief 协议配置页。 */
    QStackedWidget *m_transportStack = nullptr;
    /** @brief 普通字节流发送页和 CAN 发送页。 */
    QStackedWidget *m_sendStack = nullptr;
    /** @brief 打开按钮。 */
    QPushButton *m_openButton = nullptr;
    /** @brief 关闭按钮。 */
    QPushButton *m_closeButton = nullptr;
    /** @brief 连接状态标签。 */
    QLabel *m_stateLabel = nullptr;

    /** @brief UDP 配置控件。 */
    QLineEdit *m_udpBindAddress = nullptr;
    QSpinBox *m_udpBindPort = nullptr;
    QLineEdit *m_udpRemoteAddress = nullptr;
    QSpinBox *m_udpRemotePort = nullptr;
    QCheckBox *m_udpMulticast = nullptr;
    QLineEdit *m_udpGroupAddress = nullptr;
    QComboBox *m_udpInterface = nullptr;

    /** @brief TCP 配置控件。 */
    QComboBox *m_tcpRole = nullptr;
    QLineEdit *m_tcpHost = nullptr;
    QSpinBox *m_tcpPort = nullptr;
    QComboBox *m_tcpFrameKind = nullptr;
    QLineEdit *m_tcpDelimiter = nullptr;
    QSpinBox *m_tcpFixedLength = nullptr;
    QComboBox *m_tcpLengthBytes = nullptr;
    QComboBox *m_tcpByteOrder = nullptr;
    QSpinBox *m_tcpIdleMs = nullptr;
    QSpinBox *m_tcpMaxBytes = nullptr;

    /** @brief 串口配置控件。 */
    QComboBox *m_serialPort = nullptr;
    QSpinBox *m_serialBaudRate = nullptr;
    QComboBox *m_serialDataBits = nullptr;
    QComboBox *m_serialParity = nullptr;
    QComboBox *m_serialStopBits = nullptr;
    QComboBox *m_serialFlowControl = nullptr;
    QComboBox *m_serialFrameKind = nullptr;
    QLineEdit *m_serialDelimiter = nullptr;
    QSpinBox *m_serialFixedLength = nullptr;
    QComboBox *m_serialLengthBytes = nullptr;
    QComboBox *m_serialByteOrder = nullptr;
    QSpinBox *m_serialIdleMs = nullptr;
    QSpinBox *m_serialMaxBytes = nullptr;

    /** @brief CAN 配置控件。 */
    QLineEdit *m_canInterface = nullptr;
    QSpinBox *m_canBitrate = nullptr;
    QCheckBox *m_canReceiveOwn = nullptr;
    QLineEdit *m_canId = nullptr;
    QCheckBox *m_canExtended = nullptr;
    QCheckBox *m_canRemote = nullptr;
    QSpinBox *m_canDlc = nullptr;
    QLineEdit *m_canData = nullptr;

    /** @brief 发送输入框。 */
    QPlainTextEdit *m_sendEditor = nullptr;
    /** @brief 是否按 HEX 读取发送输入。 */
    QCheckBox *m_sendHex = nullptr;
    /** @brief 是否自动封装 TCP/串口分帧。 */
    QCheckBox *m_autoFrame = nullptr;
    /** @brief 发送按钮。 */
    QPushButton *m_sendButton = nullptr;
    /** @brief CAN 帧发送按钮。 */
    QPushButton *m_canSendButton = nullptr;
    /** @brief 接收结果表格。 */
    QTableWidget *m_frameTable = nullptr;
    /** @brief 详细解析文本。 */
    QPlainTextEdit *m_detailView = nullptr;

    /** @brief UDP 套接字。 */
    QUdpSocket m_udpSocket;
    /** @brief TCP 服务端。 */
    QTcpServer m_tcpServer;
    /** @brief 当前 TCP 连接，测试端只保持一个活动连接。 */
    QPointer<QTcpSocket> m_tcpSocket;
    /** @brief 当前串口。 */
    QSerialPort m_serialPortDevice;
    /** @brief 当前 CAN 总线设备。 */
    QPointer<QCanBusDevice> m_canDevice;
    /** @brief TCP 或串口共享的分帧器。 */
    TestFrameDecoder m_decoder;
    /** @brief 空闲分帧定时器。 */
    QTimer *m_idleTimer = nullptr;
    /** @brief 当前是否已打开协议通道。 */
    bool m_transportOpen = false;
    /** @brief 当前 TCP/串口数据来源显示文本。 */
    QString m_streamEndpoint;
};

} // namespace edgeagent
