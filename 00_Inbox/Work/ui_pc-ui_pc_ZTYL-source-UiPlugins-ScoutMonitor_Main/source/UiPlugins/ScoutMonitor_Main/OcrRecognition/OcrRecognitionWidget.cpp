#include "OcrRecognitionWidget.h"
#include "ui_OcrRecognitionWidget.h"
#include "UiPluginFactory.h"
#include <QFile>
#include <QTextCodec>
#include <QMessageBox>
#include <QSettings>
#include <QApplication>
#include <QDateTime>
#include <QRegExp>
#include <QRegExpValidator>
#include <QValidator>
#include "Comm/DataMgr.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <FrameLogManager.h>

namespace {
bool isEmptyTargetNumber(const QString &text)
{
    const QString value = text.trimmed();
    return value.isEmpty() || value == QLatin1String("--") || value == QLatin1String("XX");
}

QString currentTargetNumberProtocol()
{
    QSettings settings(QApplication::applicationDirPath() + "/config/communication.ini",
                       QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("Address"));
    QString protocol = settings.value(QStringLiteral("cartype"),
                                      QStringLiteral("033")).toString().trimmed().toUpper();
    settings.endGroup();
    if (protocol.isEmpty()) {
        protocol = QStringLiteral("033");
    }
    return protocol;
}

QString targetNumberSerialKey(const QString &protocol)
{
    return QStringLiteral("serial_%1").arg(protocol);
}

QString targetNumberDateKey(const QString &protocol)
{
    return QStringLiteral("date_%1").arg(protocol);
}

class TargetNumber002Validator : public QValidator
{
public:
    explicit TargetNumber002Validator(QObject *parent = nullptr)
        : QValidator(parent)
    {
    }

    State validate(QString &input, int &position) const override
    {
        Q_UNUSED(position);
        if (input.isEmpty()) {
            return Intermediate;
        }
        if (input.size() > 5) {
            return Invalid;
        }
        for (const QChar ch : input) {
            if (!ch.isDigit()) {
                return Invalid;
            }
        }

        bool ok = false;
        const uint value = input.toUInt(&ok);
        if (!ok || value > 65535u) {
            return Invalid;
        }
        return value >= 1u ? Acceptable : Intermediate;
    }
};
} // namespace


const int MAX_CHARS=10240;

OcrRecognitionWidget::OcrRecognitionWidget(QWidget *parent) :
    CustomMoveWidget(parent),
    ui(new Ui::OcrRecognitionWidget)
{
    ui->setupUi(this);
    setTitle(ui->OcrContent_widget);
    setWindowFlags(Qt::FramelessWindowHint /*| Qt::WindowStaysOnTopHint*/ |Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground,true);
}

OcrRecognitionWidget::~OcrRecognitionWidget()
{
    delete ui;
}

/**
 * @brief initUi
 * 界面初始化函数。
 * @param pPlugin 界面框架指针
 * @return 无返回值
 */
void OcrRecognitionWidget::initUi(IUiPluginBase *pPlugin)
{
    m_pPlugin = pPlugin;
    m_pPlugin->setStyle("day");

    // 获取图片存储路径
    initCfg();

    // 创建图片显示类
    m_pImageWidget = new ImageWidget(ui->Picture_widget);

    // 创建下载器实例
    m_pImageDownloader = new ImageDownloader(this);

    connect(ui->Close_Btn,SIGNAL(clicked()),this,SLOT(slotClose_Btn()));
    connect(ui->Modify_Btn,SIGNAL(clicked(bool)),this,SLOT(slotModify_Btn(bool)));
    connect(ui->Report_Btn,SIGNAL(clicked()),this,SLOT(slotReport_Btn()));
    connect(ui->textEdit_OCR, &QTextEdit::textChanged,this,&OcrRecognitionWidget::slottextChanged);
    connect(ui->pushButton_Track, &QPushButton::clicked,this,[=](bool statu){
        m_bTrackSwitch=statu;
        if(statu){
            dealTargetTrack(0,1,1,1);
            m_pReportPosition->start();
        }else{
            m_pReportPosition->stop();
        }
    });
    ui->pushButton_Track->hide();

    // 连接信号槽，处理下载结果
    connect(m_pImageDownloader, SIGNAL(downloadCompleted(QString)),this,SLOT(slotDownloadCompleted(QString)));
    connect(m_pImageDownloader, SIGNAL(downloadFailed(QString)),this,SLOT(slotloadFailed(QString)));
    connect(this,SIGNAL(signalIdentify(int,int)),this,SLOT(slotIdentify(int,int)));


    ui->X_label->hide();
    ui->X_value_label->hide();
    ui->Y_label->hide();
    ui->Y_value_label->hide();

    //定时器 用于本次下发超时或反馈后，进行下次下发
    m_pIdentify = new QTimer(this);
    m_pIdentify->setInterval(4000);
    connect(m_pIdentify,SIGNAL(timeout()),this,SLOT(slotIdentifyTimeout()));

    //判断网络是否在线 重启SSE重连
    m_pDeviceOffline=new QTimer(this);
    m_pDeviceOffline->setInterval(10000);
    connect(m_pDeviceOffline,SIGNAL(timeout()),this,SLOT(slotDeviceOfflineTimeout()));

    //定时器 上报位置给1401K 绘制航迹
    m_pReportPosition=new QTimer(this);
    m_pReportPosition->setInterval(1000);
    connect(m_pReportPosition,SIGNAL(timeout()),this,SLOT(slotReportPositionTo1401K()));


   /* //创建双精度验证器，范围[-180.0,180.0] 保留6位小数
    QDoubleValidator *validator=new QDoubleValidator(-180.0,180.0,6,ui->Lon_value_label);//经度
    //设置输入格式
    validator->setNotation(QDoubleValidator::StandardNotation);
    validator->setLocale(QLocale::C);
    validator->setRange(-180.0,180.0,6);
    ui->Lon_value_label->setValidator(validator);
    QObject::connect(ui->Lon_value_label,&QLineEdit::textChanged,[=](){
        bool ok;
        double lon=ui->Lon_value_label->text().toDouble(&ok);
        if(!ok || (lon<-180.0 || lon>180.0)){
//            ui->Lon_value_label->clear();
            ui->Lon_value_label->setPlaceholderText(QString::fromLocal8Bit("请输入-180~180之间的数值"));
        }
    });*/


    // 正则表达式：允许 -180.0 ~ 180.0，最多6位小数
    QRegExp rx(R"(^-?(180(\.0{0,7})?|(1[0-7]\d|[1-9]\d?|0)(\.\d{1,7})?)$)");
    QRegExpValidator *validator = new QRegExpValidator(rx, ui->Lon_value_label);//经度
    ui->Lon_value_label->setValidator(validator);

    // 正则表达式：允许 -90.0 ~ 90.0，最多7位小数
    QRegExp rx1(R"(^-?(90(\.0{1,7})?|([0-8]?\d)(\.\d{1,7})?)$)");
    QRegExpValidator *validator1 = new QRegExpValidator(rx1, ui->Lat_value_label);//纬度
    ui->Lat_value_label->setValidator(validator1);


    QDoubleValidator *validator2=new QDoubleValidator(-9999999999999.0,999999999999.0,1,ui->Ele_value_label);//高程
    //设置输入格式
    validator2->setNotation(QDoubleValidator::StandardNotation);
    ui->Ele_value_label->setValidator(validator2);

    QRegExp rxNumber(QStringLiteral("^[0-9]{0,10}$"));
    m_targetNumberDefaultValidator = new QRegExpValidator(rxNumber, this);
    m_targetNumber002Validator = new TargetNumber002Validator(this);
    updateTargetNumberValidator();
    ui->Number_value_label->setDisabled(true);

//    ui->Time_value_label->setInputMask("99:99:99 99/99");

    loadQssStyleFile();
    loadPictureToUi("");

    ui->pushStatus_widget->hide();
    ui->widget_OCR->hide();
}

/**
 * @brief initCfg
 * 读取配置文件获取发送时的基础UI
 * @param void
 * @return 无返回值
 */
void OcrRecognitionWidget::initCfg()
{
    QSettings settings(QApplication::applicationDirPath() + "/config/communication.ini" , QSettings::IniFormat);
    settings.setIniCodec(QTextCodec::codecForName("GBK"));
    settings.beginGroup("Picture");
    m_strPicturePath = settings.value("path").toString();
    settings.endGroup();

    if(m_strPicturePath=="")
        m_strPicturePath =QCoreApplication::applicationDirPath()+"/"+"OCRPicture"+"/";

    settings.beginGroup("HTTP");
    m_strUrl = settings.value("url").toString();
    settings.endGroup();

    loadTargetNumberSerial();
}

/**
 * @brief initData
 * 插件数据初始化函数。
 * @param void
 * @return 无返回值
 */
void OcrRecognitionWidget::initData()
{
    DataMgr::getInstance().initData(m_pPlugin);
    connect(&DataMgr::getInstance(),SIGNAL(signalSendSSEInfo(QString,QString)),
            this,SLOT(slotSendSSEInfo(QString,QString)));
    setLineEditUseEdit(false);

    //给1401K发送数据 绘制航迹
    m_pBackendData = getBackendDataInstance(3838,QCoreApplication::applicationDirPath() + "/dds");//前端发送域ID
    if(m_pBackendData){
        m_pBackendData->registListener(this);
    }else{
        FrameLogManager::getInstance()->log(QString("OcrRecognition"),QString("m_pBackendData isNullptr id:%1").arg("32768"),IFrameLogManager::ERROR_TYPE);
    }

    //给1401K QB发送数据 绘制航迹
    m_pISimulatorData = getSimulatorDataInstance(1,QCoreApplication::applicationDirPath() + "/dds");//前端发送域ID
    if(m_pISimulatorData){
        m_pISimulatorData->registListener(this);
    }else{
        FrameLogManager::getInstance()->log(QString("OcrRecognition"),QString("m_pISimulatorData isNullptr id:%1").arg("1"),IFrameLogManager::ERROR_TYPE);
    }


}

/**
 * @brief resetQss
 * 切肤函数。
 * @param strStyleName
 * @return 无返回值
 */
void OcrRecognitionWidget::resetQss(const QString& strStyleName)
{
    //设置换肤路径
    m_pPlugin->setStyle(strStyleName);
    loadQssStyleFile();
}

/**
 * @brief registerProtocols
 * 虚函数的具体实现，初始化插件信号注册函数。
 * @param strStyleName
 * @return 无返回值
 */
void OcrRecognitionWidget::registerProtocols()
{
    std::vector<IUiPluginBase *> vPlugins = UiPluginFactory::getInstance()->getPlugins("TargetTable");
    foreach (IUiPluginBase * plugin, vPlugins) {
        connect(this,SIGNAL(signalSendOcrTargetInfo(TargetInfo)),
                plugin->getWidget(),SLOT(slotRecvOcrTargetInfo(TargetInfo)));
        connect(plugin->getWidget(),SIGNAL(signalTargetNum(int)),
                this,SLOT(slotRecvTargetNum(int)));
        connect(this,SIGNAL(signalSendTargetNum(int)),
                plugin->getWidget(),SLOT(slotRecvOCRTargetNum(int)));
    }

    vPlugins = UiPluginFactory::getInstance()->getPlugins("MessageBox");
    foreach (IUiPluginBase * plugin, vPlugins) {
        connect(m_pImageWidget,SIGNAL(signalMessageInfoText(QString,QString,MessageType)),
                plugin->getWidget(),SLOT(onMessageText(QString,QString,MessageType )));
        connect(this,SIGNAL(signalOcrMessageInfoText(QString,QString,MessageType)),
                plugin->getWidget(),SLOT(onMessageText(QString,QString,MessageType )));
    }

    vPlugins = UiPluginFactory::getInstance()->getPlugins("SignalTransit");
    foreach (IUiPluginBase * plugin, vPlugins) {
        connect(plugin->getWidget(),SIGNAL(signalPluginMsg(pluginmsg)),this,SLOT(slotPluginMsg(pluginmsg)));
        connect(this,SIGNAL(signalPluginMsg(pluginmsg)),plugin->getWidget(),SIGNAL(signalPluginMsg(pluginmsg)));//发送
    }

    vPlugins = UiPluginFactory::getInstance()->getPlugins("DeviceManagement");
    foreach (IUiPluginBase * plugin, vPlugins) {
       connect(plugin->getWidget(),SIGNAL(signalDeviceStatusToParameterSet(QString,int)),
                this,SLOT(slotFinishedForgetdeviceStatus(QString,int)));//从DeviceManagement插件接收获取设备状态
    }
}

/**
 * @brief loadQssStyleFile
 * 给予UI界面控件设置Qss图片样式。
 * @param void
 * @return 无返回值
 */
void OcrRecognitionWidget::loadQssStyleFile()
{
//    QString strPath = m_pPlugin->getStylePath() + QString("/OcrRecognitionWidget.qss");

//    Q_ASSERT(!strPath.isEmpty() && "path is empty");
//    QFile textFile(strPath);

//    Q_ASSERT(textFile.exists() && "file can't open");
//    textFile.open(QIODevice::ReadOnly | QIODevice::Text);
//    QTextStream textStream(&textFile);

//    QString style = textStream.readAll();
//    this->setStyleSheet(style);
}


/**
 * @brief setLineEditUseEdit
 * 设置QLineEdit是否可编辑
 * @param void(bStatus为true：可编辑 false:不可编辑)
 * @return 无返回值
 */
void OcrRecognitionWidget::setLineEditUseEdit(bool bStatus)
{
    if(bStatus)
    {
        ui->Number_value_label->setDisabled(false);
        ui->Type_comboBox->setDisabled(false);
        ui->Lon_value_label->setDisabled(false);
        ui->Lat_value_label->setDisabled(false);
        ui->Ele_value_label->setDisabled(false);
//        ui->Time_value_label->setDisabled(false);
        ui->X_value_label->setDisabled(false);
        ui->Y_value_label->setDisabled(false);
        if(ui->Lon_value_label->text()=="--"){
            ui->Lon_value_label->clear();
            ui->Lon_value_label->setPlaceholderText(QString::fromLocal8Bit("请输入-180~180之间的数值"));
        }

        if(ui->Lat_value_label->text()=="--"){
            ui->Lat_value_label->clear();
            ui->Lat_value_label->setPlaceholderText(QString::fromLocal8Bit("请输入-90~90之间的数值"));
        }


    }
    else
    {
        ui->Number_value_label->setDisabled(true);
        ui->Type_comboBox->setDisabled(true);
        ui->Lon_value_label->setDisabled(true);
        ui->Lat_value_label->setDisabled(true);
        ui->Ele_value_label->setDisabled(true);
        ui->Time_value_label->setDisabled(true);
        ui->X_value_label->setDisabled(true);
        ui->Y_value_label->setDisabled(true);
        if(ui->Lon_value_label->text()==""){
            ui->Lon_value_label->clear();
            ui->Lon_value_label->setText("--");
        }

        if(ui->Lat_value_label->text()==""){
            ui->Lat_value_label->clear();
            ui->Lat_value_label->setText("--");
        }

    }
}

/**
 * @brief loadPictureToUi
 * 加载图片信息至界面显示
 * @param strPictureName(图片名称)
 * @return 无返回值
 */
void OcrRecognitionWidget::loadPictureToUi(QString strPictureName)
{
    QString strPictureAllPath = strPictureName;
    if(m_pImageWidget)
    {
        m_pImageWidget->loadImage(strPictureAllPath);
        m_pImageWidget->show();
    }
}

/**
 * @brief setOcrTargetInfoToLineEdit
 * 将目标信息显示到界面
 * @param dData(目标详情信息)
 * @return 无返回值
 */
void OcrRecognitionWidget::setOcrTargetInfoToLineEdit(TargetInfo dData)
{
    ui->Number_value_label->setText(dData.targetNumber);
    ui->Type_comboBox->setCurrentIndex(dData.targetType);
    ui->Lat_value_label->setText(QString::number(dData.targetLat,'f',7));
    ui->Lon_value_label->setText(QString::number(dData.targetLon,'f',7));
    ui->Ele_value_label->setText(QString::number(dData.targerElevate,'f',1));
    ui->X_value_label->setText(QString::number(dData.X_Value,'f',1));
    ui->Y_value_label->setText(QString::number(dData.Y_Value,'f',1));
    ui->Time_value_label->setText(dData.targetTime);
    ui->textEdit_OCR->setText(dData.result);

    ui->widget_OCR->hide();
    ui->Modify_Btn->setChecked(false);
    setLineEditUseEdit(false);
}

/**
 * @brief XlhIsFull
 * 判断序列号是否已满
 * @param void
 * @return void
 */
void OcrRecognitionWidget::XlhIsFull()
{
    if (m_CurrentNumber >= 9999)
    {
        m_CurrentNumber = 0;
    }
}

void OcrRecognitionWidget::loadTargetNumberSerial()
{
    m_CurrentNumberProtocol = currentTargetNumberProtocol();
    QSettings settings(QApplication::applicationDirPath() + "/config/OcrRecognition.ini", QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("TargetNumber"));
    const QString serialKey = targetNumberSerialKey(m_CurrentNumberProtocol);
    const QString dateKey = targetNumberDateKey(m_CurrentNumberProtocol);
    m_CurrentNumber = settings.value(serialKey, 0).toUInt();
    m_CurrentNumberDate = settings.value(dateKey).toString().trimmed();
    settings.endGroup();
}

void OcrRecognitionWidget::saveTargetNumberSerial()
{
    if (m_CurrentNumberProtocol.isEmpty()) {
        m_CurrentNumberProtocol = currentTargetNumberProtocol();
    }
    QSettings settings(QApplication::applicationDirPath() + "/config/OcrRecognition.ini", QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("TargetNumber"));
    settings.setValue(targetNumberSerialKey(m_CurrentNumberProtocol), m_CurrentNumber);
    settings.setValue(targetNumberDateKey(m_CurrentNumberProtocol), m_CurrentNumberDate);
    settings.endGroup();
    settings.sync();
}

void OcrRecognitionWidget::syncTargetNumberProtocol()
{
    const QString protocol = currentTargetNumberProtocol();
    if (m_CurrentNumberProtocol != protocol) {
        loadTargetNumberSerial();
    }
}

void OcrRecognitionWidget::updateTargetNumberValidator()
{
    ui->Number_value_label->setValidator(currentTargetNumberProtocol() == QLatin1String("002")
                                         ? m_targetNumber002Validator
                                         : m_targetNumberDefaultValidator);
}

bool OcrRecognitionWidget::isTargetNumberValid(const QString &targetNumber) const
{
    const QString number = targetNumber.trimmed();
    if (number.isEmpty()) {
        return false;
    }

    bool ok = false;
    const qulonglong value = number.toULongLong(&ok);
    if (!ok) {
        return false;
    }
    if (currentTargetNumberProtocol() == QLatin1String("002")) {
        return value >= 1u && value <= 65535u;
    }
    return number.size() <= 10;
}

static QString todayTargetNumberDate()
{
    const QDate today = QDate::currentDate();
    return QStringLiteral("%1%2%3")
            .arg(today.year() - 2000, 2, 10, QChar('0'))
            .arg(today.month(), 2, 10, QChar('0'))
            .arg(today.day(), 2, 10, QChar('0'));
}

static QString todayTargetNumberPrefix()
{
    const QDate today = QDate::currentDate();
    return QStringLiteral("%1%2")
            .arg(today.month(), 2, 10, QChar('0'))
            .arg(today.day(), 2, 10, QChar('0'));
}

QString OcrRecognitionWidget::peekNextTargetNumber()
{
    syncTargetNumberProtocol();
    const QString today = todayTargetNumberDate();
    unsigned int next = 1;
    if (m_CurrentNumberDate == today) {
        if (m_CurrentNumber >= 9999u) {
            next = 1;
        } else {
            next = m_CurrentNumber + 1;
        }
    }
    if (m_CurrentNumberProtocol == QLatin1String("002")) {
        return QString::number(next);
    }
    return todayTargetNumberPrefix() + QString("%1").arg(next, 4, 10, QChar('0'));
}

void OcrRecognitionWidget::commitTargetNumberSerial()
{
    syncTargetNumberProtocol();
    const QString today = todayTargetNumberDate();
    if (m_CurrentNumberDate != today) {
        m_CurrentNumberDate = today;
        m_CurrentNumber = 1;
    } else {
        XlhIsFull();
        ++m_CurrentNumber;
    }
    saveTargetNumberSerial();
    emit signalSendTargetNum(static_cast<int>(m_CurrentNumber));
}

/**
 * @brief setInvalidData
 * 将UI界面显示的数据置为无效
 * @param void
 * @return void
 */
void OcrRecognitionWidget::setInvalidData()
{
    ui->Number_value_label->setText("--");
    ui->Lon_value_label->setText("--");
    ui->Lat_value_label->setText("--");
    ui->Ele_value_label->setText("--");
    QString CreateTime = QDateTime::currentDateTime().toString("yyyy/MM/dd hh:mm:ss");
    ui->Time_value_label->setText(CreateTime);
    ui->X_value_label->setText("--");
    ui->Y_value_label->setText("--");
    ui->textEdit_OCR->clear();

    setLineEditUseEdit(false);
}

/**
 * @brief showAndHidePushStatus
 * 显隐推流状态
 * @param bShow(true:显示推流状态 false:隐藏推流状态)
 * @return void
 */
void OcrRecognitionWidget::showAndHidePushStatus(bool bShow)
{
    if(bShow)
    {
        ui->Icon_widget->hide();
        ui->pushStatus_label->hide();
    }
    else
    {
        ui->Icon_widget->show();
        ui->pushStatus_label->show();
        ui->Icon_widget->raise();
        ui->pushStatus_label->raise();
    }
}

void OcrRecognitionWidget::OnMsgData(BackendData data)
{
    qDebug()<<"hhtttt111111111111111"<<data.ID;

}

void OcrRecognitionWidget::OnMsgData(SimulatorData data)
{
    qDebug()<<"hhtttt111111111111111"<<QString::fromStdString(data.json);
}


/**
 * @brief slotFinishedForgetdeviceStatus
 * 从DeviceManagement插件接收获取设备状态,用于sse重连
 * @param strData（反馈消息内容）
 * @return void
 */
void OcrRecognitionWidget::slotFinishedForgetdeviceStatus(QString strData, int statusCode)
{
    Q_UNUSED(strData);
    Q_UNUSED(statusCode);

    m_pDeviceOffline->start();
}

/**
 * @brief slotRecvTargetNum 接收TargetTable目标编号修改
 */
void OcrRecognitionWidget::slotRecvTargetNum(int num)
{
    syncTargetNumberProtocol();
    if (num > 0
            && (static_cast<unsigned int>(num) > m_CurrentNumber
                || (m_CurrentNumber >= 9999u && num == 1))) {
        m_CurrentNumber = static_cast<unsigned int>(num);
        m_CurrentNumberDate = todayTargetNumberDate();
        saveTargetNumberSerial();
    }
}

/**
 * @brief slotSwitchHttp 从ParameterSet插件接收http通信地址切换
 * @param address
 */
void OcrRecognitionWidget::slotSwitchHttp(QString address)
{
    DataMgr::getInstance().reconnectServer(address);
    m_strUrl=address;
}

void OcrRecognitionWidget::slotPositionSwitch(bool statu)
{
    qDebug()<<"kkkmm111111111111"<<statu;
    m_bTrackSwitch=statu;
    if(statu){
        dealTargetTrack(0,1,1,1);
        m_pReportPosition->start();
    }else{
        m_pReportPosition->stop();
    }
}


/**
 * @brief slotClose_Btn
 * 点击关闭按钮触发关闭弹窗
 * @param void
 * @return 无返回值
 */
void OcrRecognitionWidget::slotClose_Btn()
{
    setInvalidData();
    m_pImageWidget->clearShow();
    this->close();
}

/**
 * @brief slotModify_Btn
 * 点击目标信息编辑按钮
 * @param bStatus(true:目标界面信息可编辑 false:目标界面信息不可编辑)
 * @return 无返回值
 */
void OcrRecognitionWidget::slotModify_Btn(bool bStatus)
{
    if(bStatus)
    {
        updateTargetNumberValidator();
        setLineEditUseEdit(true);

        if (isEmptyTargetNumber(ui->Number_value_label->text())) {
            ui->Number_value_label->setText(peekNextTargetNumber());
        }

        ui->widget_OCR->show();
    }
    else
    {
        setLineEditUseEdit(false);
        ui->widget_OCR->hide();
    }
}

/**
 * @brief slotReport_Btn
 * 点击目标信息上报按钮
 * @param void
 * @return 无返回值
 */
void OcrRecognitionWidget::slotReport_Btn()
{
    syncTargetNumberProtocol();
    updateTargetNumberValidator();
    if (isEmptyTargetNumber(ui->Number_value_label->text())) {
        ui->Number_value_label->setText(peekNextTargetNumber());
    }

    if (!isTargetNumberValid(ui->Number_value_label->text())) {
        emit signalOcrMessageInfoText(QString::fromUtf8(u8"目标上报"),
                                      QString::fromUtf8(u8"002协议目标编号请输入1~65535之间的整数！"),
                                      MessageType_Warn);
        ui->Number_value_label->setFocus();
        ui->Number_value_label->selectAll();
        return;
    }

    if(isEmptyTargetNumber(ui->Number_value_label->text())||ui->Lat_value_label->text()=="--"||ui->Lon_value_label->text()=="--"||ui->Ele_value_label->text()=="--"){
        // 源文件固定为 UTF-8；不能依赖构建容器的 Windows 本地代码页，
        // 否则中文窄字符串会在编译阶段被替换成问号。
        emit signalOcrMessageInfoText(QString::fromUtf8(u8"警告"),
                                      QString::fromUtf8(u8"目标编号、经度、纬度、高程不能为空！"),
                                      MessageType_Info);
        return;
    }

    TargetInfo sMsg;
    sMsg.targetNumber = ui->Number_value_label->text();
    sMsg.targerElevate = ui->Ele_value_label->text().toDouble();
    sMsg.targetLat = ui->Lat_value_label->text().toDouble();
    sMsg.targetLon = ui->Lon_value_label->text().toDouble();
    sMsg.targetType = ui->Type_comboBox->currentIndex();
    sMsg.targetTime = ui->Time_value_label->text();
    sMsg.X_Value = ui->X_value_label->text().toDouble();
    sMsg.Y_Value = ui->Y_value_label->text().toDouble();

    dealTargetTrack(2,sMsg.targetLon,sMsg.targetLat,sMsg.targerElevate);
    // 将目标信息通过信号槽传至目标列表，然后上报到1501车长席位
    emit signalSendOcrTargetInfo(sMsg);
    // 同一个目标允许重复上报，但编号只在首次上报时占用一次。
    if (m_LastCommittedTargetNumber != sMsg.targetNumber
            || m_LastCommittedTargetProtocol != m_CurrentNumberProtocol) {
        commitTargetNumberSerial();
        m_LastCommittedTargetNumber = sMsg.targetNumber;
        m_LastCommittedTargetProtocol = m_CurrentNumberProtocol;
    }
}

/**
 * @brief slotDownloadCompleted
 * 图片下载完成
 * @param strPath(图片绝对路径)
 * @return 无返回值
 */
void OcrRecognitionWidget::slotDownloadCompleted(QString strPath)
{
    loadPictureToUi(strPath);
    bIdentify=true;
    m_pIdentify->stop();
}

/**
 * @brief slotloadFailed
 * 图片下载失败提示
 * @param strContent(提示内容)
 * @return 无返回值
 */
void OcrRecognitionWidget::slotloadFailed(QString strContent)
{
    bIdentify=true;
    m_pIdentify->stop();
    emit signalOcrMessageInfoText(QString::fromLocal8Bit("警告"),strContent,MessageType_Warn);
}

/**
 * @brief slotSendSSEInfo
 * 接收SSE推送的目标信息。
 * @param void
 * @return 无返回值
 */
void OcrRecognitionWidget::slotSendSSEInfo(QString strEvent,QString strData)
{
    Q_UNUSED(strEvent);
    FrameLogManager::getInstance()->log("OcrRecognitionWidget",strData,IFrameLogManager::DEBUG_TYPE);
//    qDebug()<<"OcrRecognitionWidget111111111111"<<strData;
    QTextCodec *tc = QTextCodec::codecForName("UTF-8");
    Q_UNUSED(tc);
    QJsonDocument root_Doc = QJsonDocument::fromJson(strData.toUtf8().data());
    if(root_Doc.isObject())
    {
        if(root_Doc.object().contains("status") || root_Doc.object().contains("message"))
        {
            return;
        }

        if((!root_Doc.object().contains(QString::fromLocal8Bit("目标经度"))) || (!root_Doc.object().contains(QString::fromLocal8Bit("目标纬度")))
                || (!root_Doc.object().contains(QString::fromLocal8Bit("目标高程"))) || (!root_Doc.object().contains("X"))
                || (!root_Doc.object().contains("Y")) || (!root_Doc.object().contains(QString::fromLocal8Bit("OCR结果"))) ){
            return;
        }
        QString CreateTime = QDateTime::currentDateTime().toString("yyyy/MM/dd hh:mm:ss");
        char TaregetType = ui->Type_comboBox->currentIndex();
        double TargetLon = root_Doc.object().value(QString::fromLocal8Bit("目标经度")).toDouble();
        double TargetLat = root_Doc.object().value(QString::fromLocal8Bit("目标纬度")).toDouble();
        double TargetElev = root_Doc.object().value(QString::fromLocal8Bit("目标高程")).toDouble();


        if(m_bTrackSwitch){
            dealTargetTrack(1,TargetLon,TargetLat,TargetElev);
        }

        if(m_bSignalPoint){
            QString TargetNumber = peekNextTargetNumber();
            double X_Value = root_Doc.object().value("X").toDouble();
            double Y_Value = root_Doc.object().value("Y").toDouble();

            TargetInfo tempData;
            tempData.targetNumber = TargetNumber;
            tempData.targetType = TaregetType;
            tempData.targetTime = CreateTime;
            tempData.targetLat = TargetLat;
            tempData.targetLon = TargetLon;
            tempData.targerElevate = TargetElev;
            tempData.X_Value = X_Value;
            tempData.Y_Value = Y_Value;

            QJsonObject data_obj=root_Doc.object();
            QString ocrText="";
            if(data_obj[QString::fromLocal8Bit("OCR结果")].isArray()){
                QJsonArray dataArray=data_obj[QString::fromLocal8Bit("OCR结果")].toArray();
                for(int i=0;i<dataArray.size();++i){
                    QJsonValue value=dataArray.at(i);
                    if(value.isString()){
                        ocrText+=value.toString();
                        ocrText+=";";
                    }
                }
            }

            tempData.result=ocrText;

            // 添加目标信息
            if(TargetNumber != "0")
            {
                setOcrTargetInfoToLineEdit(tempData);
            }
            m_bSignalPoint=false;
        }

    }
}

/**
 * @brief slotPluginMsg
 * 接收一级菜单下发的OCR识别消息
 * @param pMsg
 * @return 无返回值
 */
void OcrRecognitionWidget::slotPluginMsg(pluginmsg pMsg)
{
    if(pMsg.id == 1)
    {
        m_bSignalPoint=true;
        setInvalidData();
        m_pImageWidget->clearShow();
        this->show();
        emit signalIdentify(pMsg.id, pMsg.state);
    }
}

/**
 * @brief slotIdentify
 * 接收菜单消息目标识别开关
 * @param nType（菜单ID） nStatus(识别开关 1 关 2开)
 * @return void
 */
void OcrRecognitionWidget::slotIdentify(int nType, int nStatus)
{

    if(!bIdentify){
        return;
        emit signalOcrMessageInfoText(QString::fromUtf8(u8"提示"),
                                      QString::fromUtf8(u8"禁止操作频繁!"),
                                      MessageType_Info);
    }

    m_pIdentify->start();
    bIdentify=false;
    QJsonObject json;
    if(nType == 1)
    {
        if(nStatus == 1)
        {
            json[QString::fromLocal8Bit("OCR")] = true;
        }
        else if(nStatus == 2)
        {
            json[QString::fromLocal8Bit("OCR")] = false;
        }

        QString url = "http://" + m_strUrl + "/proxy/setting/ocrSwitch";

        QString strFileName = QDateTime::currentDateTime().toString("yyyy-MM-dd-hhmmss.jpg");

        initCfg();
        QString strSavePath = m_strPicturePath + strFileName;

        if(m_pImageDownloader)
        {
            m_pImageDownloader->downloadImage(url,json,strSavePath);
        }
    }


}

/**
 * @brief slotIdentifyTimeout 定时器 本次下发超时
 */
void OcrRecognitionWidget::slotIdentifyTimeout()
{
    bIdentify=true;
    m_pIdentify->stop();
}

/**
 * @brief slotDeviceOfflineTimeout 判断网络是否在线 重启SSE重连
 */
void OcrRecognitionWidget::slotDeviceOfflineTimeout()
{
    m_pDeviceOffline->stop();
    DataMgr::getInstance().connectServer();
}


/**
 * @brief slotZLMParPushStatus
 * 接收推流状态信息
 * @param status
 * @return 无返回值
 */
void OcrRecognitionWidget::slotZLMParPushStatus(int status)
{
    if(status != m_pushStatus)
    {
        switch (status) {
        case 0:
            ui->Icon_widget->setStyleSheet("{border-image: url(:/resource/noData.png);background:transparent;}");
            ui->pushStatus_label->setStyleSheet("font:22px;color:#ffffff;");
            ui->pushStatus_label->setText(QString::fromLocal8Bit("无视频流接入"));
            break;
        case 1:
            ui->Icon_widget->setStyleSheet("{border-image: url(:/resource/icon-勿拔插.png);background:transparent;}");
            ui->pushStatus_label->setStyleSheet("font:22px;color:#F6D94E;");
            ui->pushStatus_label->setText(QString::fromLocal8Bit("推流进行中，请勿插拔HDMI口"));
            break;
        case 2:
            ui->Icon_widget->setStyleSheet("{border-image: url(:/resource/icon-重新拔插.png);background:transparent;}");
            ui->pushStatus_label->setStyleSheet("font:22px;color:#FA2256;");
            ui->pushStatus_label->setText(QString::fromLocal8Bit("推流失败，重新插拔HDMI口"));
            break;
        default:
            ui->Icon_widget->setStyleSheet("{border-image: url(:/resource/noData.png);background:transparent;}");
            ui->pushStatus_label->setStyleSheet("font:22px;color:#ffffff;");
            ui->pushStatus_label->setText(QString::fromLocal8Bit("无视频流接入"));
            break;
        }
        m_pushStatus = status;
    }
}

/**
 * @brief slottextChanged 限制输入字节
 */
void OcrRecognitionWidget::slottextChanged()
{
    bool islimit=false;
    if(islimit) return;

    QString content=ui->textEdit_OCR->toPlainText();

    //字符长度判断
    if(content.length() <=MAX_CHARS)
        return;

    //开始截断
    islimit=true;
    //截断最大字符数
    QString validText=content.left(MAX_CHARS);
    //保存光标
    QTextCursor cursor=ui->textEdit_OCR->textCursor();
    int oldpos=cursor.position();
    //设置合法文本
    ui->textEdit_OCR->setPlainText(validText);
    //恢复光标（防止越界）
    oldpos=qMin(oldpos,validText.length());
    cursor.setPosition(oldpos);
    ui->textEdit_OCR->setTextCursor(cursor);

    islimit=false;
}

void OcrRecognitionWidget::slotReportPositionTo1401K()
{
    if(bIdentify){
//        setInvalidData();
//        m_pImageWidget->clearShow();
        emit signalIdentify(1, 1);
    }
}

/**
 * @brief dealTargetTrack 处理目标信息 上报1401K
 */
void OcrRecognitionWidget::dealTargetTrack(int cmd,double lon, double lat, double elev)
{
    qDebug() << __FUNCTION__;
   /*//JSON文件外层索引
    QJsonObject inputJson;

//    inputJson.insert("screen","1");
//    m_la=m_la+0.10001;
    inputJson["lon"]=lon;
    inputJson["lat"]=lat;
    inputJson["alt"]=elev;
    BackendData backData;
    backData.ID = cmd;
    QJsonDocument doc;
    doc.setObject(inputJson);
    QByteArray ba = doc.toJson(QJsonDocument::Compact);
    backData.DataJson = QString(ba).toStdString();
    if(m_pBackendData)
    {
        m_pBackendData->publishMessage(backData);
        FrameLogManager::getInstance()->log(QString("OcrRecognition"),QString::fromLocal8Bit("Send BackendData.ID:%1,BackendData.DataJson:%2")
                                            .arg(QString::number(backData.ID)).arg(QString::fromStdString(backData.DataJson)),IFrameLogManager::DEBUG_TYPE);
    }*/


     //JSON文件外层索引
    QJsonObject inputJson;
    QJsonObject payloadJson;
    QJsonObject positionGeoJson;
    QJsonObject positionLocalJson;
//     m_la=m_la+0.000001;
    positionGeoJson["latitude"]=lat;
    positionGeoJson["longitude"]=lon;
    positionGeoJson["altitude"]=elev;
    positionLocalJson["x"]=0.0;
    positionLocalJson["y"]=0.0;
    positionLocalJson["z"]=0.0;

    payloadJson["observerId"]=1;

    if(cmd==1){
        payloadJson["isStatic"]=false;
    }else{
        payloadJson["isStatic"]=true;
    }

    QDateTime dt = QDateTime::currentDateTime();
    payloadJson["timestamp"] = static_cast<double>(dt.toMSecsSinceEpoch()) / 1000.0;
    payloadJson["targetId"]="1001";
    payloadJson["positionGeo"]=positionGeoJson;
    payloadJson["positionLocal"]=positionLocalJson;
    payloadJson["targetFaction"]=QString::fromLocal8Bit("敌方");
    payloadJson["targetSpeed"]=0.0;

    inputJson["type"]="measurement";
    inputJson["payload"]=payloadJson;

    SimulatorData backData;
    QJsonDocument doc;
    doc.setObject(inputJson);
    QByteArray ba = doc.toJson(QJsonDocument::Compact);
    backData.json = QString(ba).toStdString();
    if(m_pISimulatorData)
    {
        m_pISimulatorData->publishMessage(backData);
        FrameLogManager::getInstance()->log(QString("OcrRecognition"),QString::fromLocal8Bit("Send SimulatorData.YID:%1,BackendData.DataJson:%2")
                                            .arg(QString::number(1)).arg(QString::fromStdString(backData.json)),IFrameLogManager::DEBUG_TYPE);
    }

}
