/**
 * @file TestMainWindow.cpp
 * @brief EdgeAgent 可视化测试客户端主窗口实现。
 * @author xubb
 */

#include "TestMainWindow.h"

#include <QCanBus>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkInterface>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSerialPortInfo>
#include <QSizePolicy>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

namespace edgeagent {
namespace {

/** @brief 创建带范围的整数输入框。 */
QSpinBox *createSpinBox(int minimum, int maximum, int value, QWidget *parent)
{
    auto *spin = new QSpinBox(parent); // 创建统一的数字输入控件。
    spin->setRange(minimum, maximum);
    spin->setValue(value);
    return spin;
}

/** @brief 将字节转换为适合日志显示的 ASCII 文本。 */
QString printableAscii(const QByteArray &payload)
{
    QString result; // 保存可读 ASCII 字符和不可见字符占位符。
    result.reserve(payload.size());
    for (const char value : payload) {
        const unsigned char byte = static_cast<unsigned char>(value);
        result.append(byte >= 0x20 && byte <= 0x7e ? QChar(byte) : QLatin1Char('.'));
    }
    return result;
}

/** @brief 将分帧类型转换为中文名称。 */
QString frameKindName(TestFrameKind kind)
{
    switch (kind) {
    case TestFrameKind::Delimiter:
        return QStringLiteral("分隔符");
    case TestFrameKind::FixedLength:
        return QStringLiteral("固定长度");
    case TestFrameKind::LengthPrefix:
        return QStringLiteral("长度前缀");
    case TestFrameKind::IdleTimeout:
        return QStringLiteral("空闲超时");
    }
    return QStringLiteral("未知");
}

} // namespace

TestMainWindow::TestMainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("EdgeAgent 上位机模拟测试客户端"));
    resize(1280, 820);
    buildUi();
    connectUiSignals();
    refreshSerialPorts();
    updateTransportPage(m_transportCombo->currentIndex());
}

TestMainWindow::~TestMainWindow()
{
    closeTransport();
}

void TestMainWindow::buildUi()
{
    auto *central = new QWidget(this); // 主窗口中心控件。
    auto *rootLayout = new QVBoxLayout(central); // 主垂直布局。
    rootLayout->setContentsMargins(8, 8, 8, 8);
    rootLayout->setSpacing(6);

    auto *toolbar = new QHBoxLayout(); // 顶部协议和操作布局。
    toolbar->addWidget(new QLabel(QStringLiteral("测试协议："), central));
    m_transportCombo = new QComboBox(central);
    m_transportCombo->addItem(QStringLiteral("UDP 数据报"), static_cast<int>(TesterTransport::Udp));
    m_transportCombo->addItem(QStringLiteral("TCP 字节流"), static_cast<int>(TesterTransport::Tcp));
    m_transportCombo->addItem(QStringLiteral("串口字节流"), static_cast<int>(TesterTransport::Serial));
    m_transportCombo->addItem(QStringLiteral("CAN 总线帧"), static_cast<int>(TesterTransport::Can));
    toolbar->addWidget(m_transportCombo);
    m_openButton = new QPushButton(QStringLiteral("打开接收"), central);
    m_closeButton = new QPushButton(QStringLiteral("关闭"), central);
    auto *clearButton = new QPushButton(QStringLiteral("清空显示"), central);
    m_stateLabel = new QLabel(QStringLiteral("未打开"), central);
    m_stateLabel->setStyleSheet(QStringLiteral("color:#666666; font-weight:bold;"));
    toolbar->addSpacing(12);
    toolbar->addWidget(m_openButton);
    toolbar->addWidget(m_closeButton);
    toolbar->addWidget(clearButton);
    toolbar->addStretch();
    toolbar->addWidget(new QLabel(QStringLiteral("状态："), central));
    toolbar->addWidget(m_stateLabel);
    rootLayout->addLayout(toolbar);

    m_transportStack = new QStackedWidget(central); // 三种协议的配置页容器。
    // 配置页按实际内容高度显示，避免 UDP 页面产生大块空白。
    m_transportStack->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    m_transportStack->addWidget(createUdpPage());
    m_transportStack->addWidget(createTcpPage());
    m_transportStack->addWidget(createSerialPage());
    m_transportStack->addWidget(createCanPage());
    rootLayout->addWidget(m_transportStack);

    auto *sendGroup = new QGroupBox(QStringLiteral("发送报文"), central); // 发送区容器。
    auto *sendLayout = new QVBoxLayout(sendGroup);
    m_sendEditor = new QPlainTextEdit(sendGroup);
    m_sendEditor->setPlaceholderText(QStringLiteral("输入 ASCII 文本或 HEX 字符串，例如：01020304"));
    m_sendEditor->setFixedHeight(72);
    sendLayout->addWidget(m_sendEditor);
    auto *sendOptions = new QHBoxLayout(); // 发送模式选项布局。
    m_sendHex = new QCheckBox(QStringLiteral("按 HEX 发送"), sendGroup);
    m_autoFrame = new QCheckBox(QStringLiteral("自动封装 TCP/串口分帧"), sendGroup);
    m_autoFrame->setChecked(true);
    m_autoFrame->setToolTip(QStringLiteral("分隔符模式自动追加分隔符；长度前缀模式自动添加长度；固定长度模式自动补零。"));
    m_sendButton = new QPushButton(QStringLiteral("发送报文"), sendGroup);
    sendOptions->addWidget(m_sendHex);
    sendOptions->addWidget(m_autoFrame);
    sendOptions->addStretch();
    sendOptions->addWidget(m_sendButton);
    sendLayout->addLayout(sendOptions);
    m_sendStack = new QStackedWidget(central); // 普通字节流和 CAN 专用发送页面。
    m_sendStack->addWidget(sendGroup);

    auto *canSendGroup = new QGroupBox(QStringLiteral("发送 CAN 帧"), central); // CAN 发送区容器。
    auto *canSendLayout = new QFormLayout(canSendGroup);
    m_canId = new QLineEdit(QStringLiteral("0x123"), canSendGroup);
    m_canExtended = new QCheckBox(QStringLiteral("扩展帧（29 位 ID）"), canSendGroup);
    m_canRemote = new QCheckBox(QStringLiteral("远程请求帧（RTR）"), canSendGroup);
    m_canDlc = createSpinBox(0, 8, 4, canSendGroup);
    m_canData = new QLineEdit(QStringLiteral("01020304"), canSendGroup);
    m_canData->setPlaceholderText(QStringLiteral("HEX 数据，例如 01020304；RTR 帧必须为空"));
    m_canSendButton = new QPushButton(QStringLiteral("发送 CAN 帧"), canSendGroup);
    canSendLayout->addRow(QStringLiteral("CAN ID："), m_canId);
    canSendLayout->addRow(QStringLiteral("帧类型："), m_canExtended);
    canSendLayout->addRow(QString(), m_canRemote);
    canSendLayout->addRow(QStringLiteral("DLC："), m_canDlc);
    canSendLayout->addRow(QStringLiteral("数据 HEX："), m_canData);
    canSendLayout->addRow(QString(), m_canSendButton);
    m_sendStack->addWidget(canSendGroup);
    rootLayout->addWidget(m_sendStack);

    auto *splitter = new QSplitter(Qt::Vertical, central); // 接收表格和详细日志的分割区。
    m_frameTable = new QTableWidget(splitter);
    m_frameTable->setColumnCount(7);
    m_frameTable->setHorizontalHeaderLabels({QStringLiteral("时间"), QStringLiteral("方向"), QStringLiteral("协议"),
                                             QStringLiteral("来源/目标"), QStringLiteral("长度"), QStringLiteral("HEX"),
                                             QStringLiteral("解析结果")});
    m_frameTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_frameTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_frameTable->setMinimumHeight(180);
    m_frameTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_frameTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_frameTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_frameTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_frameTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_frameTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Interactive);
    m_frameTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
    m_frameTable->setAlternatingRowColors(true);

    m_detailView = new QPlainTextEdit(splitter);
    m_detailView->setReadOnly(true);
    m_detailView->setPlaceholderText(QStringLiteral("接收报文的 HEX、ASCII 和 JSON 解析结果会显示在这里。"));
    m_detailView->setMinimumHeight(120);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    rootLayout->addWidget(splitter, 1);

    m_idleTimer = new QTimer(this); // TCP 和串口的空闲分帧定时器。
    m_idleTimer->setSingleShot(true);
    connect(clearButton, &QPushButton::clicked, this, &TestMainWindow::clearView);
    connect(m_idleTimer, &QTimer::timeout, this, &TestMainWindow::flushIdleFrame);
    setCentralWidget(central);
}

QWidget *TestMainWindow::createUdpPage()
{
    auto *page = new QWidget(this); // UDP 配置页。
    auto *layout = new QFormLayout(page);
    m_udpBindAddress = new QLineEdit(QStringLiteral("0.0.0.0"), page);
    m_udpBindPort = createSpinBox(0, 65535, 19090, page);
    m_udpRemoteAddress = new QLineEdit(QStringLiteral("127.0.0.1"), page);
    m_udpRemotePort = createSpinBox(1, 65535, 18080, page);
    layout->addRow(QStringLiteral("本地绑定地址："), m_udpBindAddress);
    layout->addRow(QStringLiteral("本地接收端口："), m_udpBindPort);
    layout->addRow(QStringLiteral("发送目标地址："), m_udpRemoteAddress);
    layout->addRow(QStringLiteral("发送目标端口："), m_udpRemotePort);

    m_udpMulticast = new QCheckBox(QStringLiteral("加入 UDP 组播组"), page);
    m_udpGroupAddress = new QLineEdit(QStringLiteral("239.255.0.1"), page);
    m_udpInterface = new QComboBox(page);
    m_udpInterface->addItem(QStringLiteral("自动选择网卡"), QString());
    layout->addRow(m_udpMulticast);
    layout->addRow(QStringLiteral("组播地址："), m_udpGroupAddress);
    layout->addRow(QStringLiteral("组播网卡："), m_udpInterface);
    layout->addRow(new QLabel(QStringLiteral("UDP 每个数据报直接作为一帧，不使用 TCP 分帧配置。"), page));
    return page;
}

QWidget *TestMainWindow::createTcpPage()
{
    auto *page = new QWidget(this); // TCP 配置页。
    auto *layout = new QFormLayout(page);
    m_tcpRole = new QComboBox(page);
    m_tcpRole->addItem(QStringLiteral("客户端：连接 EdgeAgent"), QStringLiteral("client"));
    m_tcpRole->addItem(QStringLiteral("服务端：等待 EdgeAgent"), QStringLiteral("server"));
    m_tcpHost = new QLineEdit(QStringLiteral("127.0.0.1"), page);
    m_tcpPort = createSpinBox(1, 65535, 18081, page);
    m_tcpFrameKind = new QComboBox(page);
    m_tcpFrameKind->addItem(QStringLiteral("分隔符"), static_cast<int>(TestFrameKind::Delimiter));
    m_tcpFrameKind->addItem(QStringLiteral("固定长度"), static_cast<int>(TestFrameKind::FixedLength));
    m_tcpFrameKind->addItem(QStringLiteral("长度前缀"), static_cast<int>(TestFrameKind::LengthPrefix));
    m_tcpFrameKind->addItem(QStringLiteral("空闲超时"), static_cast<int>(TestFrameKind::IdleTimeout));
    m_tcpDelimiter = new QLineEdit(QStringLiteral("\\n"), page);
    m_tcpDelimiter->setToolTip(QStringLiteral("支持 \\n、\\r、\\t 和 \\xNN 转义。"));
    m_tcpFixedLength = createSpinBox(1, 1024, 8, page);
    m_tcpLengthBytes = new QComboBox(page);
    m_tcpLengthBytes->addItems({QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("4")});
    m_tcpLengthBytes->setCurrentText(QStringLiteral("2"));
    m_tcpByteOrder = new QComboBox(page);
    m_tcpByteOrder->addItem(QStringLiteral("大端"), false);
    m_tcpByteOrder->addItem(QStringLiteral("小端"), true);
    m_tcpIdleMs = createSpinBox(1, 60000, 20, page);
    m_tcpMaxBytes = createSpinBox(1, 16 * 1024 * 1024, 65536, page);
    layout->addRow(QStringLiteral("工作角色："), m_tcpRole);
    layout->addRow(QStringLiteral("目标/监听地址："), m_tcpHost);
    layout->addRow(QStringLiteral("目标/监听端口："), m_tcpPort);
    layout->addRow(QStringLiteral("接收分帧方式："), m_tcpFrameKind);
    layout->addRow(QStringLiteral("分隔符："), m_tcpDelimiter);
    layout->addRow(QStringLiteral("固定长度："), m_tcpFixedLength);
    layout->addRow(QStringLiteral("长度前缀字节数："), m_tcpLengthBytes);
    layout->addRow(QStringLiteral("长度字节序："), m_tcpByteOrder);
    layout->addRow(QStringLiteral("空闲超时："), m_tcpIdleMs);
    layout->addRow(QStringLiteral("最大帧长度："), m_tcpMaxBytes);
    layout->addRow(new QLabel(QStringLiteral("当前测试端只保持一个 TCP 活动连接，适合模拟单台上位机。"), page));
    return page;
}

QWidget *TestMainWindow::createSerialPage()
{
    auto *page = new QWidget(this); // 串口配置页。
    auto *layout = new QFormLayout(page);
    m_serialPort = new QComboBox(page);
    m_serialPort->setEditable(true);
    m_serialPort->setCurrentText(QStringLiteral("COM3"));
    auto *refreshButton = new QPushButton(QStringLiteral("刷新串口"), page);
    auto *portRow = new QWidget(page); // 串口名称和刷新按钮的组合控件。
    auto *portLayout = new QHBoxLayout(portRow);
    portLayout->setContentsMargins(0, 0, 0, 0);
    portLayout->addWidget(m_serialPort, 1);
    portLayout->addWidget(refreshButton);
    layout->addRow(QStringLiteral("串口："), portRow);

    m_serialBaudRate = createSpinBox(1200, 921600, 115200, page);
    m_serialBaudRate->setSingleStep(1200);
    m_serialDataBits = new QComboBox(page);
    m_serialDataBits->addItems({QStringLiteral("5"), QStringLiteral("6"), QStringLiteral("7"), QStringLiteral("8")});
    m_serialDataBits->setCurrentText(QStringLiteral("8"));
    m_serialParity = new QComboBox(page);
    m_serialParity->addItems({QStringLiteral("无校验"), QStringLiteral("偶校验"), QStringLiteral("奇校验"), QStringLiteral("空格校验"), QStringLiteral("标记校验")});
    m_serialParity->setCurrentIndex(0);
    m_serialStopBits = new QComboBox(page);
    m_serialStopBits->addItems({QStringLiteral("1"), QStringLiteral("1.5"), QStringLiteral("2")});
    m_serialFlowControl = new QComboBox(page);
    m_serialFlowControl->addItems({QStringLiteral("无流控"), QStringLiteral("硬件流控"), QStringLiteral("软件流控")});
    m_serialFrameKind = new QComboBox(page);
    m_serialFrameKind->addItem(QStringLiteral("分隔符"), static_cast<int>(TestFrameKind::Delimiter));
    m_serialFrameKind->addItem(QStringLiteral("固定长度"), static_cast<int>(TestFrameKind::FixedLength));
    m_serialFrameKind->addItem(QStringLiteral("长度前缀"), static_cast<int>(TestFrameKind::LengthPrefix));
    m_serialFrameKind->addItem(QStringLiteral("空闲超时"), static_cast<int>(TestFrameKind::IdleTimeout));
    m_serialDelimiter = new QLineEdit(QStringLiteral("\\n"), page);
    m_serialFixedLength = createSpinBox(1, 1024, 8, page);
    m_serialLengthBytes = new QComboBox(page);
    m_serialLengthBytes->addItems({QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("4")});
    m_serialLengthBytes->setCurrentText(QStringLiteral("2"));
    m_serialByteOrder = new QComboBox(page);
    m_serialByteOrder->addItem(QStringLiteral("大端"), false);
    m_serialByteOrder->addItem(QStringLiteral("小端"), true);
    m_serialIdleMs = createSpinBox(1, 60000, 20, page);
    m_serialMaxBytes = createSpinBox(1, 16 * 1024 * 1024, 65536, page);
    layout->addRow(QStringLiteral("波特率："), m_serialBaudRate);
    layout->addRow(QStringLiteral("数据位："), m_serialDataBits);
    layout->addRow(QStringLiteral("校验："), m_serialParity);
    layout->addRow(QStringLiteral("停止位："), m_serialStopBits);
    layout->addRow(QStringLiteral("流控："), m_serialFlowControl);
    layout->addRow(QStringLiteral("接收分帧方式："), m_serialFrameKind);
    layout->addRow(QStringLiteral("分隔符："), m_serialDelimiter);
    layout->addRow(QStringLiteral("固定长度："), m_serialFixedLength);
    layout->addRow(QStringLiteral("长度前缀字节数："), m_serialLengthBytes);
    layout->addRow(QStringLiteral("长度字节序："), m_serialByteOrder);
    layout->addRow(QStringLiteral("空闲超时："), m_serialIdleMs);
    layout->addRow(QStringLiteral("最大帧长度："), m_serialMaxBytes);
    layout->addRow(new QLabel(QStringLiteral("支持 Windows COM3 和 Linux/WSL 的 /dev/ttyUSB0 等名称。"), page));
    connect(refreshButton, &QPushButton::clicked, this, &TestMainWindow::refreshSerialPorts);
    return page;
}

QWidget *TestMainWindow::createCanPage()
{
    auto *page = new QWidget(this); // CAN 配置页。
    auto *layout = new QFormLayout(page);
    m_canInterface = new QLineEdit(QStringLiteral("vcan0"), page);
    m_canBitrate = createSpinBox(10000, 1000000, 500000, page);
    m_canBitrate->setSingleStep(10000);
    m_canReceiveOwn = new QCheckBox(QStringLiteral("接收本程序发出的回环帧"), page);
    m_canReceiveOwn->setChecked(true);
    layout->addRow(QStringLiteral("CAN 接口："), m_canInterface);
    layout->addRow(QStringLiteral("仲裁波特率："), m_canBitrate);
    layout->addRow(m_canReceiveOwn);
    layout->addRow(new QLabel(QStringLiteral("Linux/WSL 使用 SocketCAN，例如 can0 或 vcan0；当前实现为经典 CAN 2.0。"), page));
    layout->addRow(new QLabel(QStringLiteral("Windows 原生 Qt 暂不直接使用 SocketCAN，请在 Linux/WSL 中测试。"), page));
    return page;
}

void TestMainWindow::connectUiSignals()
{
    connect(m_transportCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TestMainWindow::updateTransportPage);
    connect(m_openButton, &QPushButton::clicked, this, &TestMainWindow::openTransport);
    connect(m_closeButton, &QPushButton::clicked, this, &TestMainWindow::closeTransport);
    connect(m_sendButton, &QPushButton::clicked, this, &TestMainWindow::sendPayload);
    connect(m_canSendButton, &QPushButton::clicked, this, &TestMainWindow::sendCanFrame);
    connect(&m_udpSocket, &QUdpSocket::readyRead, this, &TestMainWindow::readUdpDatagrams);
    connect(&m_tcpServer, &QTcpServer::newConnection, this, &TestMainWindow::acceptTcpConnection);
    connect(&m_serialPortDevice, &QSerialPort::readyRead, this, &TestMainWindow::readSerialBytes);
    connect(&m_serialPortDevice, &QSerialPort::errorOccurred, this, &TestMainWindow::onSerialError);
}

void TestMainWindow::updateTransportPage(int index)
{
    if (m_transportStack != nullptr) {
        m_transportStack->setCurrentIndex(index);
        // 根据当前页面内容调整高度，切换协议时不再保留其他页面的空白区域。
        if (m_transportStack->currentWidget() != nullptr) {
            m_transportStack->currentWidget()->adjustSize();
            m_transportStack->setFixedHeight(m_transportStack->currentWidget()->sizeHint().height());
        }
    }
    const bool can = currentTransport() == TesterTransport::Can;
    const bool framedStream = currentTransport() == TesterTransport::Tcp || currentTransport() == TesterTransport::Serial;
    m_sendStack->setCurrentIndex(can ? 1 : 0);
    m_autoFrame->setEnabled(framedStream);
    m_autoFrame->setVisible(framedStream);
    if (!framedStream) {
        m_autoFrame->setChecked(false);
    }
}

TesterTransport TestMainWindow::currentTransport() const
{
    return static_cast<TesterTransport>(m_transportCombo->currentData().toInt());
}

TesterFrameSettings TestMainWindow::frameSettings() const
{
    TesterFrameSettings settings; // 当前页面的分帧配置。
    const bool tcp = currentTransport() == TesterTransport::Tcp;
    const QComboBox *kindBox = tcp ? m_tcpFrameKind : m_serialFrameKind;
    settings.kind = static_cast<TestFrameKind>(kindBox->currentData().toInt());
    const QLineEdit *delimiter = tcp ? m_tcpDelimiter : m_serialDelimiter;
    bool delimiterOk = false;
    settings.delimiter = decodeEscapedText(delimiter->text(), &delimiterOk);
    if (!delimiterOk) {
        settings.delimiter = QByteArrayLiteral("\n");
    }
    settings.fixedLength = (tcp ? m_tcpFixedLength : m_serialFixedLength)->value();
    settings.lengthBytes = (tcp ? m_tcpLengthBytes : m_serialLengthBytes)->currentText().toInt();
    settings.littleEndian = (tcp ? m_tcpByteOrder : m_serialByteOrder)->currentData().toBool();
    settings.idleMs = (tcp ? m_tcpIdleMs : m_serialIdleMs)->value();
    settings.maxBytes = (tcp ? m_tcpMaxBytes : m_serialMaxBytes)->value();
    return settings;
}

void TestMainWindow::configureDecoder()
{
    const TesterFrameSettings settings = frameSettings(); // 读取当前页面分帧参数。
    m_decoder.configure(settings.kind, settings.delimiter, settings.fixedLength, settings.lengthBytes,
                        settings.littleEndian, settings.maxBytes);
}

QByteArray TestMainWindow::decodeEscapedText(const QString &text, bool *ok) const
{
    bool valid = true; // 记录转义文本是否完整有效。
    QByteArray result; // 转换后的实际字节。
    for (int index = 0; index < text.size(); ++index) {
        const QChar current = text.at(index);
        if (current != QLatin1Char('\\')) {
            result.append(QString(current).toUtf8());
            continue;
        }
        if (index + 1 >= text.size()) {
            valid = false;
            break;
        }
        const QChar escaped = text.at(++index);
        if (escaped == QLatin1Char('n')) {
            result.append('\n');
        } else if (escaped == QLatin1Char('r')) {
            result.append('\r');
        } else if (escaped == QLatin1Char('t')) {
            result.append('\t');
        } else if (escaped == QLatin1Char('\\')) {
            result.append('\\');
        } else if (escaped == QLatin1Char('x') && index + 2 < text.size()) {
            const QString hex = text.mid(index + 1, 2);
            bool hexOk = false;
            const int value = hex.toInt(&hexOk, 16);
            if (!hexOk) {
                valid = false;
                break;
            }
            result.append(static_cast<char>(value));
            index += 2;
        } else {
            valid = false;
            break;
        }
    }
    if (ok != nullptr) {
        *ok = valid;
    }
    return result;
}

QByteArray TestMainWindow::payloadFromEditor(bool *ok, QString *error) const
{
    const QString text = m_sendEditor->toPlainText(); // 读取发送输入框内容。
    if (text.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("发送内容不能为空");
        }
        if (ok != nullptr) {
            *ok = false;
        }
        return {};
    }
    if (!m_sendHex->isChecked()) {
        if (ok != nullptr) {
            *ok = true;
        }
        return text.toUtf8();
    }
    QString compact = text; // HEX 模式允许空格和换行。
    compact.remove(QRegularExpression(QStringLiteral("\\s")));
    if (compact.isEmpty() || compact.size() % 2 != 0 || !compact.contains(QRegularExpression(QStringLiteral("^[0-9A-Fa-f]+$")))) {
        if (error != nullptr) {
            *error = QStringLiteral("HEX 内容必须是偶数个十六进制字符");
        }
        if (ok != nullptr) {
            *ok = false;
        }
        return {};
    }
    if (ok != nullptr) {
        *ok = true;
    }
    return QByteArray::fromHex(compact.toLatin1());
}

QByteArray TestMainWindow::frameForSending(const QByteArray &payload, bool *ok, QString *error) const
{
    if (!m_autoFrame->isChecked() || currentTransport() == TesterTransport::Udp) {
        if (ok != nullptr) {
            *ok = true;
        }
        return payload;
    }
    const TesterFrameSettings settings = frameSettings(); // 读取自动封装参数。
    if (settings.kind == TestFrameKind::IdleTimeout) {
        if (ok != nullptr) {
            *ok = true;
        }
        return payload;
    }
    if (settings.kind == TestFrameKind::Delimiter) {
        if (settings.delimiter.isEmpty()) {
            if (error != nullptr) {
                *error = QStringLiteral("分隔符不能为空");
            }
            if (ok != nullptr) {
                *ok = false;
            }
            return {};
        }
        if (ok != nullptr) {
            *ok = true;
        }
        return payload + settings.delimiter;
    }
    if (settings.kind == TestFrameKind::FixedLength) {
        if (payload.size() > settings.fixedLength) {
            if (error != nullptr) {
                *error = QStringLiteral("发送内容长度超过固定帧长度 %1").arg(settings.fixedLength);
            }
            if (ok != nullptr) {
                *ok = false;
            }
            return {};
        }
        if (ok != nullptr) {
            *ok = true;
        }
        return payload.leftJustified(settings.fixedLength, '\0');
    }
    const quint64 maxLength = settings.lengthBytes == 4 ? 0xffffffffULL : ((1ULL << (settings.lengthBytes * 8)) - 1ULL);
    if (static_cast<quint64>(payload.size()) > maxLength || payload.size() > settings.maxBytes) {
        if (error != nullptr) {
            *error = QStringLiteral("发送内容长度不能用当前长度前缀表示");
        }
        if (ok != nullptr) {
            *ok = false;
        }
        return {};
    }
    QByteArray prefix(settings.lengthBytes, '\0'); // 创建长度前缀。
    for (int index = 0; index < settings.lengthBytes; ++index) {
        const int shift = settings.littleEndian ? index * 8 : (settings.lengthBytes - 1 - index) * 8;
        prefix[index] = static_cast<char>((payload.size() >> shift) & 0xff);
    }
    if (ok != nullptr) {
        *ok = true;
    }
    return prefix + payload;
}

void TestMainWindow::openTransport()
{
    closeTransport();
    if (currentTransport() == TesterTransport::Udp) {
        QHostAddress bindAddress; // UDP 本地绑定地址。
        QHostAddress remoteAddress; // UDP 发送目标地址。
        if (!bindAddress.setAddress(m_udpBindAddress->text()) || !remoteAddress.setAddress(m_udpRemoteAddress->text())) {
            setConnectionState(QStringLiteral("UDP 地址无效"), true);
            return;
        }
        const auto mode = QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint;
        if (!m_udpSocket.bind(bindAddress, static_cast<quint16>(m_udpBindPort->value()), mode)) {
            setConnectionState(QStringLiteral("UDP 绑定失败：%1").arg(m_udpSocket.errorString()), true);
            return;
        }
        if (m_udpMulticast->isChecked()) {
            QHostAddress groupAddress; // UDP 组播地址。
            if (!groupAddress.setAddress(m_udpGroupAddress->text()) || !groupAddress.isMulticast()) {
                m_udpSocket.close();
                setConnectionState(QStringLiteral("组播地址无效"), true);
                return;
            }
            bool joined = false; // 记录是否成功加入组播组。
            const QString interfaceName = m_udpInterface->currentData().toString();
            if (interfaceName.isEmpty()) {
                joined = m_udpSocket.joinMulticastGroup(groupAddress);
            } else {
                joined = m_udpSocket.joinMulticastGroup(groupAddress, QNetworkInterface::interfaceFromName(interfaceName));
            }
            if (!joined) {
                m_udpSocket.close();
                setConnectionState(QStringLiteral("加入 UDP 组播失败：%1").arg(m_udpSocket.errorString()), true);
                return;
            }
        }
        Q_UNUSED(remoteAddress)
        m_transportOpen = true;
        setConnectionState(QStringLiteral("UDP 已打开，本地端口 %1").arg(m_udpSocket.localPort()));
        appendLog(QStringLiteral("UDP 接收已启动，可向 %1:%2 发送报文").arg(m_udpRemoteAddress->text()).arg(m_udpRemotePort->value()));
        return;
    }

    if (currentTransport() == TesterTransport::Can) {
        QCanBus *bus = QCanBus::instance(); // 获取 Qt Serial Bus 工厂。
        const QString backend = QStringLiteral("socketcan"); // 当前 Linux/WSL 测试使用 SocketCAN 后端。
        if (!bus->plugins().contains(backend)) {
            setConnectionState(QStringLiteral("CAN 后端不可用：%1").arg(backend), true);
            return;
        }
        QString error; // 创建 CAN 设备时的错误信息。
        m_canDevice = bus->createDevice(backend, m_canInterface->text().trimmed(), &error);
        if (m_canDevice == nullptr) {
            setConnectionState(QStringLiteral("CAN 设备创建失败：%1").arg(error), true);
            return;
        }
        connect(m_canDevice, &QCanBusDevice::framesReceived, this, &TestMainWindow::readCanFrames);
        connect(m_canDevice, &QCanBusDevice::errorOccurred, this, &TestMainWindow::onCanError);
        connect(m_canDevice, &QCanBusDevice::stateChanged, this, &TestMainWindow::onCanStateChanged);
        // Qt 5.12 的 SocketCAN 插件不支持 BitRateKey；真实接口由 ip link 配置波特率。
        m_canDevice->setConfigurationParameter(QCanBusDevice::LoopbackKey, true);
        m_canDevice->setConfigurationParameter(QCanBusDevice::ReceiveOwnKey, m_canReceiveOwn->isChecked());
        if (!m_canDevice->connectDevice()) {
            const QString detail = m_canDevice->errorString();
            closeCanDevice();
            setConnectionState(QStringLiteral("CAN 接口打开失败：%1").arg(detail), true);
            return;
        }
        m_transportOpen = true;
        m_streamEndpoint = m_canInterface->text().trimmed();
        setConnectionState(QStringLiteral("CAN 已打开：%1").arg(m_streamEndpoint));
        appendLog(QStringLiteral("CAN 后端 socketcan 已连接，波特率=%1").arg(m_canBitrate->value()));
        return;
    }

    configureDecoder();
    if (currentTransport() == TesterTransport::Tcp) {
        const QString role = m_tcpRole->currentData().toString();
        const QHostAddress host(m_tcpHost->text());
        if (host.isNull()) {
            setConnectionState(QStringLiteral("TCP 地址无效"), true);
            return;
        }
        if (role == QStringLiteral("server")) {
            if (!m_tcpServer.listen(host, static_cast<quint16>(m_tcpPort->value()))) {
                setConnectionState(QStringLiteral("TCP 监听失败：%1").arg(m_tcpServer.errorString()), true);
                return;
            }
            m_transportOpen = true;
            m_streamEndpoint = QStringLiteral("监听 %1:%2").arg(m_tcpHost->text()).arg(m_tcpPort->value());
            setConnectionState(QStringLiteral("TCP 服务端已监听"));
            appendLog(QStringLiteral("等待 EdgeAgent TCP 客户端连接"));
        } else {
            m_tcpSocket = new QTcpSocket(this); // 创建 TCP 客户端套接字。
            connect(m_tcpSocket, &QTcpSocket::readyRead, this, &TestMainWindow::readTcpBytes);
            connect(m_tcpSocket, &QTcpSocket::connected, this, &TestMainWindow::onTcpConnected);
            connect(m_tcpSocket, &QTcpSocket::disconnected, this, &TestMainWindow::onTcpDisconnected);
            connect(m_tcpSocket, static_cast<void (QAbstractSocket::*)(QAbstractSocket::SocketError)>(&QAbstractSocket::error),
                    this, &TestMainWindow::onTcpError);
            m_transportOpen = true;
            m_streamEndpoint = QStringLiteral("%1:%2").arg(m_tcpHost->text()).arg(m_tcpPort->value());
            m_tcpSocket->connectToHost(host, static_cast<quint16>(m_tcpPort->value()));
            setConnectionState(QStringLiteral("TCP 正在连接 %1").arg(m_streamEndpoint));
        }
        return;
    }

    m_serialPortDevice.setPortName(m_serialPort->currentText().trimmed());
    m_serialPortDevice.setBaudRate(m_serialBaudRate->value());
    m_serialPortDevice.setDataBits(static_cast<QSerialPort::DataBits>(m_serialDataBits->currentText().toInt()));
    const QSerialPort::Parity parity = m_serialParity->currentIndex() == 1
        ? QSerialPort::EvenParity
        : (m_serialParity->currentIndex() == 2
               ? QSerialPort::OddParity
               : (m_serialParity->currentIndex() == 3 ? QSerialPort::SpaceParity
                                                      : (m_serialParity->currentIndex() == 4 ? QSerialPort::MarkParity
                                                                                              : QSerialPort::NoParity)));
    m_serialPortDevice.setParity(parity);
    m_serialPortDevice.setStopBits(m_serialStopBits->currentText() == QStringLiteral("1.5")
                                       ? QSerialPort::OneAndHalfStop
                                       : (m_serialStopBits->currentText() == QStringLiteral("2") ? QSerialPort::TwoStop : QSerialPort::OneStop));
    m_serialPortDevice.setFlowControl(static_cast<QSerialPort::FlowControl>(m_serialFlowControl->currentIndex()));
    if (!m_serialPortDevice.open(QIODevice::ReadWrite)) {
        setConnectionState(QStringLiteral("串口打开失败：%1").arg(m_serialPortDevice.errorString()), true);
        return;
    }
    m_transportOpen = true;
    m_streamEndpoint = m_serialPortDevice.portName();
    setConnectionState(QStringLiteral("串口已打开：%1").arg(m_streamEndpoint));
    appendLog(QStringLiteral("串口参数：%1，%2 数据位，%3，%4 停止位").arg(m_serialBaudRate->value())
                  .arg(m_serialDataBits->currentText(), m_serialParity->currentText(), m_serialStopBits->currentText()));
}

void TestMainWindow::closeTransport()
{
    m_idleTimer->stop();
    if (m_udpSocket.isOpen()) {
        if (m_udpMulticast->isChecked()) {
            QHostAddress groupAddress;
            if (groupAddress.setAddress(m_udpGroupAddress->text())) {
                m_udpSocket.leaveMulticastGroup(groupAddress);
            }
        }
        m_udpSocket.close();
    }
    closeCanDevice();
    closeStreamResources();
    if (m_transportOpen) {
        setConnectionState(QStringLiteral("已关闭"));
    }
    m_transportOpen = false;
}

void TestMainWindow::closeStreamResources()
{
    m_tcpServer.close();
    if (m_tcpSocket != nullptr) {
        m_tcpSocket->disconnectFromHost();
        if (m_tcpSocket->state() != QAbstractSocket::UnconnectedState) {
            m_tcpSocket->abort();
        }
        m_tcpSocket->deleteLater();
        m_tcpSocket = nullptr;
    }
    if (m_serialPortDevice.isOpen()) {
        m_serialPortDevice.close();
    }
    m_decoder.reset();
    m_streamEndpoint.clear();
}

void TestMainWindow::closeCanDevice()
{
    if (m_canDevice == nullptr) {
        return;
    }
    QCanBusDevice *device = m_canDevice; // 保存设备指针，随后先从成员中摘除。
    m_canDevice = nullptr;
    device->disconnectDevice();
    device->deleteLater();
}

void TestMainWindow::sendPayload()
{
    if (!m_transportOpen) {
        setConnectionState(QStringLiteral("请先打开通信通道"), true);
        return;
    }
    if (currentTransport() == TesterTransport::Can) {
        sendCanFrame();
        return;
    }
    bool payloadOk = false; // 记录输入数据是否有效。
    QString error; // 保存发送数据校验错误。
    const QByteArray payload = payloadFromEditor(&payloadOk, &error);
    if (!payloadOk) {
        setConnectionState(error, true);
        return;
    }
    bool frameOk = false; // 记录自动封帧是否成功。
    const QByteArray bytes = frameForSending(payload, &frameOk, &error);
    if (!frameOk) {
        setConnectionState(error, true);
        return;
    }

    bool sent = false; // 记录底层发送是否成功。
    QString endpoint; // 记录本次发送目标。
    if (currentTransport() == TesterTransport::Udp) {
        QHostAddress address;
        if (!address.setAddress(m_udpRemoteAddress->text())) {
            setConnectionState(QStringLiteral("UDP 发送地址无效"), true);
            return;
        }
        sent = m_udpSocket.writeDatagram(bytes, address, static_cast<quint16>(m_udpRemotePort->value())) == bytes.size();
        endpoint = QStringLiteral("%1:%2").arg(m_udpRemoteAddress->text()).arg(m_udpRemotePort->value());
    } else if (currentTransport() == TesterTransport::Tcp) {
        sent = m_tcpSocket != nullptr && m_tcpSocket->state() == QAbstractSocket::ConnectedState
            && m_tcpSocket->write(bytes) == bytes.size();
        endpoint = m_streamEndpoint;
    } else {
        sent = m_serialPortDevice.isOpen() && m_serialPortDevice.write(bytes) == bytes.size();
        endpoint = m_streamEndpoint;
    }
    if (!sent) {
        setConnectionState(QStringLiteral("发送失败"), true);
        appendLog(QStringLiteral("发送失败：%1").arg(endpoint), true);
        return;
    }
    displayFrame(QStringLiteral("发送"), endpoint, bytes);
    appendLog(QStringLiteral("已发送 %1 字节到 %2").arg(bytes.size()).arg(endpoint));
}

void TestMainWindow::sendCanFrame()
{
    if (!m_transportOpen || m_canDevice == nullptr || m_canDevice->state() != QCanBusDevice::ConnectedState) {
        setConnectionState(QStringLiteral("请先打开 CAN 接口"), true);
        return;
    }
    const QString idText = m_canId->text().trimmed(); // 读取 CAN ID 文本。
    if (!idText.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        setConnectionState(QStringLiteral("CAN ID 必须使用 0x 前缀"), true);
        return;
    }
    bool idOk = false; // 记录 CAN ID 是否成功转换。
    const qulonglong idValue = idText.mid(2).toULongLong(&idOk, 16);
    const bool extended = m_canExtended->isChecked(); // 是否发送扩展帧。
    const bool remote = m_canRemote->isChecked(); // 是否发送远程请求帧。
    const int dlc = m_canDlc->value(); // 当前帧 DLC。
    if (!idOk || idValue > (extended ? 0x1FFFFFFULL : 0x7FFULL)) {
        setConnectionState(QStringLiteral("CAN ID 超出当前帧格式范围"), true);
        return;
    }

    QString compact = m_canData->text(); // 读取 CAN 数据 HEX 文本。
    compact.remove(QRegularExpression(QStringLiteral("\\s")));
    QByteArray data; // 解码后的 CAN 数据。
    if (!compact.isEmpty() && (compact.size() % 2 != 0 || !compact.contains(QRegularExpression(QStringLiteral("^[0-9A-Fa-f]+$"))))) {
        setConnectionState(QStringLiteral("CAN 数据必须是偶数个十六进制字符"), true);
        return;
    }
    data = QByteArray::fromHex(compact.toLatin1());
    if ((remote && !data.isEmpty()) || (!remote && data.size() != dlc)) {
        setConnectionState(remote ? QStringLiteral("RTR 帧的数据必须为空")
                                  : QStringLiteral("CAN 数据长度必须等于 DLC"),
                           true);
        return;
    }

    QCanBusFrame frame; // 构造待发送的经典 CAN 帧。
    frame.setFrameId(static_cast<quint32>(idValue));
    frame.setExtendedFrameFormat(extended);
    frame.setFrameType(remote ? QCanBusFrame::RemoteRequestFrame : QCanBusFrame::DataFrame);
    frame.setPayload(remote ? QByteArray(dlc, '\0') : data);
    if (!frame.isValid() || frame.hasFlexibleDataRateFormat() || !m_canDevice->writeFrame(frame)) {
        setConnectionState(QStringLiteral("CAN 帧发送失败：%1").arg(m_canDevice->errorString()), true);
        return;
    }
    displayCanFrame(QStringLiteral("发送"), m_streamEndpoint, frame);
    appendLog(QStringLiteral("已发送 CAN 帧：ID=%1，DLC=%2").arg(idText).arg(dlc));
}

void TestMainWindow::readUdpDatagrams()
{
    while (m_udpSocket.hasPendingDatagrams()) {
        const qint64 size = m_udpSocket.pendingDatagramSize(); // 当前数据报长度。
        QByteArray payload(static_cast<int>(qMax<qint64>(size, 0)), '\0'); // 当前数据报缓存。
        QHostAddress address; // UDP 来源地址。
        quint16 port = 0; // UDP 来源端口。
        const qint64 received = m_udpSocket.readDatagram(payload.data(), payload.size(), &address, &port);
        if (received < 0) {
            appendLog(QStringLiteral("UDP 接收失败：%1").arg(m_udpSocket.errorString()), true);
            continue;
        }
        payload.resize(static_cast<int>(received));
        displayFrame(QStringLiteral("接收"), QStringLiteral("%1:%2").arg(address.toString()).arg(port), payload);
    }
}

void TestMainWindow::acceptTcpConnection()
{
    QTcpSocket *socket = m_tcpServer.nextPendingConnection(); // 取出待处理 TCP 连接。
    if (socket == nullptr) {
        return;
    }
    if (m_tcpSocket != nullptr) {
        m_tcpSocket->disconnectFromHost();
        m_tcpSocket->deleteLater();
    }
    m_tcpSocket = socket;
    m_tcpSocket->setParent(this);
    m_decoder.reset();
    m_streamEndpoint = QStringLiteral("%1:%2").arg(socket->peerAddress().toString()).arg(socket->peerPort());
    connect(socket, &QTcpSocket::readyRead, this, &TestMainWindow::readTcpBytes);
    connect(socket, &QTcpSocket::disconnected, this, &TestMainWindow::onTcpDisconnected);
    connect(socket, static_cast<void (QAbstractSocket::*)(QAbstractSocket::SocketError)>(&QAbstractSocket::error),
            this, &TestMainWindow::onTcpError);
    setConnectionState(QStringLiteral("TCP 已连接：%1").arg(m_streamEndpoint));
    appendLog(QStringLiteral("收到 TCP 连接"));
}

void TestMainWindow::onTcpConnected()
{
    if (m_tcpSocket == nullptr) {
        return;
    }
    m_streamEndpoint = QStringLiteral("%1:%2").arg(m_tcpSocket->peerAddress().toString()).arg(m_tcpSocket->peerPort());
    setConnectionState(QStringLiteral("TCP 已连接：%1").arg(m_streamEndpoint));
    appendLog(QStringLiteral("TCP 连接建立"));
}

void TestMainWindow::onTcpDisconnected()
{
    m_idleTimer->stop();
    m_decoder.reset();
    setConnectionState(QStringLiteral("TCP 已断开"), true);
    appendLog(QStringLiteral("TCP 连接已断开"), true);
    if (m_tcpSocket != nullptr) {
        m_tcpSocket->deleteLater();
        m_tcpSocket = nullptr;
    }
}

void TestMainWindow::onTcpError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error)
    if (m_tcpSocket != nullptr) {
        appendLog(QStringLiteral("TCP 错误：%1").arg(m_tcpSocket->errorString()), true);
    }
}

void TestMainWindow::readTcpBytes()
{
    if (m_tcpSocket == nullptr) {
        return;
    }
    processStreamBytes(m_tcpSocket->readAll(), m_streamEndpoint);
}

void TestMainWindow::readSerialBytes()
{
    processStreamBytes(m_serialPortDevice.readAll(), m_streamEndpoint);
}

void TestMainWindow::onSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError || !m_serialPortDevice.isOpen()) {
        return;
    }
    appendLog(QStringLiteral("串口错误：%1").arg(m_serialPortDevice.errorString()), true);
}

void TestMainWindow::readCanFrames()
{
    if (m_canDevice == nullptr) {
        return;
    }
    while (m_canDevice->framesAvailable() > 0) {
        const QCanBusFrame frame = m_canDevice->readFrame(); // 读取一条 CAN 总线帧。
        if (frame.frameType() == QCanBusFrame::ErrorFrame) {
            appendLog(QStringLiteral("收到 CAN 错误帧：%1").arg(m_canDevice->interpretErrorFrame(frame)), true);
            continue;
        }
        if (!frame.isValid() || frame.hasFlexibleDataRateFormat() || frame.payload().size() > 8) {
            appendLog(QStringLiteral("收到非法或不支持的 CAN FD 帧"), true);
            continue;
        }
        displayCanFrame(QStringLiteral("接收"), m_streamEndpoint, frame);
    }
}

void TestMainWindow::onCanError(QCanBusDevice::CanBusError error)
{
    Q_UNUSED(error)
    if (m_canDevice != nullptr) {
        appendLog(QStringLiteral("CAN 错误：%1").arg(m_canDevice->errorString()), true);
    }
}

void TestMainWindow::onCanStateChanged(QCanBusDevice::CanBusDeviceState state)
{
    if (state == QCanBusDevice::ConnectedState) {
        setConnectionState(QStringLiteral("CAN 已连接：%1").arg(m_streamEndpoint));
    } else if (state == QCanBusDevice::UnconnectedState && m_transportOpen) {
        setConnectionState(QStringLiteral("CAN 已断开"), true);
    }
}

void TestMainWindow::processStreamBytes(const QByteArray &data, const QString &endpoint)
{
    const TesterFrameSettings settings = frameSettings(); // 读取当前页面的分帧模式。
    if (settings.kind == TestFrameKind::IdleTimeout) {
        restartIdleTimer();
    }
    QVector<QByteArray> frames; // 当前字节块解析出的完整帧。
    QString error; // 分帧失败说明。
    if (!m_decoder.feed(data, &frames, &error)) {
        appendLog(QStringLiteral("%1 分帧失败：%2").arg(endpoint, error), true);
        m_idleTimer->stop();
        return;
    }
    for (const QByteArray &frame : frames) {
        displayFrame(QStringLiteral("接收"), endpoint, frame);
    }
}

void TestMainWindow::restartIdleTimer()
{
    m_idleTimer->start(frameSettings().idleMs);
}

void TestMainWindow::flushIdleFrame()
{
    if (m_decoder.hasBuffered()) {
        displayFrame(QStringLiteral("接收"), m_streamEndpoint, m_decoder.takeBuffered());
    }
}

void TestMainWindow::displayFrame(const QString &direction, const QString &endpoint, const QByteArray &payload)
{
    const QString hex = payload.toHex(' ').toUpper(); // 生成便于检查的 HEX 文本。
    const QString ascii = printableAscii(payload); // 生成可读 ASCII 文本。
    QJsonParseError jsonError; // JSON 解析错误详情。
    const QJsonDocument document = QJsonDocument::fromJson(payload, &jsonError);
    QString parsed; // 面向用户的解析结果。
    if (!document.isNull()) {
        parsed = QString::fromUtf8(document.toJson(QJsonDocument::Indented)).trimmed();
    } else {
        parsed = QStringLiteral("ASCII：%1").arg(ascii);
    }
    const QString now = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    const QString protocol = m_transportCombo->currentText().section(QLatin1Char(' '), 0, 0);
    const int row = m_frameTable->rowCount();
    m_frameTable->insertRow(row);
    const QStringList values = {now, direction, protocol, endpoint, QString::number(payload.size()), hex, parsed};
    for (int column = 0; column < values.size(); ++column) {
        auto *item = new QTableWidgetItem(values.at(column));
        item->setToolTip(values.at(column));
        m_frameTable->setItem(row, column, item);
    }
    m_frameTable->scrollToBottom();

    m_detailView->appendPlainText(QStringLiteral("[%1] %2 %3 %4，长度=%5，分帧=%6")
                                      .arg(now, direction, protocol, endpoint)
                                      .arg(payload.size())
                                      .arg(protocol == QStringLiteral("UDP") ? QStringLiteral("数据报") : frameKindName(frameSettings().kind)));
    m_detailView->appendPlainText(QStringLiteral("HEX：%1").arg(hex));
    m_detailView->appendPlainText(QStringLiteral("ASCII：%1").arg(ascii));
    if (!document.isNull()) {
        m_detailView->appendPlainText(QStringLiteral("JSON：\n%1").arg(QString::fromUtf8(document.toJson(QJsonDocument::Indented)).trimmed()));
    }
    m_detailView->appendPlainText(QStringLiteral("----------------------------------------"));
}

void TestMainWindow::displayCanFrame(const QString &direction, const QString &endpoint, const QCanBusFrame &frame)
{
    const bool remote = frame.frameType() == QCanBusFrame::RemoteRequestFrame; // 判断是否为远程请求帧。
    const bool extended = frame.hasExtendedFrameFormat(); // 判断是否为扩展帧。
    const int dlc = frame.payload().size(); // Qt 使用 payload 长度表示 DLC。
    const QString id = QStringLiteral("0x%1").arg(frame.frameId(), extended ? 8 : 3, 16, QLatin1Char('0'));
    const QString hex = remote ? QString() : frame.payload().toHex(' ').toUpper();
    const QString frameType = QStringLiteral("%1，%2%3")
                                  .arg(extended ? QStringLiteral("扩展帧") : QStringLiteral("标准帧"),
                                       remote ? QStringLiteral("RTR") : QStringLiteral("数据帧"),
                                       remote ? QStringLiteral("，DLC=%1").arg(dlc) : QString());
    const QString now = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    const int row = m_frameTable->rowCount();
    m_frameTable->insertRow(row);
    const QStringList values = {now, direction, QStringLiteral("CAN"), endpoint, QString::number(dlc), hex,
                                QStringLiteral("ID=%1，%2").arg(id, frameType)};
    for (int column = 0; column < values.size(); ++column) {
        auto *item = new QTableWidgetItem(values.at(column));
        item->setToolTip(values.at(column));
        m_frameTable->setItem(row, column, item);
    }
    m_frameTable->scrollToBottom();
    m_detailView->appendPlainText(QStringLiteral("[%1] %2 CAN %3，ID=%4，%5，DLC=%6")
                                      .arg(now, direction, endpoint, id, frameType)
                                      .arg(dlc));
    m_detailView->appendPlainText(QStringLiteral("HEX：%1").arg(hex.isEmpty() ? QStringLiteral("（RTR 无数据）") : hex));
    m_detailView->appendPlainText(QStringLiteral("----------------------------------------"));
}

void TestMainWindow::clearView()
{
    m_frameTable->setRowCount(0);
    m_detailView->clear();
    appendLog(QStringLiteral("已清空显示"));
}

void TestMainWindow::refreshSerialPorts()
{
    const QString current = m_serialPort->currentText(); // 保存用户当前输入的串口名。
    m_serialPort->clear();
    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &port : ports) {
        m_serialPort->addItem(port.portName(), port.portName());
    }
    if (!current.isEmpty()) {
        m_serialPort->setCurrentText(current);
    }
    if (ports.isEmpty() && current.isEmpty()) {
        m_serialPort->setCurrentText(QStringLiteral("COM3"));
    }
    if (m_udpInterface != nullptr && m_udpInterface->count() == 1) {
        for (const QNetworkInterface &interfaceItem : QNetworkInterface::allInterfaces()) {
            if (interfaceItem.flags().testFlag(QNetworkInterface::IsUp)) {
                m_udpInterface->addItem(interfaceItem.humanReadableName() + QStringLiteral(" (") + interfaceItem.name() + QLatin1Char(')'), interfaceItem.name());
            }
        }
    }
}

void TestMainWindow::appendLog(const QString &message, bool error)
{
    const QString prefix = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    m_detailView->appendPlainText(QStringLiteral("[%1] %2%3").arg(prefix, error ? QStringLiteral("错误：") : QString(), message));
}

void TestMainWindow::setConnectionState(const QString &text, bool error)
{
    m_stateLabel->setText(text);
    m_stateLabel->setStyleSheet(error ? QStringLiteral("color:#c62828; font-weight:bold;")
                                      : QStringLiteral("color:#2e7d32; font-weight:bold;"));
}

} // namespace edgeagent
