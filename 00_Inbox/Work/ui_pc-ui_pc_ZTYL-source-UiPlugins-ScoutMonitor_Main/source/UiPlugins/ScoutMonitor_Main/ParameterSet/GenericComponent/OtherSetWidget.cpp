#include "OtherSetWidget.h"
#include "ui_OtherSetWidget.h"
#include <QFile>
#include <QTextCodec>
#include <QRegularExpression>
#include <QDir>
#include <QFileDialog>
#include <QJsonObject>
#include <QJsonDocument>
#include <QCoreApplication>
#include <QMap>
#include "../Comm/CommunicationIniUtils.h"

OtherSetWidget::OtherSetWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::OtherSetWidget)
{
    ui->setupUi(this);
}

OtherSetWidget::~OtherSetWidget()
{
    delete ui;
}

/**
 * @brief initUi
 * 插件初始化界面
 * @param plugin（插件指针）
 * @return 无返回值
 */
void OtherSetWidget::initUi(IUiPluginBase *plugin)
{
    m_pPlugin = plugin;
    m_pPlugin->setStyle("day");


    // 初始化配置
    initConfig();

    // 解析HTTP URL
    QString httpIp = extractIpFromUrl(m_httpUrl);

    // 解析视频输出URL
    UrlComponents videoUrlComponents = parseVideoUrl(m_outputUrl);

    // 设置UI控件
    ui->lineEdit_http->setText(httpIp);
    ui->lineEdit_ocr->setText(m_ocrPath);
    ui->lineEdit_SnaoShot->setText(m_snapshotPath);
    ui->lineEdit_Record->setText(m_recordPath);
    ui->lineEdit_targetIP->setText(m_czIp);
    ui->lineEdit_targetPort->setText(QString::number(m_czPort));
    ui->lineEdit_videoIP->setText(videoUrlComponents.ip);
    ui->lineEdit_videoPort->setText(videoUrlComponents.port);

    ui->comboBox_Car->clear();
    for (const CarInfo &carInfo : m_carList) {
        ui->comboBox_Car->addItem(carInfo.name);
    }
    if (findCarIndexByName(m_carType) >= 0) {
        ui->comboBox_Car->setCurrentText(m_carType);
    } else if (!m_carList.isEmpty()) {
        ui->comboBox_Car->setCurrentIndex(0);
        m_carType = m_carList.first().name;
    }


    ui->lineEdit_ocr->setDisabled(true);
    ui->lineEdit_SnaoShot->setDisabled(true);
    ui->lineEdit_Record->setDisabled(true);

    //输入限制
    QIntValidator *pValidator=new QIntValidator(0,99999,this);
    ui->lineEdit_targetPort->setValidator(pValidator);
    ui->lineEdit_videoPort->setValidator(pValidator);

    // 2. IP地址：使用正则表达式验证
    // IP地址正则表达式：0-255.0-255.0-255.0-255
    QString ipPattern = QStringLiteral("^((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}"
                                       "(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$");
    QRegExp ipRegex(ipPattern);
    QRegExpValidator *ipValidator = new QRegExpValidator(ipRegex, this);
    ui->lineEdit_targetIP->setValidator(ipValidator);
    ui->lineEdit_videoIP->setValidator(new QRegExpValidator(ipRegex, this));
    ui->lineEdit_http->setValidator(new QRegExpValidator(ipRegex, this));

    connect(ui->pushButton_http, &QPushButton::clicked, this, &OtherSetWidget::slotHttpSetClicked);
    connect(ui->pushButton_ocr, &QPushButton::clicked, this, &OtherSetWidget::slotOcrSetClicked);
    connect(ui->pushButton_SnaoShot, &QPushButton::clicked, this, &OtherSetWidget::slotSnaoShotSetClicked);
    connect(ui->pushButton_Record, &QPushButton::clicked, this, &OtherSetWidget::slotRecordSetClicked);
    connect(ui->pushButton_Target, &QPushButton::clicked, this, &OtherSetWidget::slotTargetSetClicked);
    connect(ui->pushButton_video, &QPushButton::clicked,this, &OtherSetWidget::slotVideoSetClicked);
    connect(ui->pushButton_videoReport, &QPushButton::clicked,
            this, &OtherSetWidget::slotVideoReportClicked);
    connect(ui->comboBox_Car, SIGNAL(currentIndexChanged(int)),this, SLOT(slotCarSelect(int)));

//    loadQssStyle();
}

/**
 * @brief initConfig
 * 插件初始化配置
 * @return 无返回值
 */
void OtherSetWidget::initConfig()
{
    QString configDir = QCoreApplication::applicationDirPath() + "/config/";
    //确保目录存在
    QDir dir(configDir);
    if(!dir.exists())
        dir.mkpath(configDir);

    // 初始化通信配置文件
    m_pSettingsCommunication=std::make_unique<QSettings>(configDir + "communication.ini", QSettings::IniFormat);
    m_pSettingsCommunication->setIniCodec(QTextCodec::codecForName("UTF-8"));

    // 初始化视频处理配置文件
    m_pSettingsVideoProcess=std::make_unique<QSettings>(configDir + "VideoProcess.ini", QSettings::IniFormat);
    m_pSettingsVideoProcess->setIniCodec(QTextCodec::codecForName("UTF-8"));

    // 从communication.ini读取参数
    m_httpUrl = getParaValue("HTTP", "url").toString();
    if(m_httpUrl.isEmpty() || m_httpUrl=="Null")
        m_httpUrl="192.168.0.100:3000";

    m_czIp = getParaValue("Address", "CzIp").toString();
    if(m_czIp.isEmpty() || m_czIp=="Null")
        m_czIp="192.168.0.5";

    m_czPort = getParaValue("Address", "CzPort").toInt();
    if(m_czPort==0)
        m_czPort=6100;

    m_carType = getParaValue("Address", "cartype").toString();

    loadCarConfig();

    if (m_carType.isEmpty() || m_carType == "Null") {
        if (!m_carList.isEmpty()) {
            m_carType = m_carList.first().name;
        }
    } else {
        bool ok = false;
        const int legacyIndex = m_carType.toInt(&ok);
        if (ok && legacyIndex >= 0 && legacyIndex < m_carList.size()) {
            m_carType = m_carList.at(legacyIndex).name;
        }
    }

    m_ocrPath = getParaValue("Picture", "path").toString();
    if(m_ocrPath.isEmpty() || m_ocrPath=="Null")
        m_ocrPath=QCoreApplication::applicationDirPath()+"/"+"OCRPicture";

    // 从VideoProcess.ini读取参数
    m_outputUrl = getParaValue("Video", "outputUrl").toString();
    if(m_outputUrl.isEmpty() || m_outputUrl=="Null")
        m_outputUrl="udp://10.1.2.12:5703?pkt_size=1316";

    m_inputUrl = getParaValue("Video", "inputUrl").toString();
    if(m_inputUrl.isEmpty() || m_inputUrl=="Null")
        m_inputUrl="rtmp://192.168.0.100:1935/live/stream";

    m_snapshotPath = getParaValue("SnapShot", "path").toString();
    if(m_snapshotPath.isEmpty() || m_snapshotPath=="Null")
        m_snapshotPath=QCoreApplication::applicationDirPath()+"/"+"Picture";

    m_recordPath = getParaValue("Record", "path").toString();
    if(m_recordPath.isEmpty() || m_recordPath=="Null")
        m_recordPath=QCoreApplication::applicationDirPath()+"/"+"Video";


}

/**
 * @brief loadCarConfig 从communication.ini读取[Car]配置
 * Car0/Car0Ip/Car0Port 为一组，依次读取所有车辆配置
 */
void OtherSetWidget::loadCarConfig()
{
    m_carList.clear();
    if (!m_pSettingsCommunication) {
        return;
    }

    m_pSettingsCommunication->beginGroup("Car");
    const QStringList keys = m_pSettingsCommunication->childKeys();
    QRegularExpression carNameRegex(QStringLiteral("^Car(\\d+)$"));
    QMap<int, CarInfo> carMap;

    for (const QString &key : keys) {
        const QRegularExpressionMatch match = carNameRegex.match(key);
        if (!match.hasMatch()) {
            continue;
        }

        const int index = match.captured(1).toInt();
        CarInfo carInfo;
        carInfo.name = m_pSettingsCommunication->value(key).toString();
        carInfo.ip = m_pSettingsCommunication->value(QStringLiteral("Car%1Ip").arg(index)).toString();
        carInfo.port = m_pSettingsCommunication->value(QStringLiteral("Car%1Port").arg(index)).toInt();
        carMap.insert(index, carInfo);
    }
    m_pSettingsCommunication->endGroup();

    for (auto it = carMap.constBegin(); it != carMap.constEnd(); ++it) {
        m_carList.append(it.value());
    }
}

int OtherSetWidget::findCarIndexByName(const QString &name) const
{
    for (int i = 0; i < m_carList.size(); ++i) {
        if (m_carList.at(i).name == name) {
            return i;
        }
    }
    return -1;
}
/**
 * @brief loadQssStyle
 * 设置组件样式
 * @param void
 * @return 无返回值
 */
void OtherSetWidget::loadQssStyle()
{
    QString strPath = m_pPlugin->getStylePath() + QString("/OtherSetWidget.qss");
    Q_ASSERT(!strPath.isEmpty() && "path is empty");
    QFile textFile(strPath);

    Q_ASSERT(textFile.exists() && "file can't open");
    textFile.open(QIODevice::ReadOnly | QIODevice::Text);
    QTextStream textStream(&textFile);

    QString style = textStream.readAll();
    this->setStyleSheet(style);
}

/**
 * @brief slotHttpSetClicked
 * HTTP通信地址配置
 * @param void
 * @return void
 */
void OtherSetWidget::slotHttpSetClicked(bool)
{
    if(!ui->lineEdit_http->text().isEmpty()){
        // emit signalMessageInfoText(QString::fromUtf8(u8"其他设置"),
        //                            QString::fromUtf8(u8"地址设置成功!"),
        //                            MessageType_Success);

        setParaValue("HTTP","url",ui->lineEdit_http->text()+":"+m_httpUrlPort);
        m_inputUrl = getParaValue("Video", "inputUrl").toString();
        if(!m_inputUrl.isEmpty()){
            QRegularExpression regex("//.*?:");//匹配替换//与：之间字符
            m_inputUrl.replace(regex,QString::fromLocal8Bit("//")+ui->lineEdit_http->text()+QString::fromLocal8Bit(":"));
        }
        setParaValue("Video","inputUrl",m_inputUrl);
        m_httpUrl=ui->lineEdit_http->text();

        QString temphttp=ui->lineEdit_http->text()+":"+m_httpUrlPort;
        emit signalSwitchHttp(temphttp);

        // 通信地址切换由 ParaContentWidget 统一生成并下发新设备的视频输入流地址。
        // 这里不能再下发“旧播放路径 + 新 IP”，否则会覆盖自动切流结果。
//        emit signalStreamPullAdress(m_inputUrl);

    }else{
        emit signalMessageInfoText(QString::fromUtf8(u8"其他设置"),
                                   QString::fromUtf8(u8"IP地址不能为空!"),
                                   MessageType_Info);
    }
}

/**
 * @brief slotOcrSetClicked
 * OCR截图路径配置
 * @param void
 * @return void
 */
void OtherSetWidget::slotOcrSetClicked(bool)
{
    // 获取当前路径作为初始目录
    QString currentPath = ui->lineEdit_ocr->text();
    if (currentPath.isEmpty() || !QDir(currentPath).exists()) {
        // 如果当前路径无效，使用默认路径或应用程序目录
        currentPath = QCoreApplication::applicationDirPath();
    }

    // 使用QFileDialog选择文件夹
    QString newPath = selectPath(currentPath);

    // 如果用户选择了目录
    if (!newPath.isEmpty()) {

        // 更新UI显示
        ui->lineEdit_ocr->setText(newPath);

        // 保存到配置
        setParaValue("Picture", "path", newPath);
        m_ocrPath = newPath;

        // 发送成功消息
        emit signalMessageInfoText(QString::fromUtf8(u8"其他设置"),
                                   QString::fromUtf8(u8"路径设置成功!"),
                                   MessageType_Success);
    }
}

/**
 * @brief slotSnaoShotSetClicked
 * 视频截图路径配置
 * @param void
 * @return void
 */
void OtherSetWidget::slotSnaoShotSetClicked(bool)
{
    // 获取当前路径作为初始目录
    QString currentPath = ui->lineEdit_SnaoShot->text();
    if (currentPath.isEmpty() || !QDir(currentPath).exists()) {
        // 如果当前路径无效，使用默认路径或应用程序目录
        currentPath = QCoreApplication::applicationDirPath();
    }

    // 使用QFileDialog选择文件夹
    QString newPath = selectPath(currentPath);

    // 如果用户选择了目录
    if (!newPath.isEmpty()) {

        // 更新UI显示
        ui->lineEdit_SnaoShot->setText(newPath);

        // 保存到配置
        setParaValue("SnapShot", "path", newPath);
        m_snapshotPath = newPath;

        // 发送成功消息
        emit signalMessageInfoText(QString::fromUtf8(u8"其他设置"),
                                   QString::fromUtf8(u8"路径设置成功!"),
                                   MessageType_Success);
    }
}

/**
 * @brief slotRecordSetClicked
 * 视频录像路径配置
 * @param void
 * @return void
 */
void OtherSetWidget::slotRecordSetClicked(bool)
{
    // 获取当前路径作为初始目录
    QString currentPath = ui->lineEdit_Record->text();
    if (currentPath.isEmpty() || !QDir(currentPath).exists()) {
        // 如果当前路径无效，使用默认路径或应用程序目录
        currentPath = QCoreApplication::applicationDirPath();
    }

    // 使用QFileDialog选择文件夹
    QString newPath = selectPath(currentPath);

    // 如果用户选择了目录
    if (!newPath.isEmpty()) {

        // 更新UI显示
        ui->lineEdit_Record->setText(newPath);

        // 保存到配置
        setParaValue("Record", "path", newPath);

        // 发送成功消息
        emit signalMessageInfoText(QString::fromUtf8(u8"其他设置"),
                                   QString::fromUtf8(u8"路径设置成功!"),
                                   MessageType_Success);
    }
}

/**
 * @brief slotTargetSetClicked
 * 目标上报地址配置
 * @param void
 * @return void
 */
void OtherSetWidget::slotTargetSetClicked(bool)
{
    const QString dialogTitle = QString::fromUtf8(u8"其他设置");
    // const QString successText = QString::fromUtf8(u8"地址设置成功!");
    const QString emptyText = QString::fromUtf8(u8"IP地址或端口不能为空!");
    if(!ui->lineEdit_targetIP->text().isEmpty() && !ui->lineEdit_targetPort->text().isEmpty()){
        m_czIp=ui->lineEdit_targetIP->text();
        m_czPort=ui->lineEdit_targetPort->text().toInt();
        setParaValue("Address","CzIp",m_czIp);
        setParaValue("Address","CzPort",QString::number(m_czPort));
        m_carType=ui->comboBox_Car->currentText().trimmed();
        setParaValue("Address","cartype",m_carType);

        emit signalTargetReportAddress(m_czIp,m_czPort);

        // emit signalMessageInfoText(dialogTitle, successText, MessageType_Success);
    }else{
        emit signalMessageInfoText(dialogTitle, emptyText, MessageType_Info);
    }
}

/**
 * @brief slotVideoSetClicked
 * 视频上报地址配置
 * @param void
 * @return void
 */
void OtherSetWidget::slotVideoSetClicked(bool)
{
    if(!ui->lineEdit_videoIP->text().isEmpty() && !ui->lineEdit_videoPort->text().isEmpty()){
        QString ip=ui->lineEdit_videoIP->text();
        QString port=ui->lineEdit_videoPort->text();
        m_outputUrl=QString::fromLocal8Bit("udp://%1:%2?pkt_size=1316").arg(ip).arg(port);
        setParaValue("Video","outputUrl",m_outputUrl);
        // emit signalMessageInfoText(
        //             QString::fromUtf8(u8"其他设置"),
        //             QString::fromUtf8(u8"地址设置成功!"),
        //             MessageType_Success);
    }else{
        emit signalMessageInfoText(
                    QString::fromUtf8(u8"其他设置"),
                    QString::fromUtf8(u8"IP地址或端口不能为空!"),
                    MessageType_Info);
    }
}

void OtherSetWidget::slotVideoReportClicked(bool)
{
    // 状态值和原底部“视频上报”按钮保持一致：1启动，2停止。
    emit signalVideoReportControl(m_videoReportState);
}

void OtherSetWidget::slotVideoReportStateChanged(int state)
{
    if (state != 1 && state != 2) {
        return;
    }

    m_videoReportState = state;
    const QString reportText = QString::fromUtf8(u8"上报");
    const QString stopReportText = QString::fromUtf8(u8"停止");
    ui->pushButton_videoReport->setText(state == 2
                                        ? stopReportText
                                        : reportText);
}

/**
 * @brief slotCarSelect 车辆选择
 */
void OtherSetWidget::slotCarSelect(int index)
{
    m_carType = ui->comboBox_Car->currentText().trimmed();
    if (index >= 0 && index < m_carList.size()) {
        const CarInfo &carInfo = m_carList.at(index);
        ui->lineEdit_targetIP->setText(carInfo.ip);
        ui->lineEdit_targetPort->setText(QString::number(carInfo.port));
    }
}

/**
 * @brief slotcurrentDeviceIp
 * 接收设备选择 发送的IP信息
 * @return void
 */
void OtherSetWidget::slotcurrentDeviceIp(QString ip)
{
    if(ip!=""/*&& ip!=ui->lineEdit_http->text().trimmed()*/){
        ui->lineEdit_http->setText(ip.trimmed());
        slotHttpSetClicked(true);
    }
}

/**
 * @brief 从URL中提取IP地址
 * @param url URL字符串
 * @return 提取的IP地址，如果提取失败则返回空字符串
 */
QString OtherSetWidget::extractIpFromUrl(const QString &url)
{
    if (url.isEmpty()) {
        return QString();
    }

    QStringList parts = url.split(":");
    if (parts.size() > 1) {
        if(parts.at(1)!=""){
            m_httpUrlPort=parts.at(1);
        }
        return parts.at(0);
    }

    return QString();
}

/**
 * @brief 解析视频URL
 * @param url 视频URL字符串
 * @return 包含IP和端口的结构体
 */
UrlComponents OtherSetWidget::parseVideoUrl(const QString &url)
{
    UrlComponents components;

    if (url.isEmpty()) {
        return components;
    }

    // 查找协议分隔符
    int protocolStart = url.indexOf("//");
    if (protocolStart == -1) {
        return components;
    }

    // 查找IP和端口之间的分隔符
    int portSeparator = url.indexOf(":", protocolStart + 2);
    if (portSeparator == -1) {
        return components;
    }

    // 提取IP部分
    components.ip = url.mid(protocolStart + 2, portSeparator - protocolStart - 2);

    // 查找端口结束位置
    int portEnd = url.indexOf("?", portSeparator);
    if (portEnd == -1) {
        portEnd = url.length();
    }

    // 提取端口部分
    components.port = url.mid(portSeparator + 1, portEnd - portSeparator - 1);

    return components;
}

/**
 * @brief 获取配置参数值
 * @param groupName 组名
 * @param paraKey 参数键
 * @return 参数值，如果不存在则返回"Null"
 */
QVariant OtherSetWidget::getParaValue(QString groupName, QString paraKey)
{
    QString nameKey = groupName + "/" + paraKey;
    QSettings* settings=nullptr;

    // 确定使用哪个配置文件
    if (groupName == "HTTP" || groupName == "Address" || groupName == "Picture" || groupName == "Car") {
        settings = m_pSettingsCommunication.get();
    } else {
        settings = m_pSettingsVideoProcess.get();
    }

    if (!settings || !settings->contains(nameKey)) {
        return QVariant("Null");
    }

    return settings->value(nameKey);

}

/**
 * @brief 设置配置参数值
 * @param groupName 组名
 * @param paraKey 参数键
 * @param value 参数值
 */
void OtherSetWidget::setParaValue(QString groupName, QString paraKey, QString value)
{
    QString nameKey = groupName + "/" + paraKey;
     QSettings* settings=nullptr;

    // 确定使用哪个配置文件
    if (groupName == "HTTP" || groupName == "Address" || groupName == "Picture" || groupName == "Car") {
        settings = m_pSettingsCommunication.get();
    } else {
        settings = m_pSettingsVideoProcess.get();
    }

    if (settings) {
        settings->setValue(nameKey, value);
        if (settings == m_pSettingsCommunication.get())
            CommunicationIniUtils::syncPreservingPullStreamKeys(*settings);
        else
            settings->sync(); // 确保写入磁盘
    }
}

QString OtherSetWidget::selectPath(QString path)
{

    // 使用QFileDialog选择文件夹
    QString dirPath = QFileDialog::getExistingDirectory(
                this,
                QString::fromUtf8(u8"选择保存目录"),
                path,
                QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
                );

    // 如果用户选择了目录（没有点击取消）
    if (!dirPath.isEmpty()) {
        // 确保路径以分隔符结尾
        if (!dirPath.endsWith(QDir::separator())) {
            dirPath += QDir::separator();
        }

        /*// 可选：尝试创建目录（如果不存在）
        QDir dir(dirPath);
        if(!dir.exists()) {
            if(dir.mkpath(dirPath)){
                emit signalMessageInfoText(QString::fromLocal8Bit("目录创建"), QString::fromLocal8Bit("目录创建成功"), MessageType_Info);
            }else{
                emit signalMessageInfoText(QString::fromLocal8Bit("目录创建"), QString::fromLocal8Bit("目录创建失败"), MessageType_Info);
            }
        }*/

        dirPath.replace('\\','/');
        if(!dirPath.endsWith('/'))
            dirPath+='/';
        return dirPath;
    } else {
        // 用户取消了选择
//        emit signalMessageInfoText("OCR路径设置", "未选择目录", MessageType_Info);
        return "";
    }

}


void OtherSetWidget::sendParaValue()
{
//    QJsonObject jObj;
//    jObj["httpip"]=m_httpUrl; //http通信地址
//    jObj["inputUrl"]=m_inputUrl;//拉流视频地址
//    jObj["ocrPath"]=m_ocrPath; //OCR图片存储路径
//    jObj["snapshotPath"]=m_snapshotPath;//截图路径
//    jObj["recordPath"]=m_recordPath; //录像路径
//    jObj["czIp"]=m_czIp;//目标上报目的IP
//    jObj["czPort"]=m_czPort;//目标上报目的端口
//    jObj["outputUrl"]=m_outputUrl;//视频上报地址
//    QString strJson=QString::fromUtf8(QJsonDocument(jObj).toJson());
//    emit signalsendParaValue(strJson);
}
