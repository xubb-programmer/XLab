#include "ScreenPushSetWidget.h"
#include "ui_ScreenPushSetWidget.h"
#include <QFile>
#include <QTextCodec>
#include <QTextStream>
#include <iostream>
#include <QComboBox>
#include <QIntValidator>
#include <QStyledItemDelegate>
#include <QSignalBlocker>
#include <QScopedValueRollback>
#include <QLineEdit>
#include <QSpinBox>
#include <QRegularExpressionValidator>
#include <QButtonGroup>
#include "../Comm/CommunicationIniUtils.h"
using namespace std;
ScreenPushSetWidget::ScreenPushSetWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ScreenPushSetWidget)
{
    ui->setupUi(this);
    // applicationDirPath 属于宿主进程；插件加载后仍指向 binr 下客户端 exe 所在目录。
    m_pSettings.reset(new QSettings(ScreenPushInstance::clientConfigPath(), QSettings::IniFormat));
    m_pSettings->setIniCodec(QTextCodec::codecForName("UTF-8"));
    QButtonGroup group;
    group.addButton(ui->radioButton_rtsp,0);
    group.addButton(ui->radioButton_rtmp,1);
    group.setExclusive(true);
    ui->lineEdit_url->setMaxLength(64);
    ui->comboBox_screen->setItemDelegate(new QStyledItemDelegate(ui->comboBox_screen));
    ui->lineEdit_device->setMaxLength(15);
    ui->lineEdit_device->setValidator(new QRegularExpressionValidator(
                                        QRegularExpression("[0-9.]{0,15}"), this));
}

ScreenPushSetWidget::~ScreenPushSetWidget()
{
    // 防止失焦回调访问已销毁的配置和界面对象
    disconnect(ui->lineEdit_device, nullptr, this, nullptr);
    delete ui;
}

/**
 * @brief initUi
 * 插件初始化界面
 * @param plugin（插件指针）
 * @return 无返回值
 */
void ScreenPushSetWidget::initUi(IUiPluginBase *plugin)
{
    m_pPlugin = plugin;
    m_pPlugin->setStyle("day");
    m_widthValidator = new QIntValidator(1, 1920, this);
    m_heightValidator = new QIntValidator(1, 1080, this);
    ui->lineEdit_width->setValidator(m_widthValidator);
    ui->lineEdit_height->setValidator(m_heightValidator);
    connect(ui->lineEdit_url,SIGNAL(textChanged(QString)),this,SLOT(sltUpdateOutPutUrl(QString)));
    connect(ui->lineEdit_width,SIGNAL(textChanged(QString)),this,SLOT(sltUpdateCaptureWidth(QString)));
    connect(ui->lineEdit_height,SIGNAL(textChanged(QString)),this,SLOT(sltUpdateCaptureHeight(QString)));
    connect(ui->lineEdit_fps,SIGNAL(textChanged(QString)),this,SLOT(sltUpdateCaptureFps(QString)));
    connect(ui->spinBox_posX,SIGNAL(valueChanged(int)),this,SLOT(sltUpdateCaptureX(int)));
    connect(ui->spinBox_posY,SIGNAL(valueChanged(int)),this,SLOT(sltUpdateCaptureY(int)));
    connect(ui->radioButton_rtsp,SIGNAL(clicked(bool)),this,SLOT(sltRtspClicked(bool)));
    connect(ui->radioButton_rtmp,SIGNAL(clicked(bool)),this,SLOT(sltRtmpClicked(bool)));
    connect(ui->lineEdit_fps,&QLineEdit::editingFinished,this,&ScreenPushSetWidget::checkFpsInputEvent);
    connect(ui->lineEdit_width,&QLineEdit::editingFinished,this,&ScreenPushSetWidget::checkWidthInputEvent);
    connect(ui->lineEdit_height,&QLineEdit::editingFinished,this,&ScreenPushSetWidget::checkHeightInputEvent);
    connect(ui->lineEdit_url,&QLineEdit::editingFinished,this,&ScreenPushSetWidget::checkUrlInputEvent);

    connect(ui->comboBox_screen,SIGNAL(activated(int)),this,SLOT(sltScreenActivated(int)));
    connect(ui->lineEdit_device,&QLineEdit::editingFinished,
            this,&ScreenPushSetWidget::checkDeviceInputEvent);
    //限制中文和空格，还有&符号
    QRegularExpression urlRegex("[\\x21-\\x25\\x27-\\x7E]*");
    ui->lineEdit_url->setValidator(new QRegularExpressionValidator(urlRegex,this));
    loadQssStyle();
}
/**
 * @brief loadQssStyle
 * 设置组件样式
 * @param void
 * @return 无返回值
 */
void ScreenPushSetWidget::loadQssStyle()
{
    QString strPath = m_pPlugin->getStylePath() + QString("/ScreenPushSetWidget.qss");
    Q_ASSERT(!strPath.isEmpty() && "path is empty");
    QFile textFile(strPath);

    Q_ASSERT(textFile.exists() && "file can't open");
    textFile.open(QIODevice::ReadOnly | QIODevice::Text);
    QTextStream textStream(&textFile);

    QString style = textStream.readAll();
    this->setStyleSheet(style);
}
/**
 * @brief updateStreamInfo
 * 读取配置文件，更新推流数据信息
 * @param void
 * @return 无返回值
 */
void ScreenPushSetWidget::updateStreamInfo()
{
    //获取参数
    m_pSettings.reset(new QSettings(ScreenPushInstance::clientConfigPath(), QSettings::IniFormat));
    m_pSettings->setIniCodec(QTextCodec::codecForName("UTF-8"));

    // 只把配置显示到界面上，不在这里写回配置（用户改了控件才写）
    m_loading = true;
    refreshScreenList();
    const QString targetDevice = getParaValue("ScreenPush","targetDevice").toString();
    m_deviceText = targetDevice.isEmpty() ? ScreenPushInstance::defaultDeviceIp() : targetDevice;
    ui->lineEdit_device->setText(m_deviceText);

    QString outputUrl = getParaValue("ScreenPush","streamName").toString();
    QString captureFps = getParaValue("ScreenPush","captureFps").toString();
    QString protocol = getParaValue("ScreenPush","protocol").toString();
    if(outputUrl.isEmpty()){
        outputUrl = QString("stream");
    }
    int clampedFps = qBound(15,captureFps.toInt(),100);
    ui->lineEdit_url->setText(outputUrl);  // 设置推流url
    ui->lineEdit_fps->setText(QString::number(clampedFps)); // 设置视频帧率
    if(protocol == "rtsp"){
        ui->radioButton_rtsp->setChecked(true);
    }else if (protocol == "rtmp") {
        ui->radioButton_rtmp->setChecked(true);
    }
    m_loading = false;
}

void ScreenPushSetWidget::sltUpdateOutPutUrl(QString text)
{
    setParaValue("ScreenPush","streamName",text);
}

void ScreenPushSetWidget::sltUpdateCaptureWidth(QString)
{
    saveCaptureSettings();
}

void ScreenPushSetWidget::sltUpdateCaptureHeight(QString)
{
    saveCaptureSettings();
}

void ScreenPushSetWidget::sltUpdateCaptureFps(QString text)
{
     setParaValue("ScreenPush","captureFps",text);
}

void ScreenPushSetWidget::sltUpdateCaptureX(int)
{
    saveCaptureSettings();
}

void ScreenPushSetWidget::sltUpdateCaptureY(int)
{
    saveCaptureSettings();
}

void ScreenPushSetWidget::sltRtspClicked(bool flag)
{
    if(flag){
        setParaValue("ScreenPush","protocol","rtsp");
    }
}

void ScreenPushSetWidget::sltRtmpClicked(bool flag)
{
    if(flag){
        setParaValue("ScreenPush","protocol","rtmp");
    }
}

void ScreenPushSetWidget::checkWidthInputEvent()
{
    if (m_capture.mode != CaptureMode::Custom)
        return;
    bool isOK;
    int num = ui->lineEdit_width->text().toInt(&isOK);
    if(num % 2 != 0){
        num++;
        ui->lineEdit_width->setText(QString::number(num));
    }
}

void ScreenPushSetWidget::checkHeightInputEvent()
{
    if (m_capture.mode != CaptureMode::Custom)
        return;
    bool isOK;
    int num = ui->lineEdit_height->text().toInt(&isOK);
    if(num % 2 != 0){
        num++;
        ui->lineEdit_height->setText(QString::number(num));
    }
}

void ScreenPushSetWidget::checkUrlInputEvent()
{
    QString urlInput = ui->lineEdit_url->text();
    if(urlInput.isEmpty()){
        ui->lineEdit_url->setText(QString("stream"));
    }
}

void ScreenPushSetWidget::checkFpsInputEvent()
{
    int fpsInput = ui->lineEdit_fps->text().toInt();
    int clampedFps = qBound(15,fpsInput,100);
    ui->lineEdit_fps->setText(QString::number(clampedFps));
}

QVariant ScreenPushSetWidget::getParaValue(QString groupName, QString paraKey)
{
    QString name_key = groupName + "/" + paraKey;
    return m_pSettings->value(name_key);
}

void ScreenPushSetWidget::setParaValue(QString groupName, QString paraKey, QString value)
{
    if (m_loading || !m_pSettings)
        return;
    QString name_key = groupName + "/" + paraKey;
    m_pSettings->setValue(name_key,value);
    CommunicationIniUtils::syncPreservingPullStreamKeys(*m_pSettings);
}

void ScreenPushSetWidget::sltScreenActivated(int index)
{
    if (m_loading || index < 0 || index > m_screens.size() + 1)
        return;

    CaptureConfig config;
    config.hasCustomRegion = true;
    config.fullScreen = (index == 0);
    config.customRegion = (index == m_screens.size() + 1);
    config.screenName = index > 0 && index <= m_screens.size()
            ? m_screens.at(index - 1).name : m_capture.screenName;
    // 切到自定义时沿用切换前显示的值
    config.region.width = ui->lineEdit_width->text().toInt();
    config.region.height = ui->lineEdit_height->text().toInt();
    config.region.x = ui->spinBox_posX->value();
    config.region.y = ui->spinBox_posY->value();
    m_capture = ScreenPushInstance::resolveCapture(m_screens, config);
    applyCaptureSelection();
    saveCaptureSettings();
}


void ScreenPushSetWidget::checkDeviceInputEvent()
{
    if (m_loading || m_savingDevice || !m_pSettings)
        return;
    // 程序回填的值不因失焦而保存
    if (!ui->lineEdit_device->isModified() && ui->lineEdit_device->text() == m_deviceText)
        return;
    // 提示框抢焦点时，editingFinished 可能重入
    const QScopedValueRollback<bool> saving(m_savingDevice, true);
    const QString text = ui->lineEdit_device->text().trimmed();
    const QString ip = ScreenPushInstance::extractIp(text);
    const QRegularExpression completeIp("^[0-9]{1,3}(\\.[0-9]{1,3}){3}$");
    if (ip.isEmpty() || !completeIp.match(text).hasMatch()) {
        ui->lineEdit_device->setText(m_deviceText);
        emit signalMessageInfoText(QString::fromUtf8(u8"桌面推流"),
                                   QString::fromUtf8(u8"请输入正确的设备 IP 地址，例如 192.168.10.130"), MessageType_Warn);
        return;
    }

    setParaValue("ScreenPush","targetDevice", ip == ScreenPushInstance::defaultDeviceIp() ? QString() : ip);
    m_deviceText = ip;
    ui->lineEdit_device->setText(m_deviceText);
}

void ScreenPushSetWidget::refreshScreenList()
{
    m_screens = ScreenPushInstance::captureScreens();
    m_capture = ScreenPushInstance::resolveCapture(m_screens, ScreenPushInstance::readCaptureConfig(*m_pSettings));

    QSignalBlocker blocker(ui->comboBox_screen);
    ui->comboBox_screen->clear();
    const QRect desktop = ScreenPushInstance::desktopRect(m_screens);
    ui->comboBox_screen->addItem(QString::fromUtf8(u8"全屏（所有屏幕）%1×%2")
                                .arg(desktop.width()).arg(desktop.height()));
    for (int i = 0; i < m_screens.size(); ++i)
        ui->comboBox_screen->addItem(ScreenPushInstance::screenLabel(m_screens, i), m_screens.at(i).name);
    ui->comboBox_screen->addItem(QString::fromUtf8(u8"自定义"));
    int index = 0;
    if (m_capture.mode == CaptureMode::Custom)
        index = m_screens.size() + 1;
    else if (m_capture.mode == CaptureMode::Screen)
        index = ScreenPushInstance::findScreen(m_screens, m_capture.screenName) + 1;
    ui->comboBox_screen->setCurrentIndex(index);
    applyCaptureSelection();
    // 老配置的奇数宽高只在界面补偶数，打开页面不写回
    checkWidthInputEvent();
    checkHeightInputEvent();
}


void ScreenPushSetWidget::applyCaptureSelection()
{
    const QSignalBlocker widthBlocker(ui->lineEdit_width);
    const QSignalBlocker heightBlocker(ui->lineEdit_height);
    const QSignalBlocker xBlocker(ui->spinBox_posX);
    const QSignalBlocker yBlocker(ui->spinBox_posY);
    const QRect desktop = ScreenPushInstance::desktopRect(m_screens);
    if (m_widthValidator)
        m_widthValidator->setTop(desktop.width());
    if (m_heightValidator)
        m_heightValidator->setTop(desktop.height());
    ui->spinBox_posX->setRange(0, qMax(0, desktop.width() - 1));
    ui->spinBox_posY->setRange(0, qMax(0, desktop.height() - 1));
    ui->lineEdit_width->setText(QString::number(m_capture.region.width));
    ui->lineEdit_height->setText(QString::number(m_capture.region.height));
    ui->spinBox_posX->setValue(m_capture.region.x);
    ui->spinBox_posY->setValue(m_capture.region.y);

    const bool editable = m_capture.mode == CaptureMode::Custom;
    ui->lineEdit_width->setEnabled(editable);
    ui->lineEdit_height->setEnabled(editable);
    ui->spinBox_posX->setEnabled(editable);
    ui->spinBox_posY->setEnabled(editable);
    const QString style = editable ? QStringLiteral("color:#ffffff") : QStringLiteral("color:gray");
    ui->lineEdit_width->setStyleSheet(style);
    ui->lineEdit_height->setStyleSheet(style);
    ui->spinBox_posX->setStyleSheet(style);
    ui->spinBox_posY->setStyleSheet(style);
}

void ScreenPushSetWidget::saveCaptureSettings()
{
    if (m_loading || !m_pSettings)
        return;
    m_capture.region.width = ui->lineEdit_width->text().toInt();
    m_capture.region.height = ui->lineEdit_height->text().toInt();
    m_capture.region.x = ui->spinBox_posX->value();
    m_capture.region.y = ui->spinBox_posY->value();
    ScreenPushInstance::writeCaptureConfig(*m_pSettings, m_capture);
    CommunicationIniUtils::syncPreservingPullStreamKeys(*m_pSettings);
}
