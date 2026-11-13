#include "ParaContentWidget.h"
#include "ui_ParaContentWidget.h"
#include <QFile>
#include <QMessageBox>
#include <QString>
#include "FrameLogManager.h"
#include "DeviceListWidget.h"
#include <QSettings>
#include "../Comm/CommunicationIniUtils.h"
#include <QTextCodec>
#include <QTextStream>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QByteArray>
#include <QReadLocker>
#include <QWriteLocker>
#include <QDebug>
#include <utility>
#include <QMouseEvent>
#include <QSet>
#include <QTimer>
#include <QDateTime>
#include <QPointer>
#include <QMetaObject>
#include <QLineEdit>
#include <QUrl>
#include <QAbstractItemView>
#include <QShowEvent>

extern QString g_productNum;

namespace {
constexpr int kPullStreamReqToggle = 0;    // slotSendVideoSetInfo：拉流开关/地址下发
constexpr int kPullStreamReqIdentify = 1; // setIdentify：仅识别开关（失败时只回滚识别，不关拉流）
constexpr int kStreamSwitchSettleMs = 1500; // 等设备端切换完成后再刷新 UI，避免过早回写
constexpr int kMulticastDeviceTimeoutMs = 3000; // 某设备超过该时间未上报则视为离线/设备变化
constexpr int kMulticastDeviceWatchIntervalMs = 1000; // 设备活跃检测周期
const QString kCurrentStatusDeviceKey = QStringLiteral("__current_status_device__");

bool isIdleVideoSetSlot(const VideoSetData &d)
{
    return !d.bIsStreamPullOn
        && !d.bIsIdentifiy
        && !d.bisBigScreenShow
        && d.VideoIp.trimmed().isEmpty();
}

QString normalizedStreamPath(const QString &configuredValue, const QString &pathPrefix,
                             const QString &defaultName)
{
    QString value = configuredValue.trimmed();
    if (value.isEmpty())
        value = defaultName;

    const QUrl configuredUrl(value, QUrl::StrictMode);
    if (!configuredUrl.scheme().isEmpty() && !configuredUrl.path().isEmpty())
        value = configuredUrl.path();

    if (!value.startsWith(QLatin1Char('/')))
        value = pathPrefix + value;
    while (value.size() > 1 && value.endsWith(QLatin1Char('/')))
        value.chop(1);
    return value;
}

QString specialStreamDisplayName(const QString &deviceName, const QString &streamUrl,
                                 const QString &hdmiStreamPath,
                                 const QString &screenStreamPath,
                                 const QString &mosaicStreamPath)
{
    const QString name = deviceName.trimmed();
    if (name.isEmpty())
        return QString();

    QString path = QUrl(streamUrl.trimmed(), QUrl::StrictMode).path();
    while (path.size() > 1 && path.endsWith(QLatin1Char('/')))
        path.chop(1);

    if (path.compare(hdmiStreamPath, Qt::CaseInsensitive) == 0)
        return name + QString::fromUtf8(u8"-视频输入");
    if (path.compare(mosaicStreamPath, Qt::CaseInsensitive) == 0)
        return name + QString::fromUtf8(u8"-四宫格");
    if (path.compare(screenStreamPath, Qt::CaseInsensitive) == 0
            || path.startsWith(QStringLiteral("/screen/"), Qt::CaseInsensitive)) {
        // 投屏运行中修改桌面推流的推流名称时，已启动的流仍保留旧 /screen/... 地址。
        // 因此按实际 URL 的投屏路径识别，并用实际地址末段展示名称；新流启动后
        // 会自然切换为新名称。真实 URL 仍保存在下拉项 UserRole 中。
        const QString screenStreamName = path.section(QLatin1Char('/'), -1);
        return name + QString::fromUtf8(u8"-投屏") + screenStreamName;
    }
    return QString();
}

QString streamOrDeviceHost(const QString &value)
{
    const QString text = value.trimmed();
    if (text.isEmpty())
        return QString();

    QUrl parsed(text, QUrl::StrictMode);
    if (!parsed.host().isEmpty())
        return parsed.host().toLower();

    // 设备组播字段通常只有 IP，没有协议头。
    parsed = QUrl(QStringLiteral("device://") + text, QUrl::StrictMode);
    return parsed.host().toLower();
}

QString streamProtocolVariantKey(const QString &value)
{
    QUrl url(value.trimmed(), QUrl::StrictMode);
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || url.host().isEmpty()
            || (scheme != QStringLiteral("rtsp") && scheme != QStringLiteral("rtmp"))) {
        return value.trimmed();
    }

    // RTSP/RTMP 仅是同一路视频输入的不同传输方式。去掉协议和
    // 标准端口后生成稳定身份，用于防止下拉框同时显示两个同名别名。
    url.setScheme(QStringLiteral("stream"));
    url.setPort(-1);
    return url.toString(QUrl::FullyEncoded);
}
} // namespace

ParaContentWidget::ParaContentWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ParaContentWidget)
{
    ui->setupUi(this);
}

ParaContentWidget::~ParaContentWidget()
{
    shutdownHttpResources();
    delete m_deviceListWidget;
    m_deviceListWidget = nullptr;
    delete ui;
    ui = nullptr;
}

void ParaContentWidget::shutdownHttpResources()
{
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;

    if (m_pTimer)
        m_pTimer->stop();
    if (m_pMulticastDeviceWatchTimer)
        m_pMulticastDeviceWatchTimer->stop();

    for (VideoSetWidget *w : m_listVideoSetItem) {
        if (w)
            disconnect(w, nullptr, this, nullptr);
    }
    m_listVideoSetItem.clear();

    if (m_worker) {
        disconnect(m_worker, nullptr, this, nullptr);
        disconnect(this, nullptr, m_worker, nullptr);
    }

    if (m_worker && m_workerThread && m_workerThread->isRunning()) {
        QMetaObject::invokeMethod(m_worker, "prepareShutdown", Qt::BlockingQueuedConnection);
        m_workerThread->quit();
        if (!m_workerThread->wait(3000))
            m_workerThread->terminate();
        m_workerThread->wait();
    } else if (m_worker) {
        m_worker->prepareShutdown();
    }

    if (m_worker) {
        m_worker->moveToThread(QThread::currentThread());
        delete m_worker;
        m_worker = nullptr;
    }
}

bool compareTargetNum(VideoSetWidget* tgt1, VideoSetWidget* tgt2)
{
//    QString videoName1 = tgt1->getVideoSetData().VideoName;
//    QChar cName1 = videoName1.at(videoName1.length()-1);
//    QString videoName2 = tgt2->getVideoSetData().VideoName;
//    QChar cName2 = videoName2.at(videoName2.length()-1);
    int cName1 = tgt1->getVideoSetData().SmallScreenNum;
    int cName2 = tgt2->getVideoSetData().SmallScreenNum;
    return cName1 > cName2;
}

/**
 * @brief initData
 * 插件数据初始化交互
 * @param void
 * @return 无返回值
 */
void ParaContentWidget::initUi(IUiPluginBase *plugin)
{
    StreamUi::initializeToolTipFont();
    m_pPlugin = plugin;
    m_pPlugin->setStyle("day");

    initCfg();

    connect(ui->RTSP_pushButton,SIGNAL(clicked()),this,SLOT(slotRTSP_pushButton()));
    connect(ui->RTMP_pushButton,SIGNAL(clicked()),this,SLOT(slotRTMP_pushButton()));
    connect(ui->H264_pushButton,SIGNAL(clicked()),this,SLOT(slotH264_pushButton()));
    connect(ui->H265_pushButton,SIGNAL(clicked()),this,SLOT(slotH265_pushButton()));
    connect(ui->StreamSet_pushButton,SIGNAL(clicked(bool)),this,SLOT(slotStreamSet_pushButton(bool)));
    connect(ui->ClientPullUrl_pushButton,SIGNAL(clicked()),this,SLOT(slotClientPullUrl_pushButton()));
    connect(ui->AddItem_pushButton,SIGNAL(clicked()),this,SLOT(slotAddVideoSetItem()));
    connect(ui->Refresh_pushButton,SIGNAL(clicked()),this,SLOT(slotsendpullStreamList()));
    connect(ui->comboBox_product,SIGNAL(currentIndexChanged(int)),this,SLOT(slotRefreshDeviceNameList(int)));
    connect(ui->comboBox_product,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        QString fullName = ui->comboBox_product->itemData(index, Qt::ToolTipRole).toString();
        if (fullName.isEmpty())
            fullName = ui->comboBox_product->itemText(index);
        // 下拉框收起后，悬浮当前显示区域也能看到未省略的完整设备名称。
        ui->comboBox_product->setToolTip(fullName);
    });

    ui->ClientPullUrl_comboBox->setMaxVisibleItems(10);
    ui->ClientPullUrl_comboBox->setEditable(true);
    StreamUi::configureComboTextElision(ui->ClientPullUrl_comboBox);
    connect(ui->ClientPullUrl_comboBox, &QComboBox::currentTextChanged,
            this, [this](const QString &) {
        StreamUi::updateCurrentStreamToolTip(ui->ClientPullUrl_comboBox);
        updateClientProtocolControls();
    });
    connect(ui->ClientPullUrl_comboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::activated),
            this, [this](int) {
        // 地址选择只改变候选流，不改变客户端已选择/已确认的拉流协议。
        updateClientProtocolControls();
    });
    StreamUi::configureComboTextElision(ui->comboBox_product);
    // 设备组播可能在下拉列表展开期间到达。监听 popup 视图关闭事件，
    // 将期间积累的列表变化延迟到用户完成本次选择后再应用。
    if (ui->comboBox_product->view())
        ui->comboBox_product->view()->installEventFilter(this);
    ui->ClientPullUrl_comboBox->lineEdit()->setAttribute(Qt::WA_InputMethodEnabled, false);
    StreamUi::bindEditableStreamCombo(ui->ClientPullUrl_comboBox);
    StreamUi::bindExternalStreamToolTip(ui->ClientPullUrl_comboBox);
    connect(ui->ClientPullUrl_comboBox->lineEdit(), &QLineEdit::editingFinished,
            this, [this]() {
        if (!m_clientPullRefreshPending || m_shuttingDown)
            return;
        const StreamUi::StreamItemList pendingList = m_pendingClientPullUrls;
        m_clientPullRefreshPending = false;
        m_pendingClientPullUrls.clear();
        refreshClientPullUrlCombo(pendingList);
    });
    // 与原地址输入框一致：禁止输入中文及全角字符，同时允许手工录入 URL。
    QRegularExpression clientPullUrlRx(
        R"([^\x{3000}-\x{303F}\x{3400}-\x{4DBF}\x{4E00}-\x{9FFF}\x{F900}-\x{FAFF}\x{FE30}-\x{FE4F}\x{FF00}-\x{FFEF}]+)");
    QRegularExpressionValidator *clientPullUrlValidator =
        new QRegularExpressionValidator(clientPullUrlRx, ui->ClientPullUrl_comboBox);
    ui->ClientPullUrl_comboBox->setValidator(clientPullUrlValidator);

    ui->scrollArea->setStyleSheet("QScrollArea {background-color:transparent; border:0px;}"
                                  "QScrollArea QWidget {background-color:transparent;}");

    ui->AddItem_pushButton->setToolTip(QString::fromUtf8(u8"添加"));

    ui->comboBox_product->installEventFilter(this);
    // 展开客户端拉流地址列表前，用最新设备缓存重建一次别名和候选项；同时
    // 主动请求当前设备的流列表，响应返回后还会走统一刷新链路。
    ui->ClientPullUrl_comboBox->installEventFilter(this);

    ui->Refresh_pushButton->hide();
    ui->AddItem_pushButton->hide();
    ui->H264_pushButton->setChecked(true);
    updateClientProtocolControls();
//    ui->StreamProtocol_widget->hide();
    ui->EncodingFormat_widget->hide();

    m_deviceListWidget = new DeviceListWidget();
    m_deviceListWidget->hide();
    connect(m_deviceListWidget, &DeviceListWidget::signalDeviceIpSelected,this,[=](const QString &deviceIp){
        if(m_currentDeviceIP!=deviceIp){
            clearAllVideoSetItem();
            initDefaultVideoSetItems();
            m_currentDeviceIP=deviceIp;
            if (m_pMulticastClient)
                m_pMulticastClient->setCommunicationAddress(streamOrDeviceHost(deviceIp));
        }
        // signalcurrentDeviceIp 会统一触发通信地址切换；切换完成后强制拉取该设备视频输入流。
        emit signalcurrentDeviceIp(deviceIp);
        slotsendpullStreamList();
    });

//    loadQssStyle();

}

/**
 * @brief initCfg
 * 读取配置文件获取发送时的基础UI
 * @param void
 * @return 无返回值
 */
void ParaContentWidget::initCfg()
{
    const QString communicationIniPath = QApplication::applicationDirPath() + "/config/communication.ini";
    CommunicationIniUtils::restoreReadablePullStreamKeys(communicationIniPath);
    QSettings settings(communicationIniPath, QSettings::IniFormat);
    settings.setIniCodec(QTextCodec::codecForName("UTF-8"));
    settings.beginGroup("HTTP");
    m_strUrl = settings.value("url").toString();
    settings.endGroup();

    if(m_strUrl!=""){
        m_currentDeviceIP = streamOrDeviceHost(m_strUrl);
    }

    //组播IP 端口
    settings.beginGroup("Multicast");
    m_groupIp = settings.value("groupIp","224.0.0.100").toString();
    m_localPort = settings.value("groupPort","50000").toUInt();
    settings.endGroup();

    settings.beginGroup("PushStream");
    m_PushStreamName = normalizedStreamPath(
                settings.value("pushstreamName", QStringLiteral("/live/stream")).toString(),
                QStringLiteral("/live/"), QStringLiteral("stream"));
    settings.endGroup();

    m_localPullStreamItems.clear();
    m_localpushstreamData.clear();
    m_localStreamDisplayNameByUrl.clear();
    // 采用字节级自适应读取与解码，兼容 UTF-8、Windows XP 记事本默认保存的 ANSI (GBK)
    // 以及 QSettings 转义的百分号键名。
    QFile pullStreamFile(communicationIniPath);
    if (pullStreamFile.open(QIODevice::ReadOnly)) {
        bool inPullStreamGroup = false;
        while (!pullStreamFile.atEnd()) {
            const QByteArray lineBytes = pullStreamFile.readLine();
            const QByteArray trimmedLine = lineBytes.trimmed();
            if (trimmedLine.startsWith('[') && trimmedLine.endsWith(']')) {
                if (inPullStreamGroup)
                    break;
                inPullStreamGroup = trimmedLine.toLower() == QByteArrayLiteral("[pullstream]");
                continue;
            }
            if (!inPullStreamGroup || trimmedLine.isEmpty()
                    || trimmedLine.startsWith(';')
                    || trimmedLine.startsWith('#')) {
                continue;
            }

            const int separatorIndex = lineBytes.indexOf('=');
            if (separatorIndex <= 0)
                continue;

            const QByteArray rawKey = lineBytes.left(separatorIndex).trimmed();
            const QByteArray rawUrl = lineBytes.mid(separatorIndex + 1).trimmed();

            const QString displayName = CommunicationIniUtils::decodePullStreamKeyBytes(rawKey);
            const QString pullStreamUrl = QString::fromUtf8(rawUrl).trimmed();
            if (pullStreamUrl.isEmpty() || displayName.isEmpty())
                continue;
            // communication.ini 属于外部配置：无效地址不加入任何下拉列表。
            if (!StreamUi::isValidStreamUrl(pullStreamUrl))
                continue;

            StreamUi::StreamItem item(pullStreamUrl, displayName);
            // communication.ini 里面重复流地址不进行去重处理，每个配置项（名称与地址）均保留
            if (!m_localPullStreamItems.contains(item))
                m_localPullStreamItems.append(item);

            if (!m_localpushstreamData.contains(pullStreamUrl))
                m_localpushstreamData.append(pullStreamUrl);
            // 同一地址配置多次时保留第一个名称，避免显示名称随遍历反复变化。
            if (!m_localStreamDisplayNameByUrl.contains(pullStreamUrl))
                m_localStreamDisplayNameByUrl.insert(pullStreamUrl, displayName);
        }
        pullStreamFile.close();
    }

    QSettings settings1(QApplication::applicationDirPath() + "/config/VideoProcess.ini" , QSettings::IniFormat);
    settings1.beginGroup("Video");
    m_StreamPullAdress = settings1.value("inputUrl").toString();

    // 下方协议选择拥有独立状态，不能再由上方手动拉流地址实时反推。
    // 兼容旧配置：首次升级没有独立配置项时，仅在初始化时沿用原地址协议。
    m_clientProtocolSelection = settings1.value("clientPullProtocol").toString().trimmed().toLower();
    if (m_clientProtocolSelection != QStringLiteral("rtsp")
            && m_clientProtocolSelection != QStringLiteral("rtmp")) {
        const QString configuredScheme = QUrl(m_StreamPullAdress, QUrl::StrictMode).scheme().toLower();
        m_clientProtocolSelection = configuredScheme == QStringLiteral("rtmp")
                ? QStringLiteral("rtmp") : QStringLiteral("rtsp");
        settings1.setValue("clientPullProtocol", m_clientProtocolSelection);
    }
    settings1.endGroup();


    // ZLM 列表尚未到达时先显示上次成功设置的地址；收到列表后会重建为实时流列表。
    if (!m_StreamPullAdress.trimmed().isEmpty()) {
        StreamUi::addStreamItem(ui->ClientPullUrl_comboBox, m_StreamPullAdress);
        StreamUi::setCurrentUnlistedStreamUrl(ui->ClientPullUrl_comboBox, m_StreamPullAdress);
    }
}

/**
 * @brief initData
 * 插件数据初始化交互
 * @param void
 * @return 无返回值
 */
void ParaContentWidget::initData()
{
    qRegisterMetaType<VideoSetData>("VideoSetData");
    qRegisterMetaType<StreamUi::StreamItem>("StreamUi::StreamItem");
    qRegisterMetaType<StreamUi::StreamItemList>("StreamUi::StreamItemList");

    // 创建工作线程和工作对象（HttpWorker 不设 parent，避免跨线程析构）
    m_workerThread = new QThread(this);
    m_worker = new HttpWorker();

    // 将工作对象移到工作线程
    m_worker->moveToThread(m_workerThread);

    // 启动线程
    m_workerThread->start();

    // 连接信号槽
    connect(this, &ParaContentWidget::signalPosPullStreamPara, m_worker, &HttpWorker::doPospullStreamParaWork);
    connect(this, &ParaContentWidget::signalOCRSwitch, m_worker, &HttpWorker::doOCRSwitchWork);
    connect(this, &ParaContentWidget::signalRecognitionSwitch, m_worker, &HttpWorker::doRecognitionSwitchWork);
    connect(this, &ParaContentWidget::signalPosSwitchBigScreen, m_worker, &HttpWorker::doSwitchBigScreenWork);

    connect(m_worker, &HttpWorker::requestFinishedForPullStreamPara, this, &ParaContentWidget::slotFinishedForPullStreamPara);
    connect(m_worker, &HttpWorker::requestFinishedForOCRSwitch, this, &ParaContentWidget::slotFinishedForOCRSwitch);
    connect(m_worker, &HttpWorker::requestFinishedForRecognitionSwitch, this, &ParaContentWidget::slotFinishedForRecognitionSwitch);
    connect(m_worker, &HttpWorker::requestFinishedForSwitchBigScreenWork, this, &ParaContentWidget::slotFinishedForSwitchBigScreenWork);

    connect(this, &ParaContentWidget::signalPosPushStreamPara, m_worker, &HttpWorker::doPosPushStreamParaWork);
    connect(this, &ParaContentWidget::signalOcrPushStreamPara, m_worker, &HttpWorker::doOcrpushStreamParaWork);
    connect(this, &ParaContentWidget::signalpullStreamList, m_worker, &HttpWorker::doGetpullStreamList);
    connect(this, &ParaContentWidget::signalSendNetSetPara, m_worker, &HttpWorker::doNetWorkSet);
    connect(m_worker, &HttpWorker::requestFinishedForNetWorkSet, this, &ParaContentWidget::signalFinishedForNetWorkSet);
    connect(m_worker, &HttpWorker::requestFinishedForPushStreamPara, this, &ParaContentWidget::slotFinishedForPushStreamPara);
    connect(m_worker, &HttpWorker::requestFinishedForOcrPushStreamPara,this, &ParaContentWidget::signalFinishedForOcrPushStreamPara);
    connect(m_worker, &HttpWorker::requestFinishedForgetpullStreamList, this, &ParaContentWidget::slotFinishedForgetpullStreamList);
    connect(m_worker, &HttpWorker::requestFinishedForgetZLMpushList, this, &ParaContentWidget::slotFinishedForgetZLMpushList);

    enqueueWorkerSetSseClient(m_strUrl);

    //定时 查询正在拉流列表
    m_pTimer=new QTimer(this);
    m_pTimer->setInterval(3000);
    connect(m_pTimer,&QTimer::timeout,this,[=](){
        QString url = "http://" + m_strUrl + "/proxy/status/pullStreamList";
        emit signalpullStreamList(url);
    });
    m_pTimer->start();


    //UDP组播（ZLM推流列表）
    m_pMulticastClient = new MulticastClient(this);
    // IPv4 示例
    m_pMulticastClient->configure(m_groupIp, 50000, m_localPort, "", 3000, 5000,
                                  1000, 10000, streamOrDeviceHost(m_currentDeviceIP));
    // IPv6 示例（注意地址形如 ff15::xxxx）
    // m_pMulticastClient->configure("ff15::1234", 45454, 45454, "eth0", 3000, 10000, 1000, 15000);
    qRegisterMetaType<QHostAddress>("QHostAddress");
    connect(m_pMulticastClient, &MulticastClient::datagramReceived, this, &ParaContentWidget::slotUdpDataReceived);
    connect(m_pMulticastClient, &MulticastClient::stateChanged, this, [](const QString &st){
        qDebug() << "MulticastClient state:" << st;
    });
    m_pMulticastClient->start();
    // 发送
//    m_pMulticastClient->send("hello multicast");

    m_pMulticastDeviceWatchTimer = new QTimer(this);
    m_pMulticastDeviceWatchTimer->setInterval(kMulticastDeviceWatchIntervalMs);
    connect(m_pMulticastDeviceWatchTimer, &QTimer::timeout, this, &ParaContentWidget::checkMulticastDeviceChanges);
    m_pMulticastDeviceWatchTimer->start();

    initDefaultVideoSetItems();

}

/**
 * @brief slotRTSP_pushButton
 * 点击“RTSP”按钮让自身按钮高亮，其他按钮恢复常态
 * @param void
 * @return void
 */
void ParaContentWidget::slotRTSP_pushButton()
{
    // 这里只记录下方协议选项，不修改、保存或重新拉取上方地址。
    m_clientProtocolSelection = QStringLiteral("rtsp");
    updateClientProtocolControls();

//    QSettings settings1(QApplication::applicationDirPath() + "/config/VideoProcess.ini" , QSettings::IniFormat);
//    settings1.beginGroup("Video");
//    m_StreamPullAdress = settings1.value("inputUrl").toString();
//    settings1.endGroup();
//    if(!m_StreamPullAdress.isEmpty())
//    {
//        m_StreamPullAdress.replace("rtmp","rtsp");
//        m_StreamPullAdress.replace("1935","554");
//    }

}

/**
 * @brief slotRTMP_pushButton
 * 点击“RTMP”按钮让自身按钮高亮，其他按钮恢复常态
 * @param void
 * @return void
 */
void ParaContentWidget::slotRTMP_pushButton()
{
    // 这里只记录下方协议选项，不修改、保存或重新拉取上方地址。
    m_clientProtocolSelection = QStringLiteral("rtmp");
    updateClientProtocolControls();

//    QSettings settings1(QApplication::applicationDirPath() + "/config/VideoProcess.ini" , QSettings::IniFormat);
//    settings1.beginGroup("Video");
//    m_StreamPullAdress = settings1.value("inputUrl").toString();
//    settings1.endGroup();
//    if(!m_StreamPullAdress.isEmpty())
//    {
//        m_StreamPullAdress.replace("rtsp","rtmp");
//        m_StreamPullAdress.replace("554","1935");
//    }
}

/**
 * @brief slotH264_pushButton
 * 点击“H264”按钮让自身按钮高亮，其他按钮恢复常态
 * @param void
 * @return void
 */
void ParaContentWidget::slotH264_pushButton()
{
    ui->H264_pushButton->setChecked(true);
    ui->H265_pushButton->setChecked(false);
}

/**
 * @brief slotH265_pushButton
 * 点击“H265”按钮让自身按钮高亮，其他按钮恢复常态
 * @param void
 * @return void
 */
void ParaContentWidget::slotH265_pushButton()
{
    ui->H265_pushButton->setChecked(true);
    ui->H264_pushButton->setChecked(false);
}

/**
 * @brief slotStreamSet_pushButton
 * 客户端拉流设置
 * @param void
 * @return void
 */
void ParaContentWidget::slotStreamSet_pushButton(bool bFlag)
{
    Q_UNUSED(bFlag);
    if (!currentClientStreamIsValid()) {
        updateClientProtocolControls();
        return;
    }
   /*test
    Q_UNUSED(bFlag);
    // test zlm 推流列表
    // 构造 JSON
    QJsonObject obj;
    obj.insert(QStringLiteral("序列号"), QStringLiteral("721C54AD"));
    obj.insert(QStringLiteral("设备名称"), QStringLiteral("智图1"));
    obj.insert(QStringLiteral("设备编号"), QStringLiteral("ZTYL_110"));
    obj.insert(QStringLiteral("设备IP"), QStringLiteral("192.168.0.110"));
    QJsonArray streamList;
    streamList.append(QStringLiteral("rtsp://127.0.0.1:554/stream1"));
    streamList.append(QStringLiteral("rtmp://127.0.0.1:1935/stream2"));
    obj.insert(QStringLiteral("推流列表"), streamList);
    // 转成紧凑 JSON 字节流（适合网络发送）
    QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    // 发送
    m_pMulticastClient->send(payload);

    QJsonObject obj1;
    obj1.insert(QStringLiteral("序列号"), QStringLiteral("721C54AB"));
    obj1.insert(QStringLiteral("设备名称"), QStringLiteral("智图2"));
    obj1.insert(QStringLiteral("设备编号"), QStringLiteral("ZTYL_111"));
    obj1.insert(QStringLiteral("设备IP"), QStringLiteral("192.168.0.111"));
    QJsonArray streamList1;
//    streamList1.append(QStringLiteral("rtsp://127.0.0.1:554/stream3"));
//    streamList1.append(QStringLiteral("rtmp://127.0.0.1:1935/stream4"));
    obj1.insert(QStringLiteral("推流列表"), streamList1);
    // 转成紧凑 JSON 字节流（适合网络发送）
    QByteArray payload1 = QJsonDocument(obj1).toJson(QJsonDocument::Compact);
    m_pMulticastClient->send(payload1);

    QJsonObject obj2;
    obj2.insert(QStringLiteral("序列号"), QStringLiteral("721C64AB"));
    obj2.insert(QStringLiteral("设备名称"), QStringLiteral("智图3"));
    obj2.insert(QStringLiteral("设备编号"), QStringLiteral("ZTYL_114"));
    obj2.insert(QStringLiteral("设备IP"), QStringLiteral("192.168.0.114"));
    QJsonArray streamList2;
    streamList2.append(QStringLiteral("rtsp://127.0.0.1:554/stream5"));
    streamList2.append(QStringLiteral("rtmp://127.0.0.1:1935/stream6"));
    obj2.insert(QStringLiteral("推流列表"), streamList2);
    // 转成紧凑 JSON 字节流（适合网络发送）
    QByteArray payload2 = QJsonDocument(obj2).toJson(QJsonDocument::Compact);
    m_pMulticastClient->send(payload2);

    QJsonObject obj3;
    obj3.insert(QStringLiteral("序列号"), QStringLiteral("789C64AB"));
    obj3.insert(QStringLiteral("设备名称"), QStringLiteral("智图4"));
    obj3.insert(QStringLiteral("设备编号"), QStringLiteral("ZTYL_115"));
    obj3.insert(QStringLiteral("设备IP"), QStringLiteral("192.168.0.115"));
    QJsonArray streamList3;
    streamList3.append(QStringLiteral("rtsp://127.0.0.1:554/stream7"));
    streamList3.append(QStringLiteral("rtmp://127.0.0.1:1935/stream8"));
    obj3.insert(QStringLiteral("推流列表"), streamList3);
    // 转成紧凑 JSON 字节流（适合网络发送）
    QByteArray payload3 = QJsonDocument(obj3).toJson(QJsonDocument::Compact);
    m_pMulticastClient->send(payload3);

    QJsonObject obj4;
    obj4.insert(QStringLiteral("序列号"), QStringLiteral("999C64AB"));
    obj4.insert(QStringLiteral("设备名称"), QStringLiteral("智图5"));
    obj4.insert(QStringLiteral("设备编号"), QStringLiteral("ZTYL_116"));
    obj4.insert(QStringLiteral("设备IP"), QStringLiteral("192.168.0.116"));
    QJsonArray streamList4;
    streamList4.append(QStringLiteral("rtsp://127.0.0.1:554/stream7"));
    streamList4.append(QStringLiteral("rtmp://127.0.0.1:1935/stream8"));
    obj4.insert(QStringLiteral("推流列表"), streamList4);
    // 转成紧凑 JSON 字节流（适合网络发送）
    QByteArray payload4 = QJsonDocument(obj4).toJson(QJsonDocument::Compact);
    m_pMulticastClient->send(payload4);*/

    // 只有点击本按钮时，下方协议才作用于上方当前显示/输入的地址。
    const QString sourceUrl = StreamUi::currentStreamUrl(ui->ClientPullUrl_comboBox).trimmed();
    const QString targetUrl = clientStreamUrlForProtocol(m_clientProtocolSelection, sourceUrl);
    if (targetUrl.isEmpty())
        return;

    // 协议切换只替换 scheme 和标准端口，保留所选别名流的主机、路径和参数。
    // 别名只用于显示，不是协议切换的前置条件。设备自动生成的
    // /live/stream 可能尚未出现在组播流列表中，此时仍应允许切换协议。
    // 不要把当前自动别名复制进 m_localStreamDisplayNameByUrl：该容器只保存
    // communication.ini 的固定配置名称，否则设备改名后旧名称会一直覆盖新别名。
    // 协议转换后的地址只是客户端当前拉流值，不代表该协议流已经存在。
    // 不写入 m_localpushstreamData，否则它会被后续刷新当成真实候选流。
    m_StreamPullAdress = targetUrl;
    QSettings settings1(QApplication::applicationDirPath() + "/config/VideoProcess.ini" , QSettings::IniFormat);
    settings1.beginGroup("Video");
    settings1.setValue("inputUrl",m_StreamPullAdress);
    settings1.setValue("clientPullProtocol", m_clientProtocolSelection);
    settings1.endGroup();
    // 协议切换后地址的 scheme/端口会变化，先按新地址重建别名映射，
    // 再通过统一接口选中，避免把真实 URL 直接显示到客户端拉流地址框。
    selectPushStreamListBySerialNumber(
                ui->comboBox_product->currentData(Qt::UserRole).toString());
    StreamUi::setCurrentUnlistedStreamUrl(ui->ClientPullUrl_comboBox, m_StreamPullAdress);
    updateClientProtocolControls();
    // 先将配置和界面稳定到同一新协议，再通知视频插件，避免同步槽执行期间
    // 的列表刷新使按钮短暂回到旧协议。
    emit signalStreamPullAdress(m_StreamPullAdress);

    /* QJsonObject json;
    if(ui->RTSP_pushButton->isChecked())
    {
        json[QString::fromUtf8(u8"推流协议")] = "RTSP";
        QString strIp = ui->ClientPullUrl_comboBox->currentText();
        json[QString::fromUtf8(u8"推流地址")] = QString::fromLocal8Bit("rtsp://")+strIp+QString::fromLocal8Bit(":554")+m_PushStreamName;
    }

    if(ui->RTMP_pushButton->isChecked())
    {
        json[QString::fromUtf8(u8"推流协议")] = "RTMP";
        QString strIp = ui->ClientPullUrl_comboBox->currentText();
        json[QString::fromUtf8(u8"推流地址")] = QString::fromLocal8Bit("rtmp://")+strIp+QString::fromLocal8Bit(":1935")+m_PushStreamName;
    }

    if(ui->H264_pushButton->isChecked())
    {
        json[QString::fromUtf8(u8"编码格式")] = "H264";
    }
    if(ui->H265_pushButton->isChecked())
    {
        json[QString::fromUtf8(u8"编码格式")] = "H265";
    }

    json[QString::fromUtf8(u8"推流开关")] = true;

    QString url = "http://" + m_strUrl + "/proxy/setting/pushStream";
    emit signalPosPushStreamPara(url,json);*/
}

bool ParaContentWidget::currentClientStreamIsValid() const
{
    const QString actual = StreamUi::currentStreamUrl(ui->ClientPullUrl_comboBox).trimmed();
    return StreamUi::isValidStreamUrl(actual);
}

QString ParaContentWidget::clientStreamUrlForProtocol(const QString &protocol,
                                                      const QString &selectedSourceUrl) const
{
    QString sourceUrl = selectedSourceUrl.trimmed();
    if (!StreamUi::isValidStreamUrl(sourceUrl))
        sourceUrl = m_StreamPullAdress.trimmed();
    if (!StreamUi::isValidStreamUrl(sourceUrl))
        sourceUrl = StreamUi::currentStreamUrl(ui->ClientPullUrl_comboBox).trimmed();
    QUrl url(sourceUrl, QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty())
        return QString();

    const bool useRtsp = protocol.compare(QStringLiteral("RTSP"), Qt::CaseInsensitive) == 0;
    const QString sourceScheme = url.scheme().toLower();
    const QString targetScheme = useRtsp ? QStringLiteral("rtsp") : QStringLiteral("rtmp");
    const int targetPort = useRtsp ? 554 : 1935;

    if (sourceScheme == QStringLiteral("rtsp") || sourceScheme == QStringLiteral("rtmp")) {
        // RTSP/RTMP 之间只切换协议和标准端口，保留流路径及参数。
        url.setScheme(targetScheme);
        url.setPort(targetPort);
        return url.toString(QUrl::FullyEncoded);
    }

    // SRT 把实际流路径放在 streamid 的 r=... 中。选择 SRT 候选项时，
    // 提取同一路流的路径，再按客户端已选 RTSP/RTMP 生成可拉取地址。
    QString streamPath = url.path();
    if (streamPath.isEmpty()) {
        const QString decodedSource = QUrl::fromPercentEncoding(sourceUrl.toUtf8());
        int pathStart = decodedSource.indexOf(QStringLiteral("r="), 0, Qt::CaseInsensitive);
        if (pathStart >= 0) {
            pathStart += 2;
            int pathEnd = decodedSource.size();
            const QString terminators = QStringLiteral(",&#");
            for (const QChar terminator : terminators) {
                const int candidateEnd = decodedSource.indexOf(terminator, pathStart);
                if (candidateEnd >= 0 && candidateEnd < pathEnd)
                    pathEnd = candidateEnd;
            }
            streamPath = decodedSource.mid(pathStart, pathEnd - pathStart).trimmed();
        }
    }
    if (streamPath.isEmpty())
        return QString();
    if (!streamPath.startsWith(QLatin1Char('/')))
        streamPath.prepend(QLatin1Char('/'));

    QUrl targetUrl;
    targetUrl.setScheme(targetScheme);
    targetUrl.setHost(url.host());
    targetUrl.setPort(targetPort);
    targetUrl.setPath(streamPath);
    return targetUrl.toString(QUrl::FullyEncoded);
}

void ParaContentWidget::updateClientProtocolControls()
{
    // 协议单选始终可操作，且不从上方地址反推；只有协议确认需要有效地址。
    const bool addressValid = currentClientStreamIsValid();
    ui->RTSP_pushButton->setEnabled(true);
    ui->RTMP_pushButton->setEnabled(true);
    ui->StreamSet_pushButton->setEnabled(addressValid);

    const bool useRtmp = m_clientProtocolSelection == QStringLiteral("rtmp");
    ui->RTMP_pushButton->setChecked(useRtmp);
    ui->RTSP_pushButton->setChecked(!useRtmp);

    const QString normalStyle = QStringLiteral("font:18px Alibaba PuHuiTi 2.0;color:#b8c5d5;");
    const QString selectedStyle = QStringLiteral("font:18px Alibaba PuHuiTi 2.0;color:white;");
    ui->RTSP_label->setStyleSheet(ui->RTSP_pushButton->isChecked() ? selectedStyle : normalStyle);
    ui->RTMP_label->setStyleSheet(ui->RTMP_pushButton->isChecked() ? selectedStyle : normalStyle);
}

/**
 * @brief slotClientPullUrl_pushButton 客户端拉流地址设置
 */
void ParaContentWidget::slotClientPullUrl_pushButton()
{
    const QString selectedAddress = StreamUi::currentStreamUrl(ui->ClientPullUrl_comboBox).trimmed();
    if (selectedAddress.isEmpty()) {
        emit signalMessageInfoText(QString::fromUtf8(u8"提示"),
                                   QString::fromUtf8(u8"当前没有可用的ZLM流地址！"),
                                   MessageType_Warn);
        return;
    }
    QString errorText;
    if (!StreamUi::isValidStreamUrl(selectedAddress, &errorText)) {
        emit signalMessageInfoText(QString::fromUtf8(u8"提示"), errorText, MessageType_Warn);
        return;
    }

    // 上方地址确认原样使用手动输入/下拉选中的完整 URL，不读取下方协议选项。
    const QString tempPullAdress = selectedAddress;

    // 当前展示别名由设备目录动态生成；不能在确认地址时把它固化为本地配置
    // 别名，否则服务端修改设备名称后仍会被旧名称覆盖。
    QSettings settings1(QApplication::applicationDirPath() + "/config/VideoProcess.ini" , QSettings::IniFormat);
    settings1.beginGroup("Video");
    settings1.setValue("inputUrl",tempPullAdress);
    settings1.endGroup();
    m_StreamPullAdress = tempPullAdress;
    selectPushStreamListBySerialNumber(
                ui->comboBox_product->currentData(Qt::UserRole).toString());
    StreamUi::setCurrentUnlistedStreamUrl(ui->ClientPullUrl_comboBox, m_StreamPullAdress);
    StreamUi::restoreElidedDisplay(ui->ClientPullUrl_comboBox);
    updateClientProtocolControls();
    emit signalStreamPullAdress(tempPullAdress);

    FrameLogManager::getInstance()->log(
        QStringLiteral("ParameterSetWidget"),
        QString::fromUtf8(u8"[客户端拉流设置] 地址确认原样拉流: selected=%1")
            .arg(selectedAddress),
        IFrameLogManager::DEBUG_TYPE);
}

/**
 * @brief slotSendVideoSetInfo
 * 拉流设置http协议下发
 * @param VideoSetData
 * @return void
 */
void ParaContentWidget::slotSendVideoSetInfo(VideoSetData dData,bool bFlag)
{
    QJsonObject json;
    json[QString::fromUtf8(u8"拉流地址")] = dData.VideoIp;
    if(bFlag)
    {
        json[QString::fromUtf8(u8"拉流开关")] = true;
    }
    else
    {
        json[QString::fromUtf8(u8"拉流开关")] = false;
    }
    json[QString::fromUtf8(u8"识别开关")] = dData.bIsIdentifiy;
    json[QString::fromUtf8(u8"视频名称")] = dData.VideoName;
    json[QString::fromUtf8(u8"视频位置")] = dData.SmallScreenNum;


    QString url = "http://" + m_strUrl + "/proxy/setting/pullStream";
    emit signalPosPullStreamPara(url, json, dData, kPullStreamReqToggle);

}

/**
 * @brief slotAddVideoSetItem
 * 添加视频Item子项
 * @param void
 * @return void
 */
void ParaContentWidget::slotAddVideoSetItem()
{
    addVideoSetZLMlistUi(m_pushstreamlist);
}

/**
 * @brief slotDeleteItemVideoSetItem
 * 点击视频Item子项中的“删除”按钮，触发对子项的删除
 * @param VideoSetData（需要删除的子项详情信息）
 * @return void
 */
void ParaContentWidget::slotDeleteItemVideoSetItem(VideoSetData dData)
{
    m_strDeleteItem = dData.SmallScreenNum;
    emit signalMessageInfoText(QString::fromUtf8(u8"删除视频Item"),
                               QString::fromUtf8(u8"是否删除此项拉流配置?"),
                               MessageType_Info);

}

/**
 * @brief slotIdentifyVideoSetItem
 * 点击视频Item子项中的“识别”按钮，触发对子项的识别功能
 * @param VideoSetData（需要识别的子项详情信息）
 * @return void
 */
void ParaContentWidget::slotIdentifyVideoSetItem(VideoSetData dData,bool bFlag)
{
    // 其它通道可能仍挂着 m_identifyAwaitingReply，快速切换时会导致多路按钮灰显/选中状态错乱
    for (VideoSetWidget *w : m_listVideoSetItem) {
        if (w && w->smallScreenNum() != dData.SmallScreenNum)
            w->notifyIdentifyHttpFinished();
    }

    for(int n = 0 ; n != m_listVideoSetItem.size(); ++n)
    {
        VideoSetWidget *item = m_listVideoSetItem.at(n);
        if(item->smallScreenNum()==dData.SmallScreenNum){
            // 与 SwitchBigScreen 一致：点击后仅禁用，不在此处 setChecked(true)；
            // 开启态由 slotFinishedForPullStreamPara / setVideoComboxInfo 按服务端结果刷新。
            if (!bFlag)
                item->setButtonChecked(false);
            setIdentify(dData,bFlag);
        }else{
            item->setButtonChecked(false);
        }
    }

    // 识别单路互斥：只更新当前槽位会导致 m_vVideoSetData 中多路 bIsIdentifiy 仍为 true，
    // HTTP 乱序返回时用旧 pendingContext 会再次点亮已关掉的按钮
    for (int index = 0; index < m_vVideoSetData.size(); ++index) {
        if (m_vVideoSetData[index].SmallScreenNum == dData.SmallScreenNum)
            m_vVideoSetData[index].bIsIdentifiy = bFlag;
        else if (bFlag)
            m_vVideoSetData[index].bIsIdentifiy = false;
    }
}

/**
 * @brief slotSwitchBigScreenSetItem
 * 点击视频Item子项中的“大屏切换”按钮，触发对子项的识别功能
 * @param VideoSetData（需要识别的子项详情信息）
 * @return void
 */
void ParaContentWidget::slotSwitchBigScreenSetItem(VideoSetData dData,bool bFlag)
{
    // 其它通道可能仍挂着 m_switchBigScreenAwaitingReply，快速切换时仅最后一次请求的窗口会收到 notify，导致多路按钮一直灰显
    for (VideoSetWidget *w : m_listVideoSetItem) {
        if (w && w->smallScreenNum() != dData.SmallScreenNum)
            w->notifySwitchBigScreenHttpFinished();
    }

    VideoSetData reqData = dData;
    reqData.bisBigScreenShow = bFlag;

    for (int n = 0; n != m_listVideoSetItem.size(); ++n) {
        VideoSetWidget *item = m_listVideoSetItem.at(n);
        if (!item)
            continue;
        if (item->smallScreenNum() == dData.SmallScreenNum) {
            // 与 Identify 一致：点击后仅禁用，不在此处 setChecked(true)；成功后再刷新
            if (!bFlag)
                item->setSwitchBigScreenpushButtonChecked(false);
            sendSwitchBigScreen(reqData, bFlag);
        } else {
            item->setSwitchBigScreenpushButtonChecked(false);
        }
    }

    // 大屏单路互斥，与 slotIdentifyVideoSetItem 同步内存态
    for (int index = 0; index < m_vVideoSetData.size(); ++index) {
        if (m_vVideoSetData[index].SmallScreenNum == dData.SmallScreenNum)
            m_vVideoSetData[index].bisBigScreenShow = bFlag;
        else if (bFlag)
            m_vVideoSetData[index].bisBigScreenShow = false;
    }
}

/**
 * @brief slotRefreshpushStreamList 刷新推流列表信息
 */
void ParaContentWidget::slotRefreshpushStreamList()
{
    selectPushStreamListBySerialNumber(ui->comboBox_product->currentData(Qt::UserRole).toString());
    emit signalUpdateReadListData(m_pushstreamlist);
}

void ParaContentWidget::refreshClientPullUrlCombo(const StreamUi::StreamItemList &streamList)
{
    // 仅在用户真正改地址时推迟刷新。打开设置页自动获得焦点不算编辑。
    if (StreamUi::isComboEditing(ui->ClientPullUrl_comboBox)) {
        m_pendingClientPullUrls = streamList;
        m_clientPullRefreshPending = true;
        return;
    }
    m_clientPullRefreshPending = false;
    m_pendingClientPullUrls.clear();

    StreamUi::StreamItemList effectiveList;
    effectiveList.reserve(streamList.size() + 1);
    for (const StreamUi::StreamItem &item : streamList) {
        const QString trimmed = item.url.trimmed();
        if (!trimmed.isEmpty() && !effectiveList.contains(item))
            effectiveList.append(item);
    }
    // 当前客户端地址可能是为尚未上报推流的设备强制生成的。
    // 输入框保留该当前值，但不追加到候选列表；下拉项只能来自真实上报流。
    const QString configuredClientUrl = m_StreamPullAdress.trimmed();

    // 同一 host/path/query 的 RTSP 和 RTMP 是同一路视频输入，下拉框只保留
    // 一项。对于不同名称项不合并；当前已设置地址优先，保证协议切换后保留新协议而非旧协议。
    StreamUi::StreamItemList protocolDeduplicatedList;
    QHash<QString, int> protocolVariantIndex;
    const QString configuredVariantKey = streamProtocolVariantKey(configuredClientUrl);
    for (const StreamUi::StreamItem &item : effectiveList) {
        const QString variantKey = item.displayName.trimmed() + QLatin1Char('|') + streamProtocolVariantKey(item.url);
        const int existingIndex = protocolVariantIndex.value(variantKey, -1);
        if (existingIndex < 0) {
            protocolVariantIndex.insert(variantKey, protocolDeduplicatedList.size());
            protocolDeduplicatedList.append(item);
        } else if (streamProtocolVariantKey(item.url) == configuredVariantKey && item.url == configuredClientUrl) {
            protocolDeduplicatedList[existingIndex] = item;
        }
    }
    effectiveList = protocolDeduplicatedList;

    StreamUi::StreamItemList currentList;
    currentList.reserve(ui->ClientPullUrl_comboBox->count());
    for (int i = 0; i < ui->ClientPullUrl_comboBox->count(); ++i) {
        QString actual = ui->ClientPullUrl_comboBox->itemData(i, Qt::UserRole).toString().trimmed();
        if (actual.isEmpty())
            actual = ui->ClientPullUrl_comboBox->itemText(i).trimmed();
        currentList.append(StreamUi::StreamItem(actual, ui->ClientPullUrl_comboBox->itemText(i).trimmed()));
    }

    QString fromCombo = StreamUi::currentStreamUrl(ui->ClientPullUrl_comboBox).trimmed();
    const QString fromComboText = ui->ClientPullUrl_comboBox->currentText().trimmed();
    if (!StreamUi::isValidStreamUrl(fromCombo)
            || fromCombo.contains(QChar(0x2026))
            || fromCombo.contains(QLatin1String("..."))) {
        fromCombo.clear();
    }
    auto applyClientPullSelection = [&]() {
        // 组播刷新只保留用户当前选中/输入的项。优先 (名称, 地址) 精确匹配
        int index = -1;
        if (!fromComboText.isEmpty() && !fromCombo.isEmpty()) {
            for (int i = 0; i < ui->ClientPullUrl_comboBox->count(); ++i) {
                if (ui->ClientPullUrl_comboBox->itemText(i).trimmed() == fromComboText
                        && ui->ClientPullUrl_comboBox->itemData(i, Qt::UserRole).toString().trimmed() == fromCombo) {
                    index = i;
                    break;
                }
            }
        }
        if (index < 0) {
            index = StreamUi::findStreamUrl(ui->ClientPullUrl_comboBox, fromCombo);
        }
        if (index >= 0) {
            ui->ClientPullUrl_comboBox->setCurrentIndex(index);
        } else if (StreamUi::isValidStreamUrl(fromCombo)) {
            StreamUi::setCurrentUnlistedStreamUrl(ui->ClientPullUrl_comboBox, fromCombo);
        } else if (ui->ClientPullUrl_comboBox->count() > 0
                   && ui->ClientPullUrl_comboBox->currentIndex() < 0) {
            ui->ClientPullUrl_comboBox->setCurrentIndex(0);
        }
    };

    if (currentList == effectiveList) {
        // 列表未变时不要改选中项，只把收起态重新套成别名/省略显示。
        StreamUi::restoreElidedDisplay(ui->ClientPullUrl_comboBox);
        updateClientProtocolControls();
        return;
    }

    ui->ClientPullUrl_comboBox->blockSignals(true);
    ui->ClientPullUrl_comboBox->clear();
    for (const StreamUi::StreamItem &item : effectiveList)
        StreamUi::addStreamItem(ui->ClientPullUrl_comboBox, item);
    applyClientPullSelection();
    ui->ClientPullUrl_comboBox->blockSignals(false);
    StreamUi::restoreElidedDisplay(ui->ClientPullUrl_comboBox);
    updateClientProtocolControls();
}

/**
 * @brief refreshProductCombo 重建设备名称下拉并选中指定项
 * @param selectSerialNumber 非空时选中该序列号对应的名称项；为空则保持重建前用户当前项
 */
void ParaContentWidget::refreshProductCombo(const QString &selectSerialNumber)
{
    const QString priorSerial = ui->comboBox_product->currentData(Qt::UserRole).toString();
    const QString desiredSerial = selectSerialNumber.isEmpty()
            ? priorSerial : selectSerialNumber.trimmed();

    // 文本只显示设备名称，UserRole 保留唯一序列号，使同名设备仍可独立筛选。
    QVector<QPair<QString, QString>> deviceItems;
    bool statusDeviceAlreadyPresent = false;
    {
        QReadLocker locker(&m_deviceDataLock);
        deviceItems.reserve(m_devicePushStreamDataBySN.size());
        for (auto it = m_devicePushStreamDataBySN.cbegin(); it != m_devicePushStreamDataBySN.cend(); ++it) {
            const DevicePushStreamData &data = it.value();
            const bool isStatusDevice = !m_statusDeviceName.trimmed().isEmpty()
                    && ((!m_statusDeviceId.isEmpty() && data.deviceId == m_statusDeviceId)
                        || streamOrDeviceHost(data.deviceIp)
                           == streamOrDeviceHost(m_currentDeviceIP));
            const QString name = isStatusDevice
                    ? m_statusDeviceName.trimmed() : data.deviceName.trimmed();
            if (!name.isEmpty())
                deviceItems.append(qMakePair(name, it.key()));
            if ((!m_statusDeviceId.isEmpty() && data.deviceId == m_statusDeviceId)
                    || streamOrDeviceHost(data.deviceIp) == streamOrDeviceHost(m_currentDeviceIP)) {
                statusDeviceAlreadyPresent = true;
            }
        }
    }
    if (!m_statusDeviceName.trimmed().isEmpty() && !statusDeviceAlreadyPresent)
        deviceItems.append(qMakePair(m_statusDeviceName.trimmed(), kCurrentStatusDeviceKey));
    std::sort(deviceItems.begin(), deviceItems.end(),
              [](const QPair<QString, QString> &left, const QPair<QString, QString> &right) {
        const int nameOrder = left.first.localeAwareCompare(right.first);
        return nameOrder == 0 ? left.second < right.second : nameOrder < 0;
    });

    const QString allText = QString::fromUtf8(u8"全部");
    const auto comboItemsMatch = [this, &deviceItems, &allText]() {
        if (ui->comboBox_product->count() != deviceItems.size() + 1)
            return false;
        if (!ui->comboBox_product->itemData(0, Qt::UserRole).toString().isEmpty()
                || ui->comboBox_product->itemText(0) != allText) {
            return false;
        }

        for (int i = 0; i < deviceItems.size(); ++i) {
            const int comboIndex = i + 1;
            const QPair<QString, QString> &item = deviceItems.at(i);
            if (ui->comboBox_product->itemData(comboIndex, Qt::UserRole).toString() != item.second
                    || ui->comboBox_product->itemData(comboIndex, Qt::ToolTipRole).toString() != item.first
                    || ui->comboBox_product->itemText(comboIndex)
                       != StreamUi::elidedComboText(ui->comboBox_product, item.first)) {
                return false;
            }
        }
        return true;
    };

    const bool itemsUnchanged = comboItemsMatch();
    const int desiredIndex = ui->comboBox_product->findData(
                desiredSerial, Qt::UserRole, Qt::MatchExactly);
    const QString effectiveDesiredSerial = desiredIndex >= 0 ? desiredSerial : QString();
    const bool selectionUnchanged = priorSerial == effectiveDesiredSerial;

    // QComboBox popup 展开时清空 model 会使当前 hover/index 失效，表现为失焦或无法点击。
    // 此时只记录一次待刷新，等 popup Hide 后再重建；若内容实际没有变化则完全不动 UI。
    if (ui->comboBox_product->view() && ui->comboBox_product->view()->isVisible()) {
        m_productComboRefreshPending = !itemsUnchanged || !selectionUnchanged;
        return;
    }

    m_productComboRefreshPending = false;
    if (itemsUnchanged) {
        const int index = desiredIndex >= 0 ? desiredIndex : 0;
        if (ui->comboBox_product->currentIndex() != index) {
            ui->comboBox_product->blockSignals(true);
            ui->comboBox_product->setCurrentIndex(index);
            ui->comboBox_product->blockSignals(false);
        }
        QString currentFullName = ui->comboBox_product->currentData(Qt::ToolTipRole).toString();
        if (currentFullName.isEmpty())
            currentFullName = ui->comboBox_product->currentText();
        ui->comboBox_product->setToolTip(currentFullName);
        return;
    }

    ui->comboBox_product->blockSignals(true);
    ui->comboBox_product->clear();
    ui->comboBox_product->addItem(allText, QString());
    for (const QPair<QString, QString> &item : deviceItems) {
        ui->comboBox_product->addItem(
                    StreamUi::elidedComboText(ui->comboBox_product, item.first), item.second);
        const int index = ui->comboBox_product->count() - 1;
        ui->comboBox_product->setItemData(index, item.first, Qt::ToolTipRole);
    }

    const int idx = ui->comboBox_product->findData(desiredSerial, Qt::UserRole, Qt::MatchExactly);
    if (idx >= 0) {
        ui->comboBox_product->setCurrentIndex(idx);
    } else {
        ui->comboBox_product->setCurrentIndex(0);
    }
    ui->comboBox_product->blockSignals(false);
    // 重建期间信号被屏蔽，需要主动同步一次当前项的完整名称提示。
    QString currentFullName = ui->comboBox_product->currentData(Qt::ToolTipRole).toString();
    if (currentFullName.isEmpty())
        currentFullName = ui->comboBox_product->currentText();
    ui->comboBox_product->setToolTip(currentFullName);
}

/**
 * @brief videoSetWidgetForScreen  根据窗口号返回VideoSetWidget界面类
 * @param smallScreenNum 窗口号
 * @return  VideoSetWidget
 */
VideoSetWidget *ParaContentWidget::videoSetWidgetForScreen(int smallScreenNum) const
{
    if (m_shuttingDown)
        return nullptr;

    for (VideoSetWidget *item : m_listVideoSetItem) {
        if (item && item->getVideoSetData().SmallScreenNum == smallScreenNum)
            return item;
    }
    return nullptr;
}

/** @brief syncVideoSetDataVectorEntry 更新m_vVideoSetData*/
void ParaContentWidget::syncVideoSetDataVectorEntry(const VideoSetData &d)
{
    for (int i = 0; i < m_vVideoSetData.size(); ++i) {
        if (m_vVideoSetData[i].SmallScreenNum == d.SmallScreenNum) {
            m_vVideoSetData[i] = d;
            return;
        }
    }
}

/**
 * @brief slotRefreshDeviceNameList 按选中的设备名称项刷新下方推流内容
 * @param index 下拉项索引
 */
void ParaContentWidget::slotRefreshDeviceNameList(int index)
{
    // 只做推流内容筛选，不切换当前连接设备。
    selectPushStreamListBySerialNumber(ui->comboBox_product->itemData(index, Qt::UserRole).toString());
    emit signalUpdateReadListData(m_pushstreamlist);
}


/**
 * @brief slotFinishedForPullStreamPara
 * 接收拉流设置http反馈（pendingContext 与该次请求绑定，支持多路并发）
 */
void ParaContentWidget::slotFinishedForPullStreamPara(QString strData, VideoSetData pendingContext, int reqKind)
{
    if (m_shuttingDown)
        return;

//    qDebug()<<"hhhhhhhttt11111111111"<<QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss")<<strData;
    FrameLogManager::getInstance()->log("ParameterSetWidget","slotFinishedForPullStreamPara:" + strData,IFrameLogManager::DEBUG_TYPE);

    VideoSetWidget *const target = videoSetWidgetForScreen(pendingContext.SmallScreenNum);

    auto finishIdentifyPending = [&]() {
        if (reqKind != kPullStreamReqIdentify) {
            return;
        }
        if (VideoSetWidget *w = videoSetWidgetForScreen(pendingContext.SmallScreenNum)) {
            w->notifyIdentifyHttpFinished();
        }
    };

    auto finishStreamTogglePending = [&]() {
        if (reqKind != kPullStreamReqToggle) {
            return;
        }
        if (VideoSetWidget *w = videoSetWidgetForScreen(pendingContext.SmallScreenNum)) {
            w->notifyStreamToggleHttpFinished();
        }
    };

    auto rollbackUi = [&](const QString &msg) {
        VideoSetData rolledBack = pendingContext;
        if (reqKind == kPullStreamReqIdentify) {
            rolledBack.bIsIdentifiy = !pendingContext.bIsIdentifiy;
        } else {
            rolledBack.bIsStreamPullOn = !pendingContext.bIsStreamPullOn;
        }
        if (target) {
            target->setVideoComboxInfo(rolledBack, m_pushstreamlist);
            syncVideoSetDataVectorEntry(target->getVideoSetData());
        } else {
            syncVideoSetDataVectorEntry(rolledBack);
        }
        if (!msg.isEmpty()) {
            emit signalMessageInfoText(QString::fromUtf8(u8"拉流参数设置"), msg, MessageType_Warn);
        }
    };

    const QJsonDocument root_Doc = QJsonDocument::fromJson(strData.toUtf8());
    if (!root_Doc.isObject()) {
        rollbackUi(QString::fromUtf8(u8"拉流参数设置失败!(响应无效或网络异常)"));
        finishIdentifyPending();
        finishStreamTogglePending();
        return;
    }

    const QJsonObject value_obj = root_Doc.object();
    if (value_obj.contains(QStringLiteral("error")) && value_obj.value(QStringLiteral("error")).isString()) {
        const QString err = value_obj.value(QStringLiteral("error")).toString();
        rollbackUi(QString::fromUtf8(u8"拉流参数设置失败!(%1)").arg(err));
        finishIdentifyPending();
        finishStreamTogglePending();
        return;
    }

    const int nCode = value_obj.value(QStringLiteral("code")).toInt();
    const QString mes = value_obj.value(QStringLiteral("message")).toString();

    if (nCode == 0) {
        const VideoSetData ctx = pendingContext;
        const int kind = reqKind;
        QPointer<ParaContentWidget> self(this);
//        QTimer::singleShot(kStreamSwitchSettleMs, this, [self, ctx, kind]() {
            if (!self || self->m_shuttingDown)
                return;
            if (VideoSetWidget *w = self->videoSetWidgetForScreen(ctx.SmallScreenNum)) {
                VideoSetData applyData = ctx;
                // 识别请求可能乱序返回：若用户已切到其它通道，忽略本响应里过期的识别开关意图
                if (kind == kPullStreamReqIdentify) {
                    for (const VideoSetData &vd : self->m_vVideoSetData) {
                        if (vd.SmallScreenNum == ctx.SmallScreenNum) {
                            if (vd.bIsIdentifiy != ctx.bIsIdentifiy)
                                applyData.bIsIdentifiy = vd.bIsIdentifiy;
                            break;
                        }
                    }
                } else if (kind == kPullStreamReqToggle && applyData.VideoIp.trimmed().isEmpty()) {
                    for (const VideoSetData &vd : self->m_vVideoSetData) {
                        if (vd.SmallScreenNum == ctx.SmallScreenNum && !vd.VideoIp.trimmed().isEmpty()) {
                            applyData.VideoIp = vd.VideoIp.trimmed();
                            break;
                        }
                    }
                }
                w->setVideoComboxInfo(applyData, self->m_pushstreamlist);
                self->syncVideoSetDataVectorEntry(w->getVideoSetData());
            }
            if (kind == kPullStreamReqIdentify) {
                if (VideoSetWidget *w = self->videoSetWidgetForScreen(ctx.SmallScreenNum))
                    w->notifyIdentifyHttpFinished();
            }
            if (kind == kPullStreamReqToggle) {
                if (VideoSetWidget *w = self->videoSetWidgetForScreen(ctx.SmallScreenNum))
                    w->notifyStreamToggleHttpFinished();
            }
//        });
        return;
    }

    rollbackUi(QString::fromUtf8(u8"拉流参数设置失败!(%1)").arg(mes));
    finishIdentifyPending();
    finishStreamTogglePending();
}

/**
 * @brief slotFinishedForOCRSwitch
 * 接收OCR识别http反馈
 * @param strData（反馈消息内容）
 * @return void
 */
void ParaContentWidget::slotFinishedForOCRSwitch(QString strData)
{
    FrameLogManager::getInstance()->log("ParameterSetWidget","slotFinishedForOCRSwitch:" + strData,IFrameLogManager::DEBUG_TYPE);
    QTextCodec *tc = QTextCodec::codecForName("UTF-8");
    Q_UNUSED(tc);
    QJsonDocument root_Doc = QJsonDocument::fromJson(strData.toUtf8().data());
    if(root_Doc.isObject())
    {
        QJsonObject value_obj=root_Doc.object();
        if(value_obj.contains("error")&&value_obj["error"].isString()){
            emit signalMessageInfoText(QString::fromUtf8(u8"OCR识别"),
                                       QString::fromUtf8(u8"OCR识别开启失败!"),
                                       MessageType_Warn);
        }else{
            int nCode = root_Doc.object().value("code").toInt();
            if(nCode == 0){
                emit signalMessageInfoText(QString::fromUtf8(u8"OCR识别"),
                                           QString::fromUtf8(u8"OCR识别开启成功!"),
                                           MessageType_Success);
            }else{
                emit signalMessageInfoText(QString::fromUtf8(u8"OCR识别"),
                                           QString::fromUtf8(u8"OCR识别开启失败!"),
                                           MessageType_Warn);
            }
        }
    }

}

/**
 * @brief slotFinishedForOCRSwitch
 * 接收目标识别http反馈
 * @param strData（反馈消息内容）
 * @return void
 */
void ParaContentWidget::slotFinishedForRecognitionSwitch(QString strData)
{
    QTextCodec *tc = QTextCodec::codecForName("UTF-8");
    Q_UNUSED(tc);
    QJsonDocument root_Doc = QJsonDocument::fromJson(strData.toUtf8().data());
    if(root_Doc.isObject())
    {
        QJsonObject value_obj=root_Doc.object();
        if(value_obj.contains("error")&&value_obj["error"].isString()){
            if(!m_bTagetIdentifyFlag){
//                emit signalMessageInfoText(QString::fromUtf8(u8"目标识别"),QString::fromUtf8(u8"目标识别开启失败!"),MessageType_Warn);
            }
            if(m_bTagetIdentifyFlag){
//                emit signalMessageInfoText(QString::fromUtf8(u8"目标识别"),QString::fromUtf8(u8"目标识别关闭失败!"),MessageType_Warn);
            }
        }else{
            int nCode = root_Doc.object().value("code").toInt();
            if(nCode == 0)
            {
                if(!m_bTagetIdentifyFlag)
                {
//                    emit signalMessageInfoText(QString::fromUtf8(u8"目标识别"),QString::fromUtf8(u8"目标识别开启成功!"),MessageType_Success);
                    emit signalIdentifyRespons(2,Respons_Open);
                    m_bTagetIdentifyFlag = true;
                }
                else if(m_bTagetIdentifyFlag)
                {
//                    emit signalMessageInfoText(QString::fromUtf8(u8"目标识别"),QString::fromUtf8(u8"目标识别关闭成功!"),MessageType_Success);
                    emit signalIdentifyRespons(2,Respons_Close);
                    m_bTagetIdentifyFlag = false;
                }
            }
            else
            {
                if(!m_bTagetIdentifyFlag){
//                    emit signalMessageInfoText(QString::fromUtf8(u8"目标识别"),QString::fromUtf8(u8"目标识别开启失败!"),MessageType_Warn);
                }

                if(m_bTagetIdentifyFlag){
//                    emit signalMessageInfoText(QString::fromUtf8(u8"目标识别"),QString::fromUtf8(u8"目标识别关闭失败!"),MessageType_Warn);
                }
            }
        }
    }
    slotsendpullStreamList();
}

/**
 * @brief slotFinishedForPushStreamPara
 * 接收推流设置http反馈
 * @param strData（反馈消息内容）
 * @return void
 */
void ParaContentWidget::slotFinishedForPushStreamPara(QString strData)
{
    FrameLogManager::getInstance()->log("ParameterSetWidget","slotFinishedForPushStreamPara:" + strData,IFrameLogManager::DEBUG_TYPE);
    QTextCodec *tc = QTextCodec::codecForName("UTF-8");
    Q_UNUSED(tc);
    QJsonDocument root_Doc = QJsonDocument::fromJson(strData.toUtf8().data());
    if(root_Doc.isObject())
    {
        QJsonObject value_obj=root_Doc.object();
        if(value_obj.contains("error")&&value_obj["error"].isString()){
            emit signalMessageInfoText(QString::fromUtf8(u8"推流参数设置"),
                                       QString::fromUtf8(u8"推流参数设置失败!"),
                                       MessageType_Warn);
        }else{
            int nCode = root_Doc.object().value("code").toInt();
            if(nCode == 0)
            {
                // 将推流地址IP存储到本地
                /*QSettings settings(QApplication::applicationDirPath() + "/config/communication.ini" , QSettings::IniFormat);
            settings.setIniCodec(QTextCodec::codecForName("GBK"));
            settings.beginGroup("PushStream");
            settings.setValue("IP",ui->ClientPullUrl_comboBox->currentText());
            settings.endGroup();*/
                emit signalMessageInfoText(QString::fromUtf8(u8"推流参数设置"),
                                           QString::fromUtf8(u8"推流参数设置成功!"),
                                           MessageType_Success);

            }
            else
            {
                emit signalMessageInfoText(QString::fromUtf8(u8"推流参数设置"),
                                           QString::fromUtf8(u8"推流参数设置失败!"),
                                           MessageType_Warn);
            }
        }
    }
}

/**
 * @brief slotFinishedForSwitchBigScreenWork
 * 接收大屏切换http反馈
 * @param strData（反馈消息内容）
 * @return void
 */
void ParaContentWidget::slotFinishedForSwitchBigScreenWork(QString strData, VideoSetData pendingContext)
{
    if (m_shuttingDown)
        return;

    FrameLogManager::getInstance()->log("ParameterSet","slotFinishedForSwitchBigScreenWork:" + strData,IFrameLogManager::DEBUG_TYPE);

    const bool intendedOn = pendingContext.bisBigScreenShow;

    auto finishPending = [&]() {
        if (VideoSetWidget *w = videoSetWidgetForScreen(pendingContext.SmallScreenNum))
            w->notifySwitchBigScreenHttpFinished();
    };

    auto applyBigScreenUiFromMemory = [&]() {
        for (VideoSetWidget *item : m_listVideoSetItem) {
            if (!item)
                continue;
            const int num = item->smallScreenNum();
            for (const VideoSetData &vd : m_vVideoSetData) {
                if (vd.SmallScreenNum == num) {
                    item->setSwitchBigScreenpushButtonChecked(vd.bisBigScreenShow);
                    break;
                }
            }
        }
    };

    auto rollbackBigScreenUi = [&](const QString &msg) {
        for (int index = 0; index < m_vVideoSetData.size(); ++index) {
            if (m_vVideoSetData[index].SmallScreenNum == pendingContext.SmallScreenNum)
                m_vVideoSetData[index].bisBigScreenShow = !intendedOn;
        }
        applyBigScreenUiFromMemory();
        slotsendpullStreamList();
        if (!msg.isEmpty()) {
            emit signalMessageInfoText(QString::fromUtf8(u8"拉流参数设置"), msg, MessageType_Warn);
        }
    };

    const QJsonDocument root_Doc = QJsonDocument::fromJson(strData.toUtf8());
    if (!root_Doc.isObject()) {
        rollbackBigScreenUi(QString::fromUtf8(u8"大屏切换设置失败!(响应无效或网络异常)"));
        finishPending();
        return;
    }

    const QJsonObject value_obj = root_Doc.object();
    if (value_obj.contains(QStringLiteral("error")) && value_obj.value(QStringLiteral("error")).isString()) {
        const QString err = value_obj.value(QStringLiteral("error")).toString();
        rollbackBigScreenUi(QString::fromUtf8(u8"大屏切换设置失败!(%1)").arg(err));
        finishPending();
        return;
    }

    if (!value_obj.contains(QStringLiteral("code"))) {
        rollbackBigScreenUi(QString::fromUtf8(u8"大屏切换设置失败!(响应无效或网络异常)"));
        finishPending();
        return;
    }

    const int nCode = value_obj.value(QStringLiteral("code")).toInt();
    const QString mes = value_obj.value(QStringLiteral("message")).toString();

    if (nCode == 0) {
        if (intendedOn) {
            for (const VideoSetData &vd : m_vVideoSetData) {
                if (vd.bisBigScreenShow && vd.SmallScreenNum != pendingContext.SmallScreenNum) {
                    finishPending();
                    return;
                }
            }
        }

        const VideoSetData ctx = pendingContext;
        const bool bigScreenOn = intendedOn;
        QPointer<ParaContentWidget> self(this);
//        QTimer::singleShot(kStreamSwitchSettleMs, this, [self, ctx, bigScreenOn]() {
            if (!self || self->m_shuttingDown)
                return;
            for (int index = 0; index < self->m_vVideoSetData.size(); ++index) {
                if (self->m_vVideoSetData[index].SmallScreenNum == ctx.SmallScreenNum) {
                    self->m_vVideoSetData[index].bisBigScreenShow = bigScreenOn;
                } else if (bigScreenOn) {
                    self->m_vVideoSetData[index].bisBigScreenShow = false;
                }
            }
            // 点击时已写入 m_vVideoSetData，拉流查询 needUiUpdate 可能为 false；与 Identify 的 setVideoComboxInfo 一样在此刷新按钮
            for (VideoSetWidget *item : self->m_listVideoSetItem) {
                if (!item)
                    continue;
                const int num = item->smallScreenNum();
                for (const VideoSetData &vd : self->m_vVideoSetData) {
                    if (vd.SmallScreenNum == num) {
                        item->setSwitchBigScreenpushButtonChecked(vd.bisBigScreenShow);
                        break;
                    }
                }
            }
            self->slotsendpullStreamList();
            emit self->signalMessageInfoText(QString::fromUtf8(u8"拉流参数设置"),
                                       QString::fromUtf8(u8"大屏切换设置成功!"),
                                       MessageType_Success);
            if (VideoSetWidget *w = self->videoSetWidgetForScreen(ctx.SmallScreenNum))
                w->notifySwitchBigScreenHttpFinished();
//        });
        return;
    }

    rollbackBigScreenUi(QString::fromUtf8(u8"大屏切换设置失败!(%1)").arg(mes));
    finishPending();
}


/**
 * @brief slotConfirmBtnClicked
 * 接收MessageBox确认消息
 * @param strData（消息主题）
 * @return void
 */
void ParaContentWidget::slotConfirmBtnClicked(QString strData)
{
    if(QString::fromUtf8(u8"删除视频Item") == strData)
    {
        for(int n = 0 ; n != m_vVideoSetData.size(); ++n)
        {
            if(m_vVideoSetData.at(n).SmallScreenNum==m_strDeleteItem){
                m_vVideoSetData.removeAt(n);
                m_nArrayFlag[m_strDeleteItem-1] = 0; // 将删除的Item对应的数组序号置为空闲
                break;
            }
        }

        for(int n = 0 ; n != m_listVideoSetItem.size(); ++n)
        {
            if(m_listVideoSetItem.at(n)->getVideoSetData().SmallScreenNum==m_strDeleteItem){
                delete m_listVideoSetItem.at(n);
                m_listVideoSetItem.removeAt(n);
                break;
            }
        }
        updateVideoUI();
    }
    else if(strData == QString::fromUtf8(u8"退出软件提示"))
    {
        // 关闭目标识别
        slotIdentify(2,2);
    }
}

/**
 * @brief slotIdentify
 * 接收菜单消息目标识别开关
 * @param nType（菜单ID） nStatus(识别开关 1 开 关2)
 * @return void
 */
void ParaContentWidget::slotIdentify(int nType, int nStatus)
{
    QJsonObject json;
    if(nType == 2)
    {
        if(nStatus == 1)
        {
            json[QString::fromLocal8Bit("recognition")] = true;
            m_bTagetIdentifyFlag=false;
        }
        else if(nStatus == 2)
        {
            json[QString::fromLocal8Bit("recognition")] = false;
            m_bTagetIdentifyFlag= true;
        }

        QString url = "http://" + m_strUrl + "/proxy/setting/recognitionSwitch";
        emit signalRecognitionSwitch(url,json);
    }
}

/**
 * @brief slotUpdateVideoSetDate 更新m_vVideoSetData数据
 * @param dData
 */
void ParaContentWidget::slotUpdateVideoSetDate(VideoSetData dData)
{
    for(int index = 0; index < m_vVideoSetData.size();++index)
    {
        if(m_vVideoSetData[index].SmallScreenNum == dData.SmallScreenNum)
        {
            m_vVideoSetData[index].bisBigScreenShow = dData.bisBigScreenShow;
            m_vVideoSetData[index].VideoName = dData.VideoName;
            m_vVideoSetData[index].VideoIp = dData.VideoIp;
            m_vVideoSetData[index].bIsIdentifiy = dData.bIsIdentifiy;
            m_vVideoSetData[index].bIsStreamPullOn = dData.bIsStreamPullOn;
            m_vVideoSetData[index].connectstatus = dData.connectstatus;
        }
    }

    m_currentVideoSetData=dData;
}

/**
 * @brief slotSwitchBigScreen 更新m_currentBigScreenData数据，并下发大屏切换指令
 * @param dData
 */
void ParaContentWidget::sendSwitchBigScreen(VideoSetData dData,bool status)
{
    m_currentBigScreenData=dData;
  /*  //JSON文件外层索引
    QJsonObject screenJson;
    screenJson.insert("screen",dData.SmallScreenNum);
    emit signalSwitchBigScreen(screenJson);*/

    QJsonObject json;

    if(status){//开
        json[QString::fromLocal8Bit("screen")] = dData.SmallScreenNum;
    }else{//关
        json[QString::fromLocal8Bit("screen")] = 0;
    }



    QString url = "http://" + m_strUrl + "/proxy/setting/screenNum";
    emit signalPosSwitchBigScreen(url, json, dData);
}


/**
 * @brief slotFinishedForgetpullStreamList
 * 接收获取设备端正在拉流列表查询结果
 * @param strData（反馈消息内容）
 * @return void
 */
void ParaContentWidget::slotFinishedForgetpullStreamList(QString strData)
{
    if (m_shuttingDown)
        return;

    FrameLogManager::getInstance()->log("ParameterSetWidget","slotFinishedForgetpullStreamList:" + strData,IFrameLogManager::DEBUG_TYPE);

    auto resetOnPullListFailureIfNeeded = [&]() {
        if (isDefaultVideoSetItemsLayout())
            return;
        clearAllVideoSetItem();
        initDefaultVideoSetItems();
    };

    const QJsonDocument root_Doc = QJsonDocument::fromJson(strData.toUtf8());
    if (!root_Doc.isObject()) {
        resetOnPullListFailureIfNeeded();
        return;
    }

    const QJsonObject obj = root_Doc.object();

    // 网络/超时等传输层失败（HttpWorker 上报 code:-1）
    const bool transportFailed =
        (obj.contains(QStringLiteral("code")) && obj.value(QStringLiteral("code")).toInt() != 0)
        || (obj.contains(QStringLiteral("error")) && obj.value(QStringLiteral("error")).isString());
    if (transportFailed) {
        resetOnPullListFailureIfNeeded();
        return;
    }

    if (!obj.contains(QString::fromLocal8Bit("data"))) {
        FrameLogManager::getInstance()->log("ParameterSetWidget",
            QString::fromUtf8(u8"slotFinishedForgetpullStreamList: 响应缺少 data"),
            IFrameLogManager::DEBUG_TYPE);
        return;
    }

    const QJsonValue value = obj.value(QString::fromLocal8Bit("data"));
    if (!value.isObject()) {
        FrameLogManager::getInstance()->log("ParameterSetWidget",
            QString::fromUtf8(u8"slotFinishedForgetpullStreamList: data 非对象"),
            IFrameLogManager::DEBUG_TYPE);
        return;
    }

    const QJsonObject value_obj = value.toObject();
    if (!value_obj.contains(QStringLiteral("list")) || !value_obj.value(QStringLiteral("list")).isArray()) {
        FrameLogManager::getInstance()->log("ParameterSetWidget",
            QString::fromUtf8(u8"slotFinishedForgetpullStreamList: 响应缺少 list 数组"),
            IFrameLogManager::DEBUG_TYPE);
        return;
    }

    const QJsonArray dataArray = value_obj.value(QStringLiteral("list")).toArray();
    if (dataArray.isEmpty()) {
// 去掉清空 最后一条结束拉流 HTTP 进行中：避免 setVideoSetListInfo 把界面又刷回「结束」态
//        resetOnPullListFailureIfNeeded();
        QVector<VideoSetData> temp_vVideoSetData;
        addVideoSetUi(temp_vVideoSetData);
        return;
    }

    QVector<VideoSetData> vVideoSetData;
    vVideoSetData.reserve(dataArray.size());
    for (const QJsonValue &dataValue : dataArray) {
        if (!dataValue.isObject()) {
            FrameLogManager::getInstance()->log("ParameterSetWidget",
                QString::fromUtf8(u8"slotFinishedForgetpullStreamList: list 中存在非对象元素"),
                IFrameLogManager::DEBUG_TYPE);
            continue;
        }
        const QJsonObject data_obj = dataValue.toObject();
        VideoSetData strData;

        if (data_obj.contains(QString::fromUtf8(u8"大屏显示"))) {
            strData.bisBigScreenShow = data_obj.value(QString::fromUtf8(u8"大屏显示")).toBool();
        }
        if (data_obj.contains(QString::fromUtf8(u8"小屏窗口号"))) {
            strData.SmallScreenNum = data_obj.value(QString::fromUtf8(u8"小屏窗口号")).toInt();
        }
        if (data_obj.contains(QString::fromUtf8(u8"拉流地址"))
            && data_obj.value(QString::fromUtf8(u8"拉流地址")).isString()) {
            strData.VideoIp = data_obj.value(QString::fromUtf8(u8"拉流地址")).toString();
        }
        if (data_obj.contains(QString::fromUtf8(u8"识别状态"))
            && data_obj.value(QString::fromUtf8(u8"识别状态")).isBool()) {
            strData.bIsIdentifiy = data_obj.value(QString::fromUtf8(u8"识别状态")).toBool();
        }
        if (data_obj.contains(QString::fromUtf8(u8"视频名称"))
            && data_obj.value(QString::fromUtf8(u8"视频名称")).isString()) {
            strData.VideoName = data_obj.value(QString::fromUtf8(u8"视频名称")).toString();
        }
        if (data_obj.contains(QString::fromUtf8(u8"拉流开关"))) {
            strData.bIsStreamPullOn = data_obj.value(QString::fromUtf8(u8"拉流开关")).toBool();
        }
        if (data_obj.contains(QString::fromUtf8(u8"连接状态"))) {
            strData.connectstatus = data_obj.value(QString::fromUtf8(u8"连接状态")).toString();
        }

        if (strData.SmallScreenNum > 0)
            vVideoSetData.append(strData);
    }

//    if (vVideoSetData.isEmpty()) {
//        resetOnPullListFailureIfNeeded();
//        return;
//    }

    addVideoSetUi(vVideoSetData);
}


/**
 * @brief slotFinishedForgetZLMpushList
 * 接收SSE推送的ZLM服务器推流列表
 * @param strData（反馈消息内容）
 * @return void
 */
void ParaContentWidget::slotFinishedForgetZLMpushList(QString strEvent,QString strData)
{
    Q_UNUSED(strEvent);
    FrameLogManager::getInstance()->log("ParameterSetWidget","slotFinishedForgetZLMpushList:" + strData,IFrameLogManager::DEBUG_TYPE);
    QTextCodec *tc = QTextCodec::codecForName("UTF-8");
    Q_UNUSED(tc);
    QJsonDocument root_Doc = QJsonDocument::fromJson(strData.toUtf8().data());
    QStringList pushstreamlist;
//    qDebug()<<"ParaContentWidget11111111111"<<strData;
    if(root_Doc.isObject())
    {
        QJsonObject obj=root_Doc.object();
        if(obj.contains(QString::fromUtf8(u8"推流列表")))
        {
            QJsonValue value=obj.value(QString::fromUtf8(u8"推流列表"));
            if(value.isArray()){
                QJsonArray array=value.toArray();
                for(const QJsonValue &item : array){
                    if(item.isString()){
                        pushstreamlist.append(item.toString());
                    }else{
                        FrameLogManager::getInstance()->log("ParameterSetWidget","slotFinishedForgetZLMpushList:Array contains non-string value!" ,IFrameLogManager::DEBUG_TYPE);
                    }
                }
            }
        }

        if(obj.contains(QString::fromUtf8(u8"推流状态")) && obj.contains(QString::fromUtf8(u8"错误原因")))
        {
            //定时上报
            int ZLMpushStatus = obj.value(QString::fromUtf8(u8"推流状态")).toInt();
            QString reason = obj.value(QString::fromUtf8(u8"错误原因")).toString();
            //将推流状态告知OCR插件
            emit signalZLMPushStatus(ZLMpushStatus,reason);
        }
        if(obj.contains(QString::fromUtf8(u8"推流状态")) && obj.contains(QString::fromUtf8(u8"错误原因"))&& (!obj.contains(QString::fromUtf8(u8"推流列表"))))
        {
            //实时上报
            int ZLMpushStatus = obj.value(QString::fromUtf8(u8"推流状态")).toInt();
            QString reason = obj.value(QString::fromUtf8(u8"错误原因")).toString();
            //将推流状态告知OCR插件
            emit signalZLMPushStatus(ZLMpushStatus,reason);
            return;
        }
    }

    if(pushstreamlist.size() == 0)
    {
        FrameLogManager::getInstance()->log("ParameterSetWidget",QString::fromUtf8(u8"获取ZLM服务器推流地址列表为空!") ,IFrameLogManager::DEBUG_TYPE);
    }

    m_curDevicepushstreamlist.clear();
    m_curDevicepushstreamlist << pushstreamlist;
//    Global::initialize();
//    Global::g_ZLMpushStreamList << pushstreamlist;

    // SSE 列表变化后重新汇总：顶部客户端地址取全部流，下面窗口取设备筛选结果。
    slotRefreshpushStreamList();

}

/**
 * @brief slotsendpullStreamList 发送获取拉流列表命令
 */
void ParaContentWidget::slotsendpullStreamList()
{
    QString url = "http://" + m_strUrl + "/proxy/status/pullStreamList";
    emit signalpullStreamList(url);
    //清空组播 设备列表数据
//    m_devicePushStreamDataBySN.clear();
//    m_serialNumberBydeviceId.clear();
    // setCurrentText 仅在下拉中已有完全一致项时才会切换索引；需先重建列表再用 setCurrentIndex
//    refreshProductCombo(g_productNum);
}

/** 在工作线程中调度 SSE 连接（避免主线程直接操作 HttpWorker） */
void ParaContentWidget::enqueueWorkerSetSseClient(const QString &host)
{
    if (!m_worker) {
        return;
    }
    QMetaObject::invokeMethod(
        m_worker, "setsseClient", Qt::QueuedConnection, Q_ARG(QString, host));
}

/** SSE 连接 abort */
void ParaContentWidget::enqueueWorkerSseLinkabort()
{
    if (!m_worker) {
        return;
    }
    QMetaObject::invokeMethod(m_worker, "SSELinkabort", Qt::QueuedConnection);
}

/**
 * @brief slotReconnectSSE SSE重连
 */
void ParaContentWidget::slotReconnectSSE(QString address)
{
    const QString previousAddress = m_strUrl;
    if (!address.isEmpty() && address != m_strUrl) {
        m_statusDeviceName.clear();
        m_statusDeviceId.clear();
    }
    if (address.isEmpty()) {
        enqueueWorkerSetSseClient(m_strUrl);
    } else {
        enqueueWorkerSseLinkabort();
        enqueueWorkerSetSseClient(address);
        m_strUrl = address;
    }
    m_deviceListWidget->setSelectedByDeviceId(QString(), false);

    if(m_strUrl!=""){
        const QString newDeviceIp = streamOrDeviceHost(m_strUrl);
        if (newDeviceIp != m_currentDeviceIP) {
            m_currentDeviceIP = newDeviceIp;
            if (m_pMulticastClient)
                m_pMulticastClient->setCommunicationAddress(newDeviceIp);
        }
    }

    // 通信地址变化时，不沿用当前播放流的路径，始终切到新设备的视频输入流。
    switchClientToDeviceVideoInput(m_strUrl);

    FrameLogManager::getInstance()->log(
        QStringLiteral("ParameterSetWidget"),
        QString::fromUtf8(u8"[客户端自动切流] 通信地址切换: previous=%1, requested=%2, active=%3, clientUrl=%4, comboUrl=%5")
            .arg(previousAddress, address, m_strUrl, m_StreamPullAdress,
                 StreamUi::currentStreamUrl(ui->ClientPullUrl_comboBox)),
        IFrameLogManager::DEBUG_TYPE);

    emit signalUpdateReadListData(m_pushstreamlist);
}

/**
 * @brief slotUdpDataReceived  接收UDP组播（ZLM推流列表信息）
 */
void ParaContentWidget::slotUdpDataReceived(const QByteArray &arr, QHostAddress ip, quint16 port)
{
    QJsonParseError parseError;
    const QJsonDocument rootDoc = QJsonDocument::fromJson(arr, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "slotUdpDataReceived parse failed:" << parseError.errorString();
        FrameLogManager::getInstance()->log(
            QStringLiteral("ParameterSetWidget"),
            QString::fromUtf8(u8"[设备列表诊断] 组播JSON解析失败: sender=%1:%2, bytes=%3, offset=%4, error=%5")
                .arg(ip.toString()).arg(port).arg(arr.size()).arg(parseError.offset)
                .arg(parseError.errorString()),
            IFrameLogManager::ERROR_TYPE);
        return;
    }

    // 支持两种载荷：
    // 1) 单条对象：{序列号, 设备名称, 设备编号, 设备IP, 推流列表}（以序列号为唯一 id）
    // 2) 批量数组：[{...}, {...}, ...]
    QVector<DevicePushStreamData> batch;
    if (rootDoc.isObject()) {
        batch.reserve(1);
        DevicePushStreamData data;
        if (parseDevicePushStreamData(rootDoc.object(), data)) {
            batch.push_back(std::move(data));
        }
    } else if (rootDoc.isArray()) {
        const QJsonArray array = rootDoc.array();
        batch.reserve(array.size());
        for (const QJsonValue &value : array) {
            if (!value.isObject()) {
                continue;
            }
            DevicePushStreamData data;
            if (parseDevicePushStreamData(value.toObject(), data)) {
                batch.push_back(std::move(data));
            }
        }
    }

    if (batch.isEmpty()) {
        qWarning() << "slotUdpDataReceived invalid protocol field.";
        FrameLogManager::getInstance()->log(
            QStringLiteral("ParameterSetWidget"),
            QString::fromUtf8(u8"[设备列表诊断] 组播数据没有有效设备: sender=%1:%2, bytes=%3, rootObject=%4, rootArray=%5")
                .arg(ip.toString()).arg(port).arg(arr.size())
                .arg(rootDoc.isObject()).arg(rootDoc.isArray()),
            IFrameLogManager::ERROR_TYPE);
        return;
    }

    recordMulticastDeviceActivity(batch);

    if (m_deviceListWidget) {
        QVector<DeviceListWidget::DeviceRowSnapshot> rows;
        rows.reserve(batch.size());
        for (const DevicePushStreamData &d : batch) {
            DeviceListWidget::DeviceRowSnapshot row;
            row.serialNumber = d.serialNumber;
            row.deviceId = d.deviceId;
            row.deviceName = d.deviceName;
            row.deviceIp = d.deviceIp;
            rows.append(row);
        }
        m_deviceListWidget->applyMulticastSnapshots(rows);
    }

    applyDevicePushStreamDataBatch(std::move(batch));
    refreshPushStreamListUiAfterDeviceChange();
    logDeviceListDiagnostics(
                QStringLiteral("multicast %1:%2").arg(ip.toString()).arg(port));

}

/** recordMulticastDeviceActivity 记录本帧组播中出现的设备（以序列号为唯一标识）*/
void ParaContentWidget::recordMulticastDeviceActivity(const QVector<DevicePushStreamData> &batch)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_lastAnyMulticastReceivedMs = now;

    for (const DevicePushStreamData &d : batch) {
        if (!d.serialNumber.isEmpty())
            m_multicastDeviceLastSeenMs.insert(d.serialNumber, now);
    }
}

/**
 * @brief checkMulticastDeviceChanges 定时检查：某设备超过 3s 未上报则移除该设备缓存
 */
void ParaContentWidget::checkMulticastDeviceChanges()
{
    if (m_lastAnyMulticastReceivedMs <= 0)
        return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QStringList staleSerials;
    {
        QReadLocker locker(&m_deviceDataLock);
        staleSerials.reserve(m_devicePushStreamDataBySN.size());
        for (auto it = m_devicePushStreamDataBySN.cbegin(); it != m_devicePushStreamDataBySN.cend(); ++it) {
            const QString &serial = it.key();
            const qint64 lastSeen = m_multicastDeviceLastSeenMs.value(serial, 0);
            if (lastSeen <= 0 || now - lastSeen >= kMulticastDeviceTimeoutMs)
                staleSerials.append(serial);
        }
    }

    if (staleSerials.isEmpty())
        return;

    bool anyRemoved = false;
    for (const QString &serial : staleSerials) {
        FrameLogManager::getInstance()->log(
            QStringLiteral("ParameterSetWidget"),
            QString::fromUtf8(u8"设备超过3s未上报组播(序列号=%1)，移除该设备推流缓存").arg(serial),
            IFrameLogManager::DEBUG_TYPE);
        if (removeDevicePushStreamEntry(serial))
            anyRemoved = true;
    }

    if (anyRemoved)
        refreshPushStreamListUiAfterDeviceChange();
}

/** removeDevicePushStreamEntry 按序列号移除单台设备的推流缓存 */
bool ParaContentWidget::removeDevicePushStreamEntry(const QString &serialNumber)
{
    const QString key = serialNumber.trimmed();
    if (key.isEmpty())
        return false;

    QString deviceId;
    bool removed = false;
    {
        QWriteLocker locker(&m_deviceDataLock);
        auto it = m_devicePushStreamDataBySN.find(key);
        if (it == m_devicePushStreamDataBySN.end())
            return false;

        deviceId = it.value().deviceId;
        m_devicePushStreamDataBySN.erase(it);
        if (!deviceId.isEmpty())
            m_serialNumberBydeviceId.remove(deviceId);
        removed = true;
    }

    m_multicastDeviceLastSeenMs.remove(key);

    if (m_deviceListWidget)
        m_deviceListWidget->removeDeviceBySerial(key);

    return removed;
}

/**
 * @brief refreshPushStreamListUiAfterDeviceChange 设备列表变化后刷新推流下拉
 */
void ParaContentWidget::refreshPushStreamListUiAfterDeviceChange()
{
    // 组播中的设备名称可能新增、修改或超时移除，先同步下拉再筛选。
    refreshProductCombo(QString());
    selectPushStreamListBySerialNumber(ui->comboBox_product->currentData(Qt::UserRole).toString());
    emit signalUpdateReadListData(m_pushstreamlist);
}

/**
 * @brief slotproductNum 接收产品编号，用于设备列表连接的设备选中
 * @param productNum 产品编号
 */
void ParaContentWidget::slotproductNum(QString productNum)
{
    if(g_productNum!=""){
        m_deviceListWidget->setSelectedByDeviceId(productNum, false);
    }else{

//        m_deviceListWidget->setSelectedByDeviceId(QString(), false);
        m_deviceListWidget->slotClearClicked();//清空目标列表
    }

    if(m_currentDeviceId!=productNum){
        slotsendpullStreamList();
        m_currentDeviceId=productNum;
    }

    logDeviceListDiagnostics(
        QStringLiteral("slotproductNum param=%1 global=%2").arg(productNum, g_productNum),
        productNum.trimmed().isEmpty() || g_productNum.trimmed().isEmpty());

}

void ParaContentWidget::slotCurrentDeviceStatus(QString strData, int statusCode)
{
    Q_UNUSED(statusCode);
    const QJsonDocument document = QJsonDocument::fromJson(strData.toUtf8());
    if (!document.isObject())
        return;

    const QJsonObject data = document.object().value(QStringLiteral("data")).toObject();
    if (data.contains(QStringLiteral("online")) &&
            !data.value(QStringLiteral("online")).toBool()) {
        slotCurrentDeviceOnlineChanged(false);
        return;
    }

    const QString deviceName = data.value(QStringLiteral("device_name")).toString().trimmed();
    const QString deviceId = data.value(QStringLiteral("device_id")).toString().trimmed();
    if (deviceName.isEmpty())
        return;
    if (deviceName == m_statusDeviceName && deviceId == m_statusDeviceId)
        return;

    m_statusDeviceName = deviceName;
    m_statusDeviceId = deviceId;
    refreshProductCombo(QString());
    selectPushStreamListBySerialNumber(ui->comboBox_product->currentData(Qt::UserRole).toString());
    emit signalUpdateReadListData(m_pushstreamlist);
    logDeviceListDiagnostics(QStringLiteral("slotCurrentDeviceStatus"));
}

void ParaContentWidget::slotCurrentDeviceOnlineChanged(bool online)
{
    if (online || (m_statusDeviceName.isEmpty() && m_statusDeviceId.isEmpty()))
        return;

    m_statusDeviceName.clear();
    m_statusDeviceId.clear();
    refreshProductCombo(QString());
    selectPushStreamListBySerialNumber(ui->comboBox_product->currentData(Qt::UserRole).toString());
    emit signalUpdateReadListData(m_pushstreamlist);
    logDeviceListDiagnostics(QStringLiteral("slotCurrentDeviceOnlineChanged(false)"), true);
}

/**
 * @brief parseDevicePushStreamData
 * 从单条 JSON 对象中抽取「序列号 / 设备名称 / 设备编号 / 设备IP / 推流列表」；序列号为唯一标识。
 * 推流列表仅收录数组中的字符串项；为列表 reserve 容量，减轻大数据量下的扩容开销。
 */
bool ParaContentWidget::parseDevicePushStreamData(const QJsonObject &obj, DevicePushStreamData &data) const
{
    const QString serialNumber = obj.value(QString::fromUtf8(u8"序列号")).toString().trimmed();
    const QString deviceName = obj.value(QString::fromUtf8(u8"设备名称")).toString().trimmed();
    const QString deviceId = obj.value(QString::fromUtf8(u8"设备编号")).toVariant().toString().trimmed();
    const QString deviceIp = obj.value(QString::fromUtf8(u8"设备IP")).toString().trimmed();
    const QJsonValue streamValue = obj.value(QString::fromUtf8(u8"推流列表"));
    if (serialNumber.isEmpty() || deviceIp.isEmpty()) {
        return false;
    }

    QStringList streamList;
    const QJsonArray streamArray = streamValue.toArray();
    streamList.reserve(streamArray.size());
    for (const QJsonValue &item : streamArray) {
        if (item.isString()) {
            streamList.append(item.toString());
        }
    }

    data.serialNumber = serialNumber;
    data.deviceName = deviceName;
    data.deviceId = deviceId;
    data.deviceIp = deviceIp;
    data.pushStreamList = std::move(streamList);
    return true;
}

/**
 * @brief applyDevicePushStreamDataBatch
 * 在持有一次写锁的前提下应用 batch：主表 m_devicePushStreamDataBySN -> 记录）。
 *  m_serialNumberBydeviceId（反查表：设备编号 -> 序列号）
 * 分三种路径：
 *   1) 同序列号且同设备编号：同步设备名称、IP 和推流列表；
 *   2) 同序列号但不同设备编号：从反查表移除该编号旧 deviceId 的项，再按新数据插入；
 *   3) 新 deviceId 已指向另一编号：删除另一编号的主表记录，再插入当前记录，保证每 deviceId 唯一对应一设备。
 */
void ParaContentWidget::applyDevicePushStreamDataBatch(QVector<DevicePushStreamData> &&batch)
{
    // 批量写入期间只加一次写锁，避免高并发下频繁加锁/解锁。
    QWriteLocker locker(&m_deviceDataLock);
    for (DevicePushStreamData &data : batch) {
        const auto bySNEnd = m_devicePushStreamDataBySN.end();
        auto bySNIt = m_devicePushStreamDataBySN.find(data.serialNumber);

        if (bySNIt != bySNEnd && bySNIt.value().deviceId == data.deviceId) {
            // 同序列号同编号：同步可变的名称、IP和推流列表。
            bySNIt.value().deviceName = data.deviceName;
            bySNIt.value().deviceIp = data.deviceIp;
            bySNIt.value().pushStreamList = data.pushStreamList;
            m_serialNumberBydeviceId.insert(data.deviceId, data.serialNumber);
            continue;
        }

        // 同序列号但编号变化：先删除旧IP反向索引。
        if (bySNIt != bySNEnd && !bySNIt.value().deviceId.isEmpty()) {
            m_serialNumberBydeviceId.remove(bySNIt.value().deviceId);
        }

        // 新编号已绑定其他设备：移除旧设备主记录，确保编号唯一映射。
        const auto byIdIt = m_serialNumberBydeviceId.find(data.deviceId);
        if (byIdIt != m_serialNumberBydeviceId.end() && byIdIt.value() != data.serialNumber) {
            m_devicePushStreamDataBySN.remove(byIdIt.value());
        }

        m_devicePushStreamDataBySN.insert(data.serialNumber, data);
        m_serialNumberBydeviceId.insert(data.deviceId, data.serialNumber);
    }


//    QWriteLocker locker(&m_deviceDataLock);
//    for (DevicePushStreamData &data : batch) {
//        auto it = m_devicePushStreamDataBySN.find(data.serialNumber);
//        if (it != m_devicePushStreamDataBySN.end()) {
//            it.value().deviceName = data.deviceName;
//            it.value().deviceId = data.deviceId;
//            it.value().deviceIp = data.deviceIp;
//            it.value().pushStreamList = data.pushStreamList;
//        } else {
//            m_devicePushStreamDataBySN.insert(data.serialNumber, std::move(data));
//        }
//    }

//    QString log;
//    log = QString::fromUtf8("m_devicePushStreamDataBySN size=%1\n").arg(m_devicePushStreamDataBySN.size());
//    for (auto it = m_devicePushStreamDataBySN.cbegin(); it != m_devicePushStreamDataBySN.cend(); ++it) {
//        const DevicePushStreamData &d = it.value();
//        log += QString::fromUtf8(
//                   "  key(serial)=%1  name=%2  deviceId=%3  ip=%4  pushStreamList=%5\n")
//                   .arg(it.key(), d.deviceName, d.deviceId, d.deviceIp,
//                        d.pushStreamList.join(QLatin1String(",")));
//    }
//        qDebug().noquote() << log;
}


/**
 * @brief getPushStreamListByserialNumber
 * 按序列号（唯一标识）查询推流列表；（顺序为哈希遍历顺序）
 */
QStringList ParaContentWidget::getPushStreamListByserialNumber(const QString &serialNumber) const
{
    // 查询走读锁，允许多线程并发读取。
    QReadLocker locker(&m_deviceDataLock);
    return m_devicePushStreamDataBySN.value(serialNumber.trimmed()).pushStreamList;
}

/**
 * @brief getDeviceIpByDeviceId
 * 按设备deviceId查询IP
 */
QString ParaContentWidget::getDeviceIpByDeviceId(const QString &deviceId) const
{
    // 两次哈希查询在同一读锁内完成，避免读到中间态。
    QReadLocker locker(&m_deviceDataLock);
    const QString targetserialNumber = m_serialNumberBydeviceId.value(deviceId.trimmed());
    return m_devicePushStreamDataBySN.value(targetserialNumber).deviceIp;
}

void ParaContentWidget::switchClientToDeviceVideoInput(const QString &deviceAddress)
{
    const QString deviceHost = streamOrDeviceHost(deviceAddress);
    if (deviceHost.isEmpty()) {
        FrameLogManager::getInstance()->log(
            QStringLiteral("ParameterSetWidget"),
            QString::fromUtf8(u8"[客户端自动切流] 无法从通信地址提取设备IP: address=%1")
                .arg(deviceAddress),
            IFrameLogManager::ERROR_TYPE);
        return;
    }

    // 保留当前客户端使用的 RTSP/RTMP 协议，但不保留当前流路径。
    QString protocol = QUrl(m_StreamPullAdress.trimmed(), QUrl::StrictMode).scheme().toLower();
    if (protocol != QStringLiteral("rtsp") && protocol != QStringLiteral("rtmp")) {
        protocol = QUrl(StreamUi::currentStreamUrl(ui->ClientPullUrl_comboBox).trimmed(),
                        QUrl::StrictMode).scheme().toLower();
    }
    if (protocol != QStringLiteral("rtsp") && protocol != QStringLiteral("rtmp"))
        protocol = QStringLiteral("rtsp");

    QSettings streamSettings(QApplication::applicationDirPath() + "/config/communication.ini",
                             QSettings::IniFormat);
    const QString videoInputPath = normalizedStreamPath(
                streamSettings.value("PushStream/pushstreamName", QStringLiteral("/live/stream")).toString(),
                QStringLiteral("/live/"), QStringLiteral("stream"));

    QUrl videoInputUrl;
    videoInputUrl.setScheme(protocol);
    videoInputUrl.setHost(deviceHost);
    videoInputUrl.setPort(protocol == QStringLiteral("rtmp") ? 1935 : 554);
    videoInputUrl.setPath(videoInputPath);
    const QString targetUrl = videoInputUrl.toString(QUrl::FullyEncoded);
    if (targetUrl.isEmpty())
        return;

    // 即使组播/SSE 列表中没有这一路，也直接加入地址框并下发给视频处理插件。
    m_StreamPullAdress = targetUrl;
    QSettings videoSettings(QApplication::applicationDirPath() + "/config/VideoProcess.ini",
                            QSettings::IniFormat);
    videoSettings.beginGroup("Video");
    videoSettings.setValue("inputUrl", m_StreamPullAdress);
    videoSettings.endGroup();

    selectPushStreamListBySerialNumber(
                ui->comboBox_product->currentData(Qt::UserRole).toString());
    StreamUi::setCurrentUnlistedStreamUrl(ui->ClientPullUrl_comboBox, m_StreamPullAdress);
    StreamUi::restoreElidedDisplay(ui->ClientPullUrl_comboBox);
    updateClientProtocolControls();
    emit signalStreamPullAdress(m_StreamPullAdress);

    const QString selectedUrl = StreamUi::currentStreamUrl(ui->ClientPullUrl_comboBox);
    FrameLogManager::getInstance()->log(
        QStringLiteral("ParameterSetWidget"),
        QString::fromUtf8(u8"[客户端自动切流] 强制切换视频输入: deviceAddress=%1, host=%2, protocol=%3, path=%4, target=%5, combo=%6, matched=%7")
            .arg(deviceAddress, deviceHost, protocol, videoInputPath, targetUrl, selectedUrl)
            .arg(selectedUrl == targetUrl),
        selectedUrl == targetUrl ? IFrameLogManager::DEBUG_TYPE : IFrameLogManager::ERROR_TYPE);
}

void ParaContentWidget::logDeviceListDiagnostics(const QString &reason, bool force)
{
    if (!ui)
        return;

    const int upperCount = m_deviceListWidget ? m_deviceListWidget->deviceCount() : -1;
    const int comboDeviceCount = ui->comboBox_product->count() > 0
            ? ui->comboBox_product->count() - 1 : 0;
    int cacheCount = 0;
    {
        QReadLocker locker(&m_deviceDataLock);
        cacheCount = m_devicePushStreamDataBySN.size();
    }

    const bool mismatch = upperCount != cacheCount
            || (upperCount == 0 && comboDeviceCount > 0);
    if (!force && !mismatch)
        return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!force && now - m_lastDeviceListMismatchLogMs < 3000)
        return;
    m_lastDeviceListMismatchLogMs = now;

    QStringList upperRows;
    if (m_deviceListWidget)
        upperRows = m_deviceListWidget->diagnosticRows();

    QStringList comboRows;
    for (int i = 1; i < ui->comboBox_product->count(); ++i) {
        QString name = ui->comboBox_product->itemData(i, Qt::ToolTipRole).toString();
        if (name.isEmpty())
            name = ui->comboBox_product->itemText(i);
        comboRows.append(QStringLiteral("SN=%1,Name=%2")
                         .arg(ui->comboBox_product->itemData(i, Qt::UserRole).toString(), name));
    }

    QStringList cacheRows;
    {
        QReadLocker locker(&m_deviceDataLock);
        QStringList serials = m_devicePushStreamDataBySN.keys();
        std::sort(serials.begin(), serials.end(), [](const QString &a, const QString &b) {
            return QString::compare(a, b, Qt::CaseInsensitive) < 0;
        });
        cacheRows.reserve(serials.size());
        for (const QString &serial : serials) {
            const DevicePushStreamData data = m_devicePushStreamDataBySN.value(serial);
            cacheRows.append(QStringLiteral("SN=%1,ID=%2,Name=%3,IP=%4,Streams=%5")
                             .arg(serial, data.deviceId, data.deviceName, data.deviceIp)
                             .arg(data.pushStreamList.size()));
        }
    }

    const qint64 multicastAgeMs = m_lastAnyMulticastReceivedMs > 0
            ? now - m_lastAnyMulticastReceivedMs : -1;
    const QString message = QString::fromLocal8Bit(
                "[设备列表诊断] reason=%1, mismatch=%2, 上方列表=%3, 名称下拉=%4, 组播缓存=%5, "
                "statusName=%6, statusId=%7, currentIP=%8, productNum=%9, lastMulticastAgeMs=%10; "
                "upper=[%11]; combo=[%12]; cache=[%13]")
            .arg(reason)
            .arg(mismatch)
            .arg(upperCount)
            .arg(comboDeviceCount)
            .arg(cacheCount)
            .arg(m_statusDeviceName)
            .arg(m_statusDeviceId)
            .arg(m_currentDeviceIP)
            .arg(g_productNum)
            .arg(multicastAgeMs)
            .arg(upperRows.join(QStringLiteral("; ")))
            .arg(comboRows.join(QStringLiteral("; ")))
            .arg(cacheRows.join(QStringLiteral("; ")));
    FrameLogManager::getInstance()->log(
                QStringLiteral("ParameterSetWidget"), message,
                mismatch ? IFrameLogManager::ERROR_TYPE : IFrameLogManager::DEBUG_TYPE);
}


/**
 * @brief selectPushStreamListBySerialNumber 按下拉项内部绑定的设备序列号刷新推流列表
 * @param serialNumber 为空时表示“全部”
 */
void ParaContentWidget::selectPushStreamListBySerialNumber(const QString &serialNumber)
{
    QSettings streamSettings(QApplication::applicationDirPath() + "/config/communication.ini",
                             QSettings::IniFormat);
    const QString hdmiStreamPath = normalizedStreamPath(
                streamSettings.value("PushStream/pushstreamName", QStringLiteral("/live/stream")).toString(),
                QStringLiteral("/live/"), QStringLiteral("stream"));
    const QString screenStreamPath = normalizedStreamPath(
                streamSettings.value("ScreenPush/streamName", QStringLiteral("stream")).toString(),
                QStringLiteral("/screen/"), QStringLiteral("stream"));
    // 四宫格（拼接）流由设备在主推流名基础上追加 "_mosaic1" 生成，不走单独的配置项。
    const QString mosaicStreamPath = hdmiStreamPath + QStringLiteral("_mosaic1");

    const QString key = serialNumber.trimmed();
    const bool statusDeviceSelected = key == kCurrentStatusDeviceKey;
    QStringList pushstreamlist;
    QStringList allPushstreamlist;
    QHash<QString, QString> displayNames;
    QVector<QPair<QString, QString>> deviceNamesByHost;
    bool selectedCurrentDevice = false;
    QString selectedDeviceName;
    QString currentDeviceName;
    {
        QReadLocker locker(&m_deviceDataLock);
        auto registerDeviceAliases = [&displayNames, &deviceNamesByHost,
                &hdmiStreamPath, &screenStreamPath, &mosaicStreamPath, this]
                (const DevicePushStreamData &data) {
            const QString host = streamOrDeviceHost(data.deviceIp);
            const bool isStatusDevice = !m_statusDeviceName.trimmed().isEmpty()
                    && ((!m_statusDeviceId.isEmpty() && data.deviceId == m_statusDeviceId)
                        || host == streamOrDeviceHost(m_currentDeviceIP));
            const QString deviceName = isStatusDevice
                    ? m_statusDeviceName.trimmed() : data.deviceName.trimmed();
            if (!host.isEmpty() && !deviceName.isEmpty())
                deviceNamesByHost.append(qMakePair(host, deviceName));
            for (const QString &stream : data.pushStreamList) {
                const QString friendly = specialStreamDisplayName(
                            deviceName, stream, hdmiStreamPath, screenStreamPath, mosaicStreamPath);
                if (!friendly.isEmpty())
                    displayNames.insert(stream.trimmed(), friendly);
            }
        };
        auto appendDeviceStreams = [&pushstreamlist](const DevicePushStreamData &data) {
            pushstreamlist.append(data.pushStreamList);
        };

        // 设备筛选只限制可选流列表；别名解析必须始终保留全部已发现设备的数据。
        // 否则查看 02 时，02 的窗口若正在拉取 01-HDMI，会因 01 被筛掉而回退显示真实 URL。
        for (auto it = m_devicePushStreamDataBySN.cbegin();
             it != m_devicePushStreamDataBySN.cend(); ++it) {
            registerDeviceAliases(it.value());
            allPushstreamlist.append(it.value().pushStreamList);
        }

        if (key.isEmpty()) {
            pushstreamlist = allPushstreamlist;
        } else if (statusDeviceSelected) {
            selectedDeviceName = m_statusDeviceName.trimmed();
            selectedCurrentDevice = true;
            pushstreamlist = m_curDevicepushstreamlist;
        } else {
            const DevicePushStreamData selectedData = m_devicePushStreamDataBySN.value(key);
            const QString selectedHost = streamOrDeviceHost(selectedData.deviceIp);
            const bool selectedIsStatusDevice = !m_statusDeviceName.trimmed().isEmpty()
                    && ((!m_statusDeviceId.isEmpty()
                         && selectedData.deviceId == m_statusDeviceId)
                        || selectedHost == streamOrDeviceHost(m_currentDeviceIP));
            selectedDeviceName = selectedIsStatusDevice
                    ? m_statusDeviceName.trimmed() : selectedData.deviceName.trimmed();
            appendDeviceStreams(selectedData);
        }
        const QString currentSerial = m_serialNumberBydeviceId.value(g_productNum.trimmed());
        selectedCurrentDevice = selectedCurrentDevice
                || (!key.isEmpty() && currentSerial == key);
        currentDeviceName = m_statusDeviceName.trimmed();
        if (currentDeviceName.isEmpty())
            currentDeviceName = m_devicePushStreamDataBySN.value(currentSerial).deviceName;
    }

    if (!m_statusDeviceName.trimmed().isEmpty()) {
        const QPair<QString, QString> statusDevice(
                    streamOrDeviceHost(m_currentDeviceIP), m_statusDeviceName.trimmed());
        if (!statusDevice.first.isEmpty() && !deviceNamesByHost.contains(statusDevice))
            deviceNamesByHost.append(statusDevice);
    }

    // 正在拉流的窗口地址不一定仍存在于设备“推流列表”中（例如投屏关闭后，
    // 设备仍在线但 /screen/... 流会消失）。按 URL 主机从完整设备目录反查名称，
    // 使窗口保存的 HDMI/投屏地址在源暂时离线时仍保持友好名称。
    auto registerConfiguredStreamAlias = [&displayNames, &deviceNamesByHost,
            &hdmiStreamPath, &screenStreamPath, &mosaicStreamPath, &currentDeviceName, this]
            (const QString &stream) {
        const QString actual = stream.trimmed();
        if (actual.isEmpty())
            return;

        const QString streamHost = streamOrDeviceHost(actual);
        QString deviceName;
        for (const QPair<QString, QString> &device : deviceNamesByHost) {
            if (device.first == streamHost) {
                deviceName = device.second;
                break;
            }
        }
        if (deviceName.isEmpty()
                && streamHost == streamOrDeviceHost(m_currentDeviceIP)) {
            deviceName = currentDeviceName;
        }

        const QString friendly = specialStreamDisplayName(
                    deviceName, actual, hdmiStreamPath, screenStreamPath, mosaicStreamPath);
        if (!friendly.isEmpty())
            displayNames.insert(actual, friendly);
    };
    for (const VideoSetData &videoSetData : m_vVideoSetData)
        registerConfiguredStreamAlias(videoSetData.VideoIp);
    registerConfiguredStreamAlias(m_StreamPullAdress);

    Global::initialize();
    // SSE 提供的当前设备流也属于全局别名目录，不应随顶部设备筛选一起丢失。
    for (const QString &stream : m_curDevicepushstreamlist) {
        const QString friendly = specialStreamDisplayName(
                    currentDeviceName, stream, hdmiStreamPath, screenStreamPath, mosaicStreamPath);
        if (!friendly.isEmpty())
            displayNames.insert(stream.trimmed(), friendly);
    }

    //将pushstreamlist-m_curDevicepushstreamlist的差集，在合并到 pushstreamlist
    if (selectedCurrentDevice || key.isEmpty()) {
        pushstreamlist << m_curDevicepushstreamlist.toSet().subtract(pushstreamlist.toSet()).toList();
    }

    // 顶部客户端拉流地址不参与设备筛选，始终汇总所有设备及当前 SSE 流。
    allPushstreamlist << m_curDevicepushstreamlist.toSet()
                         .subtract(allPushstreamlist.toSet()).toList();

    pushstreamlist.removeDuplicates();
    allPushstreamlist.removeDuplicates();

    // SSE 列表中的流可能没有出现在该设备的组播“推流列表”里。
    // 对已筛选设备直接使用其名称；“全部”模式则按 URL 主机 IP 反查设备名称。
    for (const QString &stream : pushstreamlist) {
        QString deviceName = selectedDeviceName;
        if (deviceName.isEmpty()) {
            const QString streamHost = streamOrDeviceHost(stream);
            for (const QPair<QString, QString> &device : deviceNamesByHost) {
                if (device.first == streamHost) {
                    deviceName = device.second;
                    break;
                }
            }
            if (deviceName.isEmpty()
                    && streamHost == streamOrDeviceHost(m_currentDeviceIP)) {
                deviceName = currentDeviceName;
            }
        }

        const QString friendly = specialStreamDisplayName(
                    deviceName, stream, hdmiStreamPath, screenStreamPath, mosaicStreamPath);
        if (!friendly.isEmpty())
            displayNames.insert(stream.trimmed(), friendly);
    }

    // communication.ini 的 PullStream 键名是用户明确指定的显示名称，
    // 优先级高于按设备和流路径自动生成的别名；键名支持中文。
    for (auto it = m_localStreamDisplayNameByUrl.cbegin();
         it != m_localStreamDisplayNameByUrl.cend(); ++it) {
        displayNames.insert(it.key(), it.value());
    }

    StreamUi::g_displayNameByUrl = displayNames;

    QSet<QString> localUrls;
    for (const StreamUi::StreamItem &item : m_localPullStreamItems)
        localUrls.insert(item.url);

    StreamUi::StreamItemList pushstreamItems;
    for (const QString &stream : pushstreamlist) {
        const QString trimmed = stream.trimmed();
        if (trimmed.isEmpty() || localUrls.contains(trimmed))
            continue;
        pushstreamItems.append(StreamUi::StreamItem(trimmed, displayNames.value(trimmed, trimmed)));
    }

    // 将 communication.ini 中的流条目加入 pushstreamItems（不进行地址去重）
    if (key.isEmpty()) {
        for (const StreamUi::StreamItem &item : m_localPullStreamItems) {
            if (!pushstreamItems.contains(item))
                pushstreamItems.append(item);
        }
    } else {
        const DevicePushStreamData selectedData = m_devicePushStreamDataBySN.value(key);
        const QString selectedHost = streamOrDeviceHost(selectedData.deviceIp);
        for (const StreamUi::StreamItem &item : m_localPullStreamItems) {
            if (streamOrDeviceHost(item.url) == selectedHost && !pushstreamItems.contains(item))
                pushstreamItems.append(item);
        }
    }

    StreamUi::StreamItemList allPushstreamItems;
    for (const QString &stream : allPushstreamlist) {
        const QString trimmed = stream.trimmed();
        if (trimmed.isEmpty() || localUrls.contains(trimmed))
            continue;
        allPushstreamItems.append(StreamUi::StreamItem(trimmed, displayNames.value(trimmed, trimmed)));
    }
    // 顶部客户端拉流始终加入所有 localPullStreamItems（不进行地址去重）
    for (const StreamUi::StreamItem &item : m_localPullStreamItems) {
        if (!allPushstreamItems.contains(item))
            allPushstreamItems.append(item);
    }

    for (const QString &u : localUrls) {
        if (!allPushstreamlist.contains(u))
            allPushstreamlist.append(u);
    }

    m_pushstreamlist = pushstreamItems;
    Global::g_ZLMpushStreamList = allPushstreamlist;
    refreshClientPullUrlCombo(allPushstreamItems);
}


/**
 * @brief loadQssStyle
 * 设置界面Qss初始化样式表
 * @param void
 * @return void
 */
void ParaContentWidget::loadQssStyle()
{
    QString strPath = m_pPlugin->getStylePath() + QString("/ParaContentWidget.qss");

    Q_ASSERT(!strPath.isEmpty() && "path is empty");
    QFile textFile(strPath);

    Q_ASSERT(textFile.exists() && "file can't open");
    textFile.open(QIODevice::ReadOnly | QIODevice::Text);
    QTextStream textStream(&textFile);

    QString style = textStream.readAll();
    this->setStyleSheet(style);
}

/**
 * @brief updateVideoSetData
 * 刷新视频参数设置列表
 * @param vdData 下发的视频参数列表
 * @return void
 */
void ParaContentWidget::updateVideoSetData(QVector<VideoSetData> vdData)
{
    QSet<int> incomingSmallScreenNums;
    for (const VideoSetData &d : vdData)
        incomingSmallScreenNums.insert(d.SmallScreenNum);

    for (const VideoSetData &dData : vdData)
    {
        int matchIndex = -1;
        for (int index = 0; index < m_vVideoSetData.size(); ++index)
        {
            if (m_vVideoSetData[index].SmallScreenNum == dData.SmallScreenNum)
            {
                matchIndex = index;
                break;
            }
        }

        if (matchIndex >= 0)
        {
            const VideoSetData &oldData = m_vVideoSetData[matchIndex];
            QString mergedVideoIp = dData.VideoIp.trimmed();
            // 定时查询无拉流开关字段；在 list 内表示正在拉流。地址缺失时保留本地地址避免结束拉流时闪占位符
            if (mergedVideoIp.isEmpty() && !oldData.VideoIp.trimmed().isEmpty())
                mergedVideoIp = oldData.VideoIp.trimmed();

            VideoSetData mergedData = dData;
            mergedData.VideoIp = mergedVideoIp;
            mergedData.bIsStreamPullOn = true;

            const bool needUiUpdate =
                oldData.VideoIp != mergedData.VideoIp ||
                oldData.bisBigScreenShow != mergedData.bisBigScreenShow ||
                oldData.VideoName != mergedData.VideoName ||
                oldData.bIsIdentifiy != mergedData.bIsIdentifiy ||
                oldData.bIsStreamPullOn != mergedData.bIsStreamPullOn ||
                oldData.connectstatus != mergedData.connectstatus;

            m_vVideoSetData[matchIndex].VideoIp = mergedData.VideoIp;
            m_vVideoSetData[matchIndex].VideoName = mergedData.VideoName;
            m_vVideoSetData[matchIndex].bisBigScreenShow = mergedData.bisBigScreenShow;
            m_vVideoSetData[matchIndex].SmallScreenNum = mergedData.SmallScreenNum;
            m_vVideoSetData[matchIndex].bIsIdentifiy = mergedData.bIsIdentifiy;
            m_vVideoSetData[matchIndex].bIsStreamPullOn = mergedData.bIsStreamPullOn;
            m_vVideoSetData[matchIndex].connectstatus = mergedData.connectstatus;

            if (needUiUpdate)
            {
                VideoSetWidget *const w = videoSetWidgetForScreen(dData.SmallScreenNum);
                // 结束拉流 HTTP 进行中：跳过定时刷新，避免 setVideoSetListInfo 把界面又刷回「结束」态
                if (w && !w->isStreamTogglePending())
                    w->setVideoSetListInfo(mergedData, m_pushstreamlist);
            }
            continue;
        }

        if (m_vVideoSetData.size() > MAX_VIDEOSET_NUM - 1)
        {
            emit signalMessageInfoText(QString::fromUtf8(u8"提示"),
                                       QString::fromUtf8(u8"添加的视频通道已到达限制！"),
                                       MessageType_Warn);
            continue;
        }

        VideoSetData newSlotData = dData;
        newSlotData.bIsStreamPullOn = true;
        m_vVideoSetData.push_back(newSlotData);

        VideoSetWidget *item = new VideoSetWidget(ui->scrollAreaWidgetContents);
        item->initUi(m_pPlugin);
        item->setFixedWidth(ui->scrollAreaWidgetContents->width() - m_nScrollOffset);
        //视频参数下发
        qRegisterMetaType<VideoSetData>("VideoSetData");
        connect(item, SIGNAL(signalSendVideoSetInfo(VideoSetData, bool)), this, SLOT(slotSendVideoSetInfo(VideoSetData, bool)));
        connect(item, SIGNAL(signalDeleteItemVideoSetItem(VideoSetData)), this, SLOT(slotDeleteItemVideoSetItem(VideoSetData)));
        connect(item, SIGNAL(signalIdentifyVideoSetItem(VideoSetData, bool)), this, SLOT(slotIdentifyVideoSetItem(VideoSetData, bool)));
        connect(item, SIGNAL(signalMessageBoxText(QString, QString, MessageType)), this, SIGNAL(signalMessageInfoText(QString, QString, MessageType)));
//        connect(item, SIGNAL(signalupdateVideoSetDate(VideoSetData)), this, SLOT(slotUpdateVideoSetDate(VideoSetData)));
        connect(item, SIGNAL(signalSwitchBigScreen(VideoSetData, bool)), this, SLOT(slotSwitchBigScreenSetItem(VideoSetData, bool)));
        connect(item, SIGNAL(signalRefreshpushStreamList()), this, SLOT(slotRefreshpushStreamList()));
        connect(this, SIGNAL(signalUpdateReadListData(StreamUi::StreamItemList)), item, SLOT(slotUpdateReadListData(StreamUi::StreamItemList)));
//        connect(this, SIGNAL(signalSendVideoSetResult()), item, SLOT(slotSendVideoSetTimeout()));

        item->setVideoSetListInfo(newSlotData, m_pushstreamlist);
        item->setVisible(true);
        m_listVideoSetItem.append(item);

        updateVideoUI();
    }


    bool anyUnmatchedInit = false;
    for (int index = 0; index < m_vVideoSetData.size(); ++index)
    {
        const int num = m_vVideoSetData[index].SmallScreenNum;
        if (incomingSmallScreenNums.contains(num))
            continue;

        const VideoSetData &oldData = m_vVideoSetData[index];
        if (isIdleVideoSetSlot(oldData))
            continue;

        VideoSetData resetData;
        resetData.SmallScreenNum = num;
        // 流断开/结束时保留用户自定义名称，仅空名称时回退默认「视频N」
        resetData.VideoName = oldData.VideoName.trimmed().isEmpty()
            ? (QString::fromUtf8(u8"视频") + QString::number(num))
            : oldData.VideoName.trimmed();
        resetData.VideoIp.clear();
        resetData.bisBigScreenShow = false;
        resetData.bIsIdentifiy = false;
        resetData.bIsStreamPullOn = false;

        m_vVideoSetData[index] = resetData;
        anyUnmatchedInit = true;
        if (VideoSetWidget *w = videoSetWidgetForScreen(num))
            w->setVideoComboxInfo(resetData, m_pushstreamlist);

    }

    if (anyUnmatchedInit)
        updateVideoUI();
}

/**
 * @brief updateAddVideoSetData
 * 手动添加时，并刷新视频参数设置列表
 * @param QStringList
 * @return void
 */
void ParaContentWidget::updateAddVideoSetData(VideoSetData dData, const StreamUi::StreamItemList &strListData, bool refreshLayout)
{

    if(m_vVideoSetData.size() > MAX_VIDEOSET_NUM-1)
    {
        emit signalMessageInfoText(QString::fromUtf8(u8"提示"),
                                   QString::fromUtf8(u8"添加的视频通道已到达限制！"),
                                   MessageType_Warn);
        return;
    }

//    VideoSetData dData;
//    dData.VideoName=name;
    m_vVideoSetData.push_back(dData);

    VideoSetWidget * item = new VideoSetWidget(ui->scrollAreaWidgetContents);
    item->initUi(m_pPlugin);
    item->setFixedWidth(ui->scrollAreaWidgetContents->width() - m_nScrollOffset);
    //视频参数下发
    connect(item,SIGNAL(signalSendVideoSetInfo(VideoSetData,bool)),this,SLOT(slotSendVideoSetInfo(VideoSetData,bool)));
    connect(item,SIGNAL(signalDeleteItemVideoSetItem(VideoSetData)),this,SLOT(slotDeleteItemVideoSetItem(VideoSetData)));
    connect(item,SIGNAL(signalIdentifyVideoSetItem(VideoSetData,bool)),this,SLOT(slotIdentifyVideoSetItem(VideoSetData,bool)));
    connect(item,SIGNAL(signalMessageBoxText(QString,QString,MessageType)),this,SIGNAL(signalMessageInfoText(QString,QString,MessageType)));
//    connect(item,SIGNAL(signalupdateVideoSetDate(VideoSetData)),this,SLOT(slotUpdateVideoSetDate(VideoSetData)));
    connect(item,SIGNAL(signalSwitchBigScreen(VideoSetData,bool)),this,SLOT(slotSwitchBigScreenSetItem(VideoSetData,bool)));
    connect(item,SIGNAL(signalRefreshpushStreamList()),this,SLOT(slotRefreshpushStreamList()));
    connect(this,SIGNAL(signalUpdateReadListData(StreamUi::StreamItemList)),item,SLOT(slotUpdateReadListData(StreamUi::StreamItemList)));
//    connect(this,SIGNAL(signalSendVideoSetResult()),item,SLOT(slotSendVideoSetTimeout()));


    item->setVideoComboxInfo(dData,strListData);
    item->setVisible(true);
    m_listVideoSetItem.append(item);

    if (refreshLayout) {
        updateVideoUI();
    }
}

/**
 * @brief initDefaultVideoSetItems
 * 与连续四次 slotAddVideoSetItem 等价，但只触发一次布局刷新，避免重复排序与 geometry 计算。
 */
void ParaContentWidget::initDefaultVideoSetItems()
{
    for (int i = 0; i < MAX_VIDEOSET_NUM; ++i) {
        addVideoSetZLMlistUi(m_pushstreamlist, false);
    }
    updateVideoUI();
}

/** 当前是否为默认空窗口布局（无拉流中通道），用于避免重复 clear/init */
bool ParaContentWidget::isDefaultVideoSetItemsLayout() const
{
    if (m_listVideoSetItem.isEmpty())
        return m_vVideoSetData.isEmpty();

    if (m_listVideoSetItem.size() != MAX_VIDEOSET_NUM)
        return false;

    for (int n = 0; n < m_listVideoSetItem.size(); ++n) {
        VideoSetWidget *item = m_listVideoSetItem.at(n);
        if (!item)
            return false;
        const VideoSetData d = item->getVideoSetData();
        if (d.bIsStreamPullOn /*|| !d.VideoIp.trimmed().isEmpty()*/)
            return false;
    }
    return true;
}

/**
 * @brief addVideoSetUi
 * 添加一组视频参数设置界面
 * @param void
 * @return void
 */
void ParaContentWidget::addVideoSetUi(QVector<VideoSetData> vstrData)
{
    updateVideoSetData(vstrData);
}

/**
 * @brief addVideoSetZLMlistUi
 * 添加一组ZLM服务器当前推流地址
 * @param QStringList
 * @return void
 */
void ParaContentWidget::addVideoSetZLMlistUi(const StreamUi::StreamItemList &strListData, bool refreshLayout)
{
    VideoSetData strData;
    int Number = getIdleItemNumber();
    strData.VideoName = (QString::fromUtf8(u8"视频") + QString::number(Number));
    strData.SmallScreenNum=Number;
    updateAddVideoSetData(strData, strListData, refreshLayout);
}

/**
 * @brief updateVideoUI
 * UI刷新
 * @param void
 * @return void
 */
void ParaContentWidget::updateVideoUI()
{
    // 界面Item显示前，先根据序号进行排序，然后再依据序号顺序进行显示
    std::sort(m_listVideoSetItem.begin(),m_listVideoSetItem.end(),compareTargetNum);

    int nIndex = 0;
    int height = 0;
    int height_single;
    int y = 0;
    int x = 0;

    for(int n = 0 ; n < m_listVideoSetItem.size(); ++n)
    {
        VideoSetWidget *item = (VideoSetWidget *)m_listVideoSetItem.at(m_listVideoSetItem.size()-n-1);
//        item->setDelVideoSetShowAndHide(true);
        item->resize(800,60);
        item->move(x, y);
        height_single = item->height();
        y = height + height_single +5;
        ++nIndex;
        height = y;
    }

    ui->scrollAreaWidgetContents->setFixedHeight(height);
    ui->scrollArea->setWidget(ui->scrollAreaWidgetContents);
}

/**
 * @brief setPushStream
 * 将设置成功的推流地址存储到本地
 * @param void
 * @return void
 */
void ParaContentWidget::setPushStream()
{
    QSettings settings(m_pPlugin->getConfigPath() + "/communication.ini" , QSettings::IniFormat);
    settings.setIniCodec(QTextCodec::codecForName("UTF-8"));
    settings.beginGroup("PushStream");
    settings.setValue("IP", StreamUi::currentStreamUrl(ui->ClientPullUrl_comboBox));
    settings.endGroup();
    CommunicationIniUtils::syncPreservingPullStreamKeys(settings);
}

/**
 * @brief getIdleItemNumber();
 * 获取空闲Item视频编号
 * @param void
 * @return int
 */
int ParaContentWidget::getIdleItemNumber()
{
    int ntemp = -1;
    for(int i=0;i<MAX_VIDEOSET_NUM;i++)
    {
        if(m_nArrayFlag[i] == 0)
        {
            m_nArrayFlag[i] = 1;
            ntemp = i+1;
            return ntemp;
        }
    }
    return ntemp;
}

/**
 * @brief setIdentify;
 * 识别开关
 * @param bFlag(true:识别开 false:识别关)
 * @return void
 */
void ParaContentWidget::setIdentify(VideoSetData dData, bool bFlag)
{
    QJsonObject json;
    json[QString::fromUtf8(u8"拉流地址")] = dData.VideoIp;
    json[QString::fromUtf8(u8"拉流开关")] = true;

    if(bFlag)
    {
        json[QString::fromUtf8(u8"识别开关")] = true;
        m_bIdentifySwitch = true;
    }
    else
    {
        json[QString::fromUtf8(u8"识别开关")] = false;
        m_bIdentifySwitch = false;
    }

    json[QString::fromUtf8(u8"视频名称")] = dData.VideoName;
    json[QString::fromUtf8(u8"视频位置")] = dData.SmallScreenNum;


    QString url = "http://" + m_strUrl + "/proxy/setting/pullStream";
    emit signalPosPullStreamPara(url, json, dData, kPullStreamReqIdentify);

}

/**
 * @brief showDeviceListWidget 显示隐藏DeviceListWidget
 */
void ParaContentWidget::showDeviceListWidget(QPoint pos)
{
    if (!m_deviceListWidget) {
        return;
    }

    if (m_deviceListWidget->isVisible()) {
        m_deviceListWidget->hide();
        return;
    }
//    const QPoint globalBtn = ui->AddItem_pushButton->mapToGlobal(QPoint(0, ui->AddItem_pushButton->height()));
    m_deviceListWidget->move(pos + QPoint(0, 4));
    m_deviceListWidget->show();
    m_deviceListWidget->raise();
    logDeviceListDiagnostics(QStringLiteral("showDeviceListWidget"), true);

}

/**
 * @brief clearAllVideoSetItem 清空所有item数据
 */
void ParaContentWidget::clearAllVideoSetItem()
{
    memset(m_nArrayFlag,0,sizeof(m_nArrayFlag));
    for (int n = 0; n != m_listVideoSetItem.size(); ++n) {
        delete m_listVideoSetItem.at(n);
    }
    m_listVideoSetItem.clear();
    m_vVideoSetData.clear();
}

void ParaContentWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_shuttingDown || !ui || !ui->ClientPullUrl_comboBox)
        return;
    // 仅在打开设置页时同步到当前正在拉流播放的源；之后组播刷新不再改选中项。
    selectPushStreamListBySerialNumber(
                ui->comboBox_product->currentData(Qt::UserRole).toString());
    if (!m_StreamPullAdress.trimmed().isEmpty())
        StreamUi::setCurrentUnlistedStreamUrl(ui->ClientPullUrl_comboBox, m_StreamPullAdress);
    StreamUi::restoreElidedDisplay(ui->ClientPullUrl_comboBox);
    updateClientProtocolControls();
}

bool ParaContentWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (ui && ui->comboBox_product && watched == ui->comboBox_product->view()
            && event->type() == QEvent::Hide && m_productComboRefreshPending) {
        // Hide 可能早于 QComboBox 提交用户点击的 currentIndexChanged；排到下一轮事件循环，
        // 先完成本次选择，再按最新设备缓存安全刷新。
        QTimer::singleShot(0, this, [this]() {
            if (m_shuttingDown || !ui || !m_productComboRefreshPending)
                return;
            m_productComboRefreshPending = false;
            refreshProductCombo(QString());
            const QString serial = ui->comboBox_product->currentData(Qt::UserRole).toString();
            selectPushStreamListBySerialNumber(serial);
            emit signalUpdateReadListData(m_pushstreamlist);
        });
    }

    if (ui && ui->ClientPullUrl_comboBox && watched == ui->ClientPullUrl_comboBox
            && event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            // 可编辑下拉框可能仍带有上一次输入的“编辑中”标记。点击箭头时先
            // 结束该状态，确保本次强制刷新不会被 refreshClientPullUrlCombo 延后。
            ui->ClientPullUrl_comboBox->setProperty("streamComboEditing", false);
            slotRefreshpushStreamList();
            slotsendpullStreamList();
        }
    }

    if(watched==ui->comboBox_product && event->type()==QEvent::MouseButtonPress){
        QMouseEvent *mouseEvent=static_cast<QMouseEvent*>(event);
        if(mouseEvent->button()==Qt::LeftButton){
            refreshProductCombo(QString());
        }
    }
    return QObject::eventFilter(watched,event);
}
