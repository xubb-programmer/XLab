#include "TargetTableWidget.h"
#include "ui_TargetTableWidget.h"
#include "UiPluginFactory.h"
#include <QFile>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QScreen>
#include <QTextCodec>
#include <QSettings>
#include <QHostAddress>
#include <climits>
#include <cstring>

namespace {

QString currentTargetNumberDatePrefix()
{
    const QDate today = QDate::currentDate();
    return QStringLiteral("%1%2%3")
            .arg(today.year() - 2000, 2, 10, QChar('0'))
            .arg(today.month(), 2, 10, QChar('0'))
            .arg(today.day(), 2, 10, QChar('0'));
}

QString currentTargetNumberPrefix()
{
    const QDate today = QDate::currentDate();
    return QStringLiteral("%1%2")
            .arg(today.month(), 2, 10, QChar('0'))
            .arg(today.day(), 2, 10, QChar('0'));
}

QString normalizedTargetNumberProtocol(const QString &carType)
{
    const QString protocol = carType.trimmed().toUpper();
    return protocol.isEmpty() ? QStringLiteral("033") : protocol;
}

QString targetNumberSerialKey(const QString &protocol)
{
    return QStringLiteral("serial_%1").arg(protocol);
}

QString targetNumberDateKey(const QString &protocol)
{
    return QStringLiteral("date_%1").arg(protocol);
}

unsigned int loadTargetNumberSerial(const QString &protocol, const QString &today)
{
    QSettings settings(QApplication::applicationDirPath() + "/config/OcrRecognition.ini",
                       QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("TargetNumber"));
    const QString serialKey = targetNumberSerialKey(protocol);
    const QString dateKey = targetNumberDateKey(protocol);
    const QString savedDate = settings.value(dateKey).toString().trimmed();
    const unsigned int serial = savedDate == today
            ? settings.value(serialKey, 0).toUInt()
            : 0u;
    settings.endGroup();
    return serial;
}

void saveTargetNumberSerial(const QString &protocol, const QString &today, unsigned int serial)
{
    QSettings settings(QApplication::applicationDirPath() + "/config/OcrRecognition.ini",
                       QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("TargetNumber"));
    settings.setValue(targetNumberSerialKey(protocol), serial);
    settings.setValue(targetNumberDateKey(protocol), today);
    settings.endGroup();
    settings.sync();
}

QByteArray marineFromGBKString(const QString &str)
{
    QTextCodec *gbk = QTextCodec::codecForName("GBK");
    if (!gbk) {
        gbk = QTextCodec::codecForName("GB2312");
    }
    return gbk ? gbk->fromUnicode(str) : str.toLocal8Bit();
}

bool marineContainChinese(const QString &str)
{
#if 0
    QRegularExpression re("[\u4E000-\u9FFF]");
    if(re.match(str).hasMatch()) {
        return true;
    }else {
        return false;
    }
#endif

    for (const QChar &ch : str) {
        const ushort code = ch.unicode();
        if ((code >= 0x4E00 && code <= 0x9FFF)
            || (code >= 0x3400 && code <= 0x4DBF)) {
            return true;
        }
    }
    return false;
}


void marineCopyStringToField(char *field, int fieldSize, const QString &str)
{
    if (!field || fieldSize <= 0) {
        return;
    }
    QByteArray ba;
    if (marineContainChinese(str)) {
        ba = marineFromGBKString(str);
    } else {
        ba = str.toLatin1();
    }
    const int copyLen = qMin(fieldSize, ba.size());
    if (copyLen > 0) {
        memcpy(field, ba.constData(), static_cast<size_t>(copyLen));
    }
}

QByteArray packTargetMarineArray(const OutHeader19 &head19,
                                 const InnerHeader30 &head30,
                                 const TargetInfo_Marine &target)
{
    QByteArray ba;
    ba.append(reinterpret_cast<const char *>(&head19), sizeof(head19));
    ba.append(reinterpret_cast<const char *>(&head30), sizeof(head30));
    ba.append(reinterpret_cast<const char *>(&target), sizeof(target));
    return ba;
}

TargetInfo_Marine buildMarineTargetFromTargetInfo(const TargetInfo &info)
{
    TargetInfo_Marine marine;

    marine.infoId = 1002;
    marine.sourcePlatformId = 18101000;//来源平台编号
    marine.envType = 7;//环境类别  与目标类型 敌我属性存在关联  7 6（动目标 装甲车辆） 或者 7 7（动目标 车辆）
    marine.longitude = static_cast<int64_t>(qRound(info.targetLon * 3600.0 * 1000.0));//经度 毫秒
    marine.latitude  = static_cast<int64_t>(qRound(info.targetLat * 3600.0 * 1000.0)); //纬度 毫秒
    marine.altitude  = static_cast<uint16_t>(qBound(0, static_cast<int>(qRound(info.targerElevate)), 65535)); //高度

    marine.moveDirection    = 0/*ui->tarYDFX->text().toDouble() * 1000000 / 360*/;     //运动方向  180
    marine.moveSpeed        = 0/*ui->tarYDSD->text().toDouble()*100 * 0.1*/;           //运动速度  0.1m/s

    marine.targetType = 7/*info.targetType*/;  //目标类型
    marine.targetAttribute = 0;  //敌我属性   默认敌方 我方不需要报
    marine.targetCountry = 1;   //国家地区 台湾
    marine.targetId = 10010000u + info.targetNumber.toUInt(); //固定基数 + 完整目标编号

    //目标名称
    const QString targetName = QString::fromLocal8Bit("目标%1").arg(info.targetNumber);
    marineCopyStringToField(reinterpret_cast<char *>(marine.targetName),
                            sizeof(marine.targetName), targetName);
    //来源名称
    marineCopyStringToField(reinterpret_cast<char *>(marine.name),
                            sizeof(marine.name), QString::fromLocal8Bit("江无侦察车"));

    marine.targetZM         = 0;    //目标正面距离
    marine.targetZS         = 0;    //目标纵深距离
    marine.targetSL         = 1;    //目标数量

    //时间
//    marine.targetTime = QDateTime::currentSecsSinceEpoch();
    QDateTime epoch1979(QDate(1979,1,1),QTime(0,0,0),Qt::UTC);
    QDateTime dt = QDateTime::fromString(info.targetTime, QStringLiteral("yyyy/MM/dd hh:mm:ss"));
    marine.targetTime = dt.isValid()
            ? static_cast<uint32_t>(epoch1979.secsTo(dt.toUTC()))
            : static_cast<uint32_t>(epoch1979.secsTo(QDateTime::currentDateTimeUtc()));

    marine.target_fxsd      = 7;  //发现手段  3
    marine.target_kkxpg     = 0; //可靠性评估
    marine.target_fh        = 5;    //目标防护 5
    marine.target_ybx       = 6;   //隐蔽性 6
    marine.target_zcxw      = 0;  //战场行为
    marine.target_zt        = 1;    //目标状态 5
    marine.target_dx        = 1;     //目标队形 1

    //备注
    marineCopyStringToField(reinterpret_cast<char *>(marine.target_bz),
                            sizeof(marine.target_bz), QStringLiteral(""));
    return marine;
}

} // namespace


TargetTableWidget::TargetTableWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::TargetTableWidget)
{
    ui->setupUi(this);
    setWindowFlags(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);

    this->show();
}

TargetTableWidget::~TargetTableWidget()
{
    delete ui;
}

/**
 * @brief init
 * 界面初始化函数。
 * @param pPlugin 界面框架指针
 * @return 无返回值
 */
void TargetTableWidget::initUi(IUiPluginBase *pPlugin)
{
    m_pPlugin = pPlugin;
    m_pPlugin->setStyle("day");


    ui->scrollArea->setStyleSheet("QScrollArea {background-color:transparent; border:0px;}"
                                  "QScrollArea QWidget {background-color:transparent;}");
    m_targetInfoSocket = new TargetInfoSocket(this);

    loadQssStyleFile();
}

/**
 * @brief initData
 * 插件数据初始化函数。
 * @param void
 * @return 无返回值
 */
void TargetTableWidget::initData()
{
    connect(ui->Add_pushButton,SIGNAL(clicked()),this,SLOT(slotAdd_pushButton()));
    connect(ui->Del_pushButton,SIGNAL(clicked()),this,SLOT(slotDel_pushButton()));

    if(m_targetInfoSocket)
    {
        m_targetInfoSocket->init();
    }
    connect(this,SIGNAL(sendUdpTargetInfo(QByteArray)),m_targetInfoSocket,SLOT(sendGDTargetArray(QByteArray)));

    initConfig();
    m_TcpComm = new TcpComm(this);
    if(m_TcpComm && m_cartype=="ZZC"){
        m_TcpComm->startClient(m_clientHost, m_clientPort);  // 客户端  ip和port 使用读取配置的
    }
    connect(this,SIGNAL(sendTcpTargetInfo(QByteArray)),m_TcpComm,SLOT(sendData(QByteArray)));
}

/**
 * @brief resetQss
 * 切肤函数。
 * @param void
 * @return 无返回值
 */
void TargetTableWidget::resetQss(const QString& strStyleName)
{
    //设置换肤路径
    m_pPlugin->setStyle(strStyleName);
    loadQssStyleFile();
}

/**
 * @brief registerProtocols
 * 虚函数的具体实现，初始化插件信号注册函数。
 * @param strStyleName（代表白天或者夜晚）
 * @return 无返回值
 */
void TargetTableWidget::registerProtocols()
{
    std::vector<IUiPluginBase *> vPlugins = UiPluginFactory::getInstance()->getPlugins("MessageBox");
    foreach (IUiPluginBase * plugin, vPlugins) {
        connect(this,SIGNAL(signalMessageInfoText(QString,QString,MessageType)),
                plugin->getWidget(),SLOT(onMessageText(QString,QString,MessageType )));
        connect(plugin->getWidget(),SIGNAL(signalConfirmBtnClicked(QString)),
                this,SLOT(slotConfirmBtnClicked(QString)));
    }
}

/**
 * @brief initConfig
 * 插件初始化配置
 */
void TargetTableWidget::initConfig()
{
    QSettings settings(QApplication::applicationDirPath() + "/config/communication.ini" , QSettings::IniFormat);
    settings.setIniCodec(QTextCodec::codecForName("GBK"));
    settings.beginGroup("Address");
    m_cartype = settings.value("cartype","033").toString();
    m_clientPort    =  settings.value("CzPort").toInt();
    m_clientHost =  settings.value("CzIp").toString();
    settings.endGroup();
}

/**
 * @brief addTargetInfo
 * 添加目标信息。
 * @param void
 * @return 无返回值
 */
void TargetTableWidget::addTargetInfo(TargetInfo dData)
{
    // 接收到的目标信息如果批号相同，只刷新目标数据
    for(int index = 0; index < m_vTargetInfo.size();++index)
    {
        if(m_vTargetInfo[index].targetNumber == dData.targetNumber)
        {
            m_vTargetInfo[index].targetType = dData.targetType;
            m_vTargetInfo[index].targetLon = dData.targetLon;
            m_vTargetInfo[index].targetLat = dData.targetLat;
            m_vTargetInfo[index].targerElevate = dData.targerElevate;
            m_vTargetInfo[index].targetTime = dData.targetTime;          
            m_vTargetInfo[index].X_Value = dData.X_Value;
            m_vTargetInfo[index].Y_Value = dData.Y_Value;
            for(int n = 0 ; n != m_listTargetItem.size(); ++n)
            {
                m_listTargetItem.at(n)->setTargetListInfo(m_vTargetInfo.at(n));
            }
            // 刷新到界面显示
            updateTargetItemUI();
            return;
        }
    }

    // 将新目标信息存储到容器
    m_vTargetInfo.push_back(dData);



    //目标列表同时显示的目标不超过20个,如果超过删除最历史的那一个
    if(m_vTargetInfo.size() > MAX_TARGET_NUM)
    {
        //删除最历史的目标数据
        m_vTargetInfo.pop_front();
        //删除最历史的目标数据界面上对应的目标信息
        for(int n = 0 ; n != m_listTargetItem.size(); ++n)
        {
            m_listTargetItem.at(n)->setTargetListInfo(m_vTargetInfo.at(n));
        }
    }
    else
    {
        // 依据新目标信息创建界面显示Item，然后显示在界面
        TargetItem * item = new TargetItem(ui->scrollAreaWidgetContents);
        item->initUi(m_pPlugin);
        item->slotModify_pushButton(false);
        item->setFixedWidth(ui->scrollAreaWidgetContents->width() - m_nScrollOffset);

        qRegisterMetaType<TargetInfo>("TargetInfo");
        connect(item,SIGNAL(signalModifyTargetInfo(TargetInfo)),this,SLOT(slotModifyTargetInfo(TargetInfo)));
        connect(item,SIGNAL(signalTargetReport(TargetInfo)),this,SLOT(slotTargetReport(TargetInfo)));
        connect(item,SIGNAL(signalDeleteInfoTarget(QString)),this,SLOT(slotItemMessageInfoText(QString)));
        connect(item,SIGNAL(signalModifyNoInfo(QString,TargetInfo)),this,SLOT(slotModifyNoInfo(QString,TargetInfo)));
        connect(item,SIGNAL(signalModifyChecked(TargetItem*,bool)),this,SLOT(slotTargetItemModifyChecked(TargetItem*,bool)));//Item编辑状态互斥
        connect(item,SIGNAL(signalMessageInfoText(QString,QString,MessageType)),
                this,SIGNAL(signalMessageInfoText(QString,QString,MessageType)));

        item->setTargetListInfo(dData);
        item->setVisible(true);
        m_listTargetItem.append(item);
    }
    updateTargetItemUI();
}

/**
 * @brief updateTargetItemUI
 * 依据存储到容器中的目标卡片信息重新刷新界面显示。
 * @param void
 * @return 无返回值
 */
void TargetTableWidget::updateTargetItemUI()
{
    int nIndex = 0;
    int height = 0;
    int height_single= 0;
    int y = 0;
    int x = 0;

    for(int n = 0 ; n < m_listTargetItem.size(); ++n)
    {
        TargetItem *item = (TargetItem *)m_listTargetItem.at(m_listTargetItem.size()-n-1);
        item->resize(233,255);
        item->move(x, y);
        height_single = item->height();
        y = height + height_single +5;
        ++nIndex;
        height = y;

    }

    // 如果界面没有目标信息，要显示没有目标信息的图标和字体
    if(m_listTargetItem.size() == 0)
    {
        ui->scrollAreaWidgetContents->setFixedHeight(ui->stackedWidget->height());
        ui->WFX_Btn->show();
        ui->WFX_label->show();
        return;
    }
    else
    {
        ui->WFX_Btn->hide();
        ui->WFX_label->hide();
    }

    ui->scrollAreaWidgetContents->setFixedHeight(height);
    ui->scrollArea->setWidget(ui->scrollAreaWidgetContents);
}

/**
 * @brief slotAdd_pushButton
 * 点击界面添加目标信息按钮触发槽函数。
 * @param void
 * @return 无返回值
 */
void TargetTableWidget::slotAdd_pushButton()
{
    TargetInfo tempInfo;
    initConfig();
//    //获取最大目标编号 自增
//    int maxTargetNumber = 0;
//    for(int i = 0; i < m_vTargetInfo.size(); ++i)
//    {
//        bool ok = false;
//        int targetNumber = m_vTargetInfo.at(i).targetNumber.toInt(&ok);
//        if(ok && targetNumber > maxTargetNumber)
//        {
//            maxTargetNumber = targetNumber;
//        }
//    }

    const QString protocol = normalizedTargetNumberProtocol(m_cartype);
    const QString today = currentTargetNumberDatePrefix();
    const unsigned int currentSerial = loadTargetNumberSerial(protocol, today);
    const unsigned int nextSerial = currentSerial >= 9999u ? 1u : currentSerial + 1u;
    saveTargetNumberSerial(protocol, today, nextSerial);

    emit signalTargetNum(static_cast<int>(nextSerial));//向OcrRecognition发送当前协议已占用的编号
    if (protocol == QLatin1String("002")) {
        tempInfo.targetNumber = QString::number(nextSerial);
    } else {
        tempInfo.targetNumber = currentTargetNumberPrefix()
                + QStringLiteral("%1").arg(nextSerial, 4, 10, QChar('0'));
    }
    tempInfo.targetTime = QDateTime::currentDateTime().toString("yyyy/MM/dd hh:mm:ss");
    addTargetInfo(tempInfo);
    m_maxTargetNumber = static_cast<int>(nextSerial);
}

/**
 * @brief slotDel_pushButton
 * 点击界面清空目标信息的按钮触发的槽函数。
 * @param void
 * @return 无返回值
 */
void TargetTableWidget::slotDel_pushButton()
{
    emit signalMessageInfoText(QString::fromUtf8(u8"目标全部删除"),
                               QString::fromUtf8(u8"是否清空目标列表！"),MessageType_Info);
}

/**
 * @brief slotModifyTargetInfo
 * 点击界面目标编辑按钮，使得目标信息控件进入可编辑状态，然后编辑好后的信息，触发执行的槽函数。
 * @param void
 * @return 无返回值
 */
void TargetTableWidget::slotModifyTargetInfo(TargetInfo dData)
{
    for(int index = 0; index < m_vTargetInfo.size();++index)
    {
        if(m_vTargetInfo[index].targetNumber == dData.targetNumber)
        {

            m_vTargetInfo[index].targetType = dData.targetType;
            m_vTargetInfo[index].targetLon = dData.targetLon;
            m_vTargetInfo[index].targetLat = dData.targetLat;
            m_vTargetInfo[index].targerElevate = dData.targerElevate;
            m_vTargetInfo[index].targetTime = dData.targetTime;
            for(int n = 0 ; n != m_listTargetItem.size(); ++n)
            {
//                m_listTargetItem.at(n)->setTargetListInfo(m_vTargetInfo.at(n));//会改变其他正在编辑界面的值显示
            }
            return;
        }
    }
}

/**
 * @brief slotTargetItemModifyChecked  Item编辑状态互斥
 * @param item
 * @param checked
 */
void TargetTableWidget::slotTargetItemModifyChecked(TargetItem *item, bool checked)
{
    if(!checked || item == nullptr){
        return;
    }

    for(int n = 0; n < m_listTargetItem.size(); ++n)
    {
        TargetItem *otherItem = m_listTargetItem.at(n);
        if(otherItem != nullptr && otherItem != item)
        {
            otherItem->setModifyChecked(false);
        }
    }
}

/**
 * @brief slotTargetReport
 * 目标上报
 * @param dData(目标信息)
 * @return 无返回值
 */
void TargetTableWidget::slotTargetReport(TargetInfo dData)
{
    //上报给车长席-炸点目标信息

    initConfig();//获取上报车型

    if(m_cartype=="033")// 033 1501协议上报
    {
        sendTargetTo15ZCC(dData);
    }else if(m_cartype=="002"){//002 协议上报
        sendTargetToZZT(dData);
    }else if(m_cartype=="HL"){//HL 协议上报
        sendTargetToMarineHL(dData);
    }else if(m_cartype=="ZZC"){//zzc 协议上报
        sendTargetToZZC(dData);
    }

}

/**
 * @brief slotItemMessageInfoText
 * 接收TargetTable类对象中的删除按钮所触发的对单个目标信息的删除。
 * @param void
 * @return 无返回值
 */
void TargetTableWidget::slotItemMessageInfoText(QString dData)
{
    QStringList strlist = dData.split(':');
    // 获取要删除的目标的目标批号记录到内存，当收到MessageBox确认消息后，依据该编号进行目标删除
    m_treatDeleteTargetId = strlist.at(1);
    emit signalMessageInfoText(QString::fromUtf8(u8"目标删除"),
                               QString::fromUtf8(u8"是否删除目标信息！"),MessageType_Info);
}

/**
 * @brief slotModifyNoInfo
 * 修改目标编号
 * @param str1(历史目标编号),str2(新目标编号)
 * @return 无返回值
 */
void TargetTableWidget::slotModifyNoInfo(QString str1,TargetInfo str2)
{
    // 接收到的目标信息如果批号相同，只刷新目标数据
    for(int index = 0; index < m_vTargetInfo.size();++index)
    {
        if(m_vTargetInfo[index].targetNumber == str1)
        {
            m_vTargetInfo[index].targetNumber = str2.targetNumber;
            m_vTargetInfo[index].targetType = str2.targetType;
            m_vTargetInfo[index].targetLon = str2.targetLon;
            m_vTargetInfo[index].targetLat = str2.targetLat;
            m_vTargetInfo[index].targerElevate = str2.targerElevate;
            m_vTargetInfo[index].targetTime = str2.targetTime;
            m_vTargetInfo[index].X_Value = str2.X_Value;
            m_vTargetInfo[index].Y_Value = str2.Y_Value;
            for(int n = 0 ; n != m_listTargetItem.size(); ++n)
            {
                m_listTargetItem.at(n)->setTargetListInfo(m_vTargetInfo.at(n));
            }
            // 刷新到界面显示
            updateTargetItemUI();
            return;
        }
    }
}

/**
 * @brief slotConfirmBtnClicked
 * 由MessageBox插件点击确认后执行的槽函数。
 * @param dData（MessageBox返回的弹窗主题名称）
 * @return 无返回值
 */
void TargetTableWidget::slotConfirmBtnClicked(QString dData)
{
    if (dData == QString::fromLocal8Bit("目标删除"))
    {
        for(int n = 0 ; n != m_vTargetInfo.size(); ++n)
        {
            if(m_vTargetInfo.at(n).targetNumber==m_treatDeleteTargetId){
                m_vTargetInfo.removeAt(n);
                break;
            }
        }

        for(int n = 0 ; n != m_listTargetItem.size(); ++n)
        {
            if(m_listTargetItem.at(n)->getTargetNumber()==m_treatDeleteTargetId){
                delete m_listTargetItem.at(n);
                m_listTargetItem.removeAt(n);
                break;
            }
        }

//        int maxTargetNumber = 0;
//        for(int i = 0; i < m_vTargetInfo.size(); ++i)
//        {
//            bool ok = false;
//            int targetNumber = m_vTargetInfo.at(i).targetNumber.toInt(&ok);
//            if(ok && targetNumber > maxTargetNumber)
//            {
//                maxTargetNumber = targetNumber;
//            }
//        }
//        emit signalTargetNum(maxTargetNumber);//向OcrRecognition发送目标编号

        updateTargetItemUI();
    }
    else if(dData == QString::fromLocal8Bit("目标全部删除"))
    {
        if(!m_vTargetInfo.isEmpty())
        {
            m_vTargetInfo.clear();
        }

        if(!m_listTargetItem.isEmpty())
        {
            for(int n = 0 ; n != m_listTargetItem.size(); ++n)
            {
                delete m_listTargetItem.at(n);
            }
            m_listTargetItem.clear();
        }
//        emit signalTargetNum(0);
        updateTargetItemUI();
    }
}

/**
 * @brief slotShowTable
 * 是否显示目标列表插件
 * @param bStatus(true:显示 false:隐藏)
 * @return 无返回值
 */
void TargetTableWidget::slotShowTable(bool bStatus)
{
    if(bStatus)
    {
        this->show();
    }
    else
    {
        this->hide();
    }
}

/**
 * @brief slotRecvOcrTargetInfo
 * 接收OCR识别插件上报的目标信息
 * @param dData(目标属性信息)
 * @return 无返回值
 */
void TargetTableWidget::slotRecvOcrTargetInfo(TargetInfo dData)
{
    addTargetInfo(dData);
    slotTargetReport(dData);
}

/**
 * @brief loadQssStyleFile
 * 给予UI界面控件设置Qss图片样式。
 * @param void
 * @return 无返回值
 */
void TargetTableWidget::loadQssStyleFile()
{
    QString strPath = m_pPlugin->getStylePath() + QString("/TargetTableWidget.qss");

    Q_ASSERT(!strPath.isEmpty() && "path is empty");
    QFile textFile(strPath);

    Q_ASSERT(textFile.exists() && "file can't open");
    textFile.open(QIODevice::ReadOnly | QIODevice::Text);
    QTextStream textStream(&textFile);

    QString style = textStream.readAll();
    this->setStyleSheet(style);
}

/**
 * @brief sendTarget
 * 识别出的上报目标信息到车长席
 * @param info(1501目标结构体)
 * @return 无返回值
 */
void TargetTableWidget::sendTarget(UavTargetInfoWithObserver1501 info)
{
    //15车数据格式定义
    const unsigned char kMsgHeadLen = 26;   //包头长度
    unsigned char msg[kMsgHeadLen + sizeof(info)] = {0x00};
    headBag(msg,En_1501_Soft_Id::Info_Process_Server, En_1501_Soft_Id::QBCL, En_1501_Msg_Type::ZCXX, En_1501_Msg_Code::Uav_Target, sizeof(info));
    memcpy(msg + kMsgHeadLen,&info,sizeof(info));
    emit sendUdpTargetInfo(QByteArray((const char*)msg,kMsgHeadLen + sizeof(info)));
}

/**
 * @brief sendBurstTarget
 * 识别出的炸点信息到车长席
 * @param info(1501炸点结构体)
 * @return 无返回值
 */
void TargetTableWidget::sendBurstTarget(BurstTargetInfoWithObserver1501 info)
{
    //15车数据格式定义
    const unsigned char kMsgHeadLen = 26;   //包头长度
    unsigned char msg[kMsgHeadLen + sizeof(info)] = {0x00};
    headBag(msg,En_1501_Soft_Id::Info_Process_Server, En_1501_Soft_Id::QBCL, En_1501_Msg_Type::ZCXX, En_1501_Msg_Code::Uav_Burst_Point, sizeof(info));
    memcpy(msg + kMsgHeadLen,&info,sizeof(info));
    emit sendUdpTargetInfo(QByteArray((const char*)msg,kMsgHeadLen + sizeof(info)));
}

/**
 * @brief sendTargetTo15ZCC
 * 识别出的上报目标信息到1501 033
 */
void TargetTableWidget::sendTargetTo15ZCC(TargetInfo dData)
{
    if((uint)dData.targetType == 1)
    {
        BurstTargetInfoWithObserver1501 info {0x00};
        info.flag = 1;
        info.burstId  = dData.targetNumber.toDouble();
        info.burstCnt = 1;
        info.burstType = 2;
        info.burstCode = 1;
        info.longitude = dData.targetLon;
        info.latitude  = dData.targetLat;
        info.altitude  = dData.targerElevate;
        info.depthDistance  = 0;
        info.frontDistance  = 0;
        QDate date = QDate::currentDate();
        info.year  = date.year();
        info.month = date.month();
        info.day   = date.day();
        QTime time = QTime::currentTime();
        info.hour  = time.hour();
        info.minute = time.minute();
        info.second = time.second();
        info.millisecond = time.msec();
        sendBurstTarget(info);
    }
    else
    {
        //上报给车长席-侦察目标信息
        UavTargetInfoWithObserver1501 info {0x00};
        info.seriesId  = dData.targetNumber.toDouble();

        info.longitude = dData.targetLon;
        info.latitude  = dData.targetLat;
        info.altitude  = dData.targerElevate;
        info.speed     = 0;
        info.heading   = 0;
        info.distance  = 0;
        info.pitch     = 0;
        info.azimuth   = 0;
        info.flag = 0;
        info.effectiveFlag = 1;
        info.type = 1;
        QDate date = QDate::currentDate();
        info.year  = date.year();
        info.month = date.month();
        info.day   = date.day();
        QTime time = QTime::currentTime();
        info.hour  = time.hour();
        info.minute = time.minute();
        info.second = time.second();
        info.millisecond = time.msec();
        sendTarget(info);
    }
}


/**
 * @brief sendTarget
 * 识别出的上报目标信息到002车
 */
void TargetTableWidget::sendTargetToZZT(TargetInfo info)
{
    //00车数据格式
    m_num++;
    unsigned char msg[104] = {0x00};
    msg[0] = 0x90;
    msg[1] = 0xEB;
    msg[2] = 0x67;
    msg[3] = 0x64;
    msg[4] = 0x50;
    msg[5] = 0x50;
    msg[6] = 0x00;
    msg[7] = 0x00;
    msg[8] = 0x00;
    msg[9] = 0x4D;
    msg[10] = 0x00;
    msg[11] = 0x00;
    msg[12] = 0x00;
    msg[13] = 0x00;
    msg[14] = 0x08;
    msg[15] = 0x01;
    msg[16] = 0x00;
    msg[17] = 0x00;
    msg[18] = 0x00;
    msg[19] = 0x68;
    msg[20] = 0x00;
    msg[21] = 0x00;
    msg[22] = 0x00;
    msg[23] = m_num;//计数
    msg[24] = 0x00;
    msg[25] = 0x00;


    // 002协议使用不含年月日的纯序号，编号字段固定为2字节。
    const ushort Number = info.targetNumber.toUShort();
    memcpy(msg + 26,&Number,2);

    msg[28] = 0x01;
    //时间
    QDateTime dt=QDateTime::fromString(info.targetTime,"yyyy/MM/dd hh:mm:ss");
    if(dt.isValid()){
        ushort year=dt.date().year();
        memcpy(msg + 29,&year,2);//年
        msg[31] = dt.date().month();//月
        msg[32] = dt.date().day();//日
        msg[33] = dt.time().hour();//时
        msg[34] = dt.time().minute();//分
        msg[35] = dt.time().second();//秒

    }
    QTime time = QTime::currentTime();//毫秒
    ushort millisecond=time.msec();
    memcpy(msg + 36,&millisecond,2);//年


    //经度
    memcpy(msg + 38,&(info.targetLon),8);
    //维度
    memcpy(msg + 46,&(info.targetLat),8);
    //高程
    memcpy(msg + 54,&(info.targerElevate),8);

    //方位 8
    msg[62] = 0x00;
    msg[69] = 0x00;
    //距离 8
    msg[70] = 0x00;
    msg[77] = 0x00;

    // 目标类型  -炸点01 00，目标E2 00
    if(info.targetType==0x01){
        msg[78] = 0x01;
        msg[79] = 0x00;
    }else{
        msg[78] = 0xE2;
        msg[79] = 0x00;
    }

    emit sendUdpTargetInfo(QByteArray((const char*)msg,104));
}

/**
 * @brief sendTargetToZZC
 * 将 TargetInfo 经纬高转换为 规定的 29 字节帧并通过 TCP 发送 至 zzc
 */
void TargetTableWidget::sendTargetToZZC(TargetInfo info)
{

    // 帧ID：精确到秒，格式 yyyyMMddHHmmss（14 字节）
    QDateTime dt = QDateTime::fromString(info.targetTime, QStringLiteral("yyyy/MM/dd hh:mm:ss"));
    if (!dt.isValid()) {
        dt = QDateTime::currentDateTime();
    }


    //文件信息
    TcpGDTargetFileFrame fileframe {};
    fileframe.frameType=0x01;

    const QByteArray fileframeId = (dt.toString(QStringLiteral("yyyyMMddHHmmss"))+"_2.jpg").toLatin1();
    const int frameidCopyLen = qMin(20, fileframeId.size());
    if (frameidCopyLen > 0) {
        memcpy(fileframe.frameId, fileframeId.constData(), static_cast<size_t>(frameidCopyLen));
    }

    //文件信息
    emit sendTcpTargetInfo(QByteArray(reinterpret_cast<const char *>(&fileframe),
                                        static_cast<int>(sizeof(fileframe))));

    sleep_msec(100);

    //目标信息
    TcpGDTargetProtocolFrame frame {};
    frame.frameType = 0x02;

    const QByteArray frameId = dt.toString(QStringLiteral("yyyyMMddHHmmss")).toLatin1();
    const int idCopyLen = qMin(14, frameId.size());
    if (idCopyLen > 0) {
        memcpy(frame.frameId, frameId.constData(), static_cast<size_t>(idCopyLen));
    }

    // 经度/纬度：double(度) -> int32，分辨率 0.0000001°/LSB，东经、北纬为正
    frame.longitude = static_cast<int32_t>(qRound(info.targetLon * 10000000.0));
    frame.latitude  = static_cast<int32_t>(qRound(info.targetLat * 10000000.0));

    // 海拔高：double(米) -> int16，分辨率 0.1m/LSB
    const qint64 alt = qRound(info.targerElevate* 10.0);
    frame.altitude = static_cast<int16_t>(qBound<qint64>(INT16_MIN, alt, INT16_MAX));

    // 距离、方位：TargetInfo 无对应字段，填 0
    frame.distance = 0;
    frame.azimuth  = 0;

    //目标信息
    emit sendTcpTargetInfo(QByteArray(reinterpret_cast<const char *>(&frame),
                                        static_cast<int>(sizeof(frame))));

}

/**
 * @brief sendTargetToMarineHL
 * 打包HL目标并通过 UDP 上报
 */
void TargetTableWidget::sendTargetToMarineHL(TargetInfo info)
{
    TargetInfo_Marine target = buildMarineTargetFromTargetInfo(info);

    OutHeader19 head19;  //需要填写 len字段网络报文长度  内头 + 外头 + 正文长，源/目的 系统标识 主机标识  实体标识 为默认
    InnerHeader30 head30; //需要填入目标正文长度

    head19.len = static_cast<uint16_t>(sizeof(head19) + sizeof(head30) + sizeof(target));
    head30.banwen_len = static_cast<uint16_t>(sizeof(target));

    if (m_targetInfoSocket) {
        const QString localIp = m_targetInfoSocket->getPreferredLocalIPv4Address();
        if (!localIp.isEmpty()) {
            head30.N1 = QHostAddress(localIp).toIPv4Address();
        }
    }

    const QByteArray ba = packTargetMarineArray(head19, head30, target);
    emit sendUdpTargetInfo(ba);
}

/**
 * @brief headBag
 * 上报目标信息到车长席帧头
 * @param p0(数据内容) sourceID(源ID) tgtID(目的ID) msgType(消息类型) msgCode(消息码) msgLen(信息长度)
 * @return 无返回值
 */
void TargetTableWidget::headBag(unsigned char *p0, int sourceID, int tgtID, int msgType, int msgCode, unsigned short msgLen)
{
    p0[0] = 0x90;
    p0[1] = 0xEB;
    p0[2] = sourceID;  //源软件ID
    p0[3] = tgtID;     //目的软件ID
    p0[4] = msgType;   //信息类型
    p0[5] = msgCode;    //信息代号
    p0[6] = 0x00;
    p0[7] = 0x00;
    p0[8] = 0x00;

    p0[9] =  (unsigned char)(msgLen &(0x00ff));//协议报文长度低字节
    p0[10] = (unsigned char)((msgLen& (0xff00))/256);//协议报文长度高字节

    p0[11] = 0x00;
    p0[12] = 0x00;
    p0[13] = 0x00;

    p0[14] = 0x08;  //抓
    p0[15] = 0x01;  //抓

    p0[16] = 0x00;
    p0[17] = 0x00;
    p0[18] = 0x00;

    p0[19] = 0xB9;  //抓

    p0[20] = 0x00;
    p0[21] = 0x00;
    p0[22] = 0x00;

    p0[23] = 0x03;  //抓
    p0[24] = 0x00;
    p0[25] = 0x00;
}

/**
 * @brief setScrollBarStyleSheet
 * 设置tableview的scrollbar的样式
 * @param void
 * @return 无返回值
 */
void TargetTableWidget::setScrollBarStyleSheet()
{
    ;
}

/**
 * @brief slotRecvOCRTargetNum 从OCR插件接收目标编号
 */
void TargetTableWidget::slotRecvOCRTargetNum(int num)
{
    m_OcrNum=num;
}

/**
 * @brief slotTargetReportAddress 从ParameterSet插件接收目标上报地址
 */
void TargetTableWidget::slotTargetReportAddress(QString ip, int port)
{
    if(m_clientPort!=port || m_clientHost!=ip){
        initConfig();
        if(m_TcpComm && m_cartype=="ZZC"){
            m_TcpComm->startClient(ip, port);  // 客户端  ip和port 使用读取配置的
        }
    }

}


void TargetTableWidget:: sleep_msec(int msec)
{
  QTime dieTime =QTime::currentTime().addMSecs(msec);
  while (QTime::currentTime()<dieTime) {
      QCoreApplication::processEvents(QEventLoop::AllEvents,100);
  }
}
