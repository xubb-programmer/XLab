#include "ParameterSetWidget.h"
#include "ui_ParameterSetWidget.h"
#include <QFile>
#include "UiPluginFactory.h"
#include "FrameLogManager.h"
#include <QButtonGroup>
#include <QCoreApplication>

ParameterSetWidget::ParameterSetWidget(QWidget *parent) :
    CustomMoveWidget(parent),
    ui(new Ui::ParameterSetWidget)
{
    ui->setupUi(this);
    setTitle(ui->ParameterSetContent_Widget);
    setWindowFlags(Qt::FramelessWindowHint /*| Qt::WindowStaysOnTopHint*/ |Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground,true);
}

ParameterSetWidget::~ParameterSetWidget()
{
    // 设备信息界面的生命周期仍由原插件管理，退出时解除参数设置界面的父子关系。
    if (m_pDeviceInfoWidget) {
        ui->verticalLayout->removeWidget(m_pDeviceInfoWidget);
        m_pDeviceInfoWidget->setParent(nullptr);
        m_pDeviceInfoWidget->setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);
    }
    delete ui;
}

/**
 * @brief initUi
 * 界面初始化函数。
 * @param pPlugin 界面框架指针
 * @return 无返回值
 */
void ParameterSetWidget::initUi(IUiPluginBase *pPlugin)
{
    m_pPlugin = pPlugin;
    m_pPlugin->setStyle("day");

    m_pParaContentWidget = new ParaContentWidget(this);
    // 将参数UI界面加入到布局
    addWidget(QString::fromLocal8Bit("流媒体"),m_pParaContentWidget);
    m_pParaContentWidget->initUi(m_pPlugin);
    m_pParaContentWidget->show();
    connect(this,SIGNAL(signalIdentify(int,int)),m_pParaContentWidget,SLOT(slotIdentify(int,int)));
    connect(m_pParaContentWidget,SIGNAL(signalIdentifyRespons(int,int)),this,SLOT(slotIdentifyRespons(int,int)));
    connect(m_pParaContentWidget,SIGNAL(signalSwitchBigScreen(QJsonObject)),this,SLOT(slotSwitchBigScreen(QJsonObject)));
    connect(m_pParaContentWidget,SIGNAL(signalStreamPullAdress(QString)),this,SIGNAL(signalToVideoProcessFFmpeg(QString)));
    connect(m_pParaContentWidget,SIGNAL(signalZLMPushStatus(int,QString)),this,SIGNAL(signalZLMParPushStatus(int,QString)));


    m_pOcrRecognitionSetWidget = new OcrRecognitionSetWidget(this);
//     将Ocr识别区域UI界面加入到布局
    addWidget(QString::fromLocal8Bit("OCR设置"),m_pOcrRecognitionSetWidget);
    m_pOcrRecognitionSetWidget->initUi(m_pPlugin);
    m_pOcrRecognitionSetWidget->hide();
    connect(m_pOcrRecognitionSetWidget,SIGNAL(signalOcrPushStreamPara(QString,QJsonObject)),m_pParaContentWidget,SIGNAL(signalOcrPushStreamPara(QString,QJsonObject)));
    connect(m_pParaContentWidget,SIGNAL(signalFinishedForOcrPushStreamPara(QString)),m_pOcrRecognitionSetWidget,SLOT(slotFinishedForOcrPushStreamPara(QString)));

    m_pScreenPushSetWidget = new ScreenPushSetWidget(this);
    // 将屏幕推流UI界面加入到布局
    addWidget(QString::fromLocal8Bit("桌面推流"),m_pScreenPushSetWidget);
    m_pScreenPushSetWidget->initUi(m_pPlugin);
    m_pScreenPushSetWidget->hide();


    m_pNetworkSetWidget = new NetworkSetWidget(this);
//     将网口设置UI界面加入到布局
    addWidget(QString::fromLocal8Bit("网口设置"),m_pNetworkSetWidget);
    m_pNetworkSetWidget->initUi(m_pPlugin);
    m_pNetworkSetWidget->hide();
    connect(m_pNetworkSetWidget,SIGNAL(signalSendNetSetPara(QString,QJsonObject)),m_pParaContentWidget,SIGNAL(signalSendNetSetPara(QString,QJsonObject)));// 下发网络设置数据
    connect(m_pParaContentWidget,SIGNAL(signalFinishedForNetWorkSet(QString)),m_pNetworkSetWidget,SLOT(slotFinishedForNetWorkSet(QString)));
//    connect(m_pNetworkSetWidget,&NetworkSetWidget::signalOnLineToSSE,m_pParaContentWidget,&ParaContentWidget::slotReconnectSSE);//向sse发送重连信号
    connect(m_pNetworkSetWidget,&NetworkSetWidget::signalproductNum,m_pParaContentWidget,&ParaContentWidget::slotproductNum);//发送产品编号 更新显示



    m_pModelContentWidget = new ModelContentWidget(this);
    // 将模型算法UI界面加入到布局
    addWidget(QString::fromLocal8Bit("模型算法"),m_pModelContentWidget);
    m_pModelContentWidget->initUi(m_pPlugin);
    m_pModelContentWidget->hide();

    m_pOtherSetWidget = new OtherSetWidget(this);
    // 将其他设置UI界面加入到布局
    addWidget(QString::fromLocal8Bit("其他设置"),m_pOtherSetWidget);
    m_pOtherSetWidget->initUi(m_pPlugin);
    m_pOtherSetWidget->hide();
    connect(m_pOtherSetWidget,SIGNAL(signalStreamPullAdress(QString)),this,SIGNAL(signalToVideoProcessFFmpeg(QString)));
    connect(m_pOtherSetWidget,&OtherSetWidget::signalSwitchHttp,m_pParaContentWidget,&ParaContentWidget::slotReconnectSSE);
    connect(m_pParaContentWidget,SIGNAL(signalcurrentDeviceIp(QString)),m_pOtherSetWidget,SLOT(slotcurrentDeviceIp(QString)));
    connect(m_pOtherSetWidget, &OtherSetWidget::signalVideoReportControl,
            this, &ParameterSetWidget::slotVideoReportControl);

    connect(this,SIGNAL(signalListClicked(QString)),this,SLOT(slotListClicked(QString)));
    connect(ui->Para_pushButton,SIGNAL(clicked()),this,SLOT(slotPara_pushButton()));
    connect(ui->Model_pushButton,SIGNAL(clicked()),this,SLOT(slotModel_pushButton()));
    connect(ui->ParameterSetClose_pushButton_2,SIGNAL(clicked()),this,SLOT(slotCloseUi()));


    //按钮互斥 显示切换 0流媒体 1桌面推流 2网口设置 3OCR设置 4模型算法 5其他设置 6设备信息
    m_pButtonGroup = new QButtonGroup(this);
    m_pButtonGroup->addButton(ui->Para_pushButton,0);
    m_pButtonGroup->addButton(ui->DeskPush_pushButton,1);
    m_pButtonGroup->addButton(ui->EthSet_pushButton,2);
    m_pButtonGroup->addButton(ui->OcrSet_pushButton,3);
    m_pButtonGroup->addButton(ui->Model_pushButton,4);
    m_pButtonGroup->addButton(ui->OtherSet_pushButton,5);
    m_pButtonGroup->addButton(ui->DeviceInfo_pushButton,6);
    ui->OcrSet_pushButton->hide();
    ui->EthSet_pushButton->hide();
    ui->Model_pushButton->hide();
    connect(m_pButtonGroup, SIGNAL(buttonClicked(int)), this, SLOT(slotUIBtnClicked(int)));//显示切换

    // 默认显示参数UI界面
    ui->Para_pushButton->setChecked(true);
    loadQssStyleFile();

    qRegisterMetaType<BackendData>("BackendData");
    connect(this,SIGNAL(signalRecBackendData(BackendData)),this,SLOT(slotBackendData(BackendData)));
}

/**
 * @brief initData
 * 插件数据初始化函数。
 * @param void
 * @return 无返回值
 */
void ParameterSetWidget::initData()
{
    m_pParaContentWidget->initData();

    //大小屏窗口切换
    m_pBackendData = getBackendDataInstance(32768,QCoreApplication::applicationDirPath() + "/dds");//前端发送域ID
    if(m_pBackendData){
//        m_pBackendData->registListener(this);
    }else{
        FrameLogManager::getInstance()->log(QString("ParameterSet"),QString("m_pBackendData isNullptr id:%1").arg("32768"),IFrameLogManager::ERROR_TYPE);
    }

}

/**
 * @brief resetQss
 * 切肤函数。
 * @param void
 * @return 无返回值
 */
void ParameterSetWidget::resetQss(const QString& strStyleName)
{
    //设置换肤路径
    m_pPlugin->setStyle(strStyleName);
    loadQssStyleFile();
}

/**
 * @brief registerProtocols
 * 虚函数的具体实现，初始化插件信号注册函数。
 * @param void
 * @return 无返回值
 */
void ParameterSetWidget::registerProtocols()
{
    std::vector<IUiPluginBase *> vPlugins = UiPluginFactory::getInstance()->getPlugins("SignalTransit");
    foreach (IUiPluginBase * plugin, vPlugins) {
        connect(plugin->getWidget(),SIGNAL(signalPluginMsg(pluginmsg)),this,SLOT(slotPluginMsg(pluginmsg)));
        connect(this,SIGNAL(signalPluginMsg(pluginmsg)),plugin->getWidget(),SIGNAL(signalPluginMsg(pluginmsg)));//发送
    }

    vPlugins = UiPluginFactory::getInstance()->getPlugins("MessageBox");
    foreach (IUiPluginBase * plugin, vPlugins) {
        connect(m_pParaContentWidget,SIGNAL(signalMessageInfoText(QString,QString,MessageType)),
                plugin->getWidget(),SLOT(onMessageText(QString,QString,MessageType )));
        connect(m_pParaContentWidget,SIGNAL(signalMessageText(QString,QString,QString,QString,QString)),
                plugin->getWidget(),SLOT(onMessageText(QString,QString,QString,QString,QString)));
        connect(m_pOcrRecognitionSetWidget,SIGNAL(signalMessageInfoText(QString,QString,MessageType)),
                plugin->getWidget(),SLOT(onMessageText(QString,QString,MessageType )));
        connect(plugin->getWidget(),SIGNAL(signalConfirmBtnClicked(QString)),
                m_pParaContentWidget,SLOT(slotConfirmBtnClicked(QString)));
        connect(m_pNetworkSetWidget,SIGNAL(signalMessageInfoText(QString,QString,MessageType)),
                plugin->getWidget(),SLOT(onMessageText(QString,QString,MessageType )));
        connect(m_pOtherSetWidget,SIGNAL(signalMessageInfoText(QString,QString,MessageType)),
                plugin->getWidget(),SLOT(onMessageText(QString,QString,MessageType )));
		connect(m_pScreenPushSetWidget,SIGNAL(signalMessageInfoText(QString,QString,MessageType)),
                plugin->getWidget(),SLOT(onMessageText(QString,QString,MessageType )));
    }


    vPlugins = UiPluginFactory::getInstance()->getPlugins("DeviceManagement");
    foreach (IUiPluginBase * plugin, vPlugins) {
       connect(plugin->getWidget(),SIGNAL(signalDeviceStatusToParameterSet(QString,int)),
                m_pNetworkSetWidget,SLOT(slotFinishedForgetdeviceStatus(QString,int)));//从DeviceManagement插件接收获取设备状态
       connect(plugin->getWidget(),SIGNAL(signalDeviceStatusToParameterSet(QString,int)),
                m_pParaContentWidget,SLOT(slotCurrentDeviceStatus(QString,int)));//同步当前设备名称，作为组播设备列表的备用数据源
       connect(plugin->getWidget(),SIGNAL(signalDeviceStatusShow(bool)),
                m_pParaContentWidget,SLOT(slotCurrentDeviceOnlineChanged(bool)));//设备离线时同步移除备用设备名称
       connect(m_pOtherSetWidget,SIGNAL(signalSwitchHttp(QString)),
                   plugin->getWidget(),SLOT(slotSwitchHttp(QString)));//向DeviceManagement插件发送http通信地址切换

       // 直接嵌入原设备管理界面，不复制界面或设备状态逻辑。
       if (!m_pDeviceInfoWidget && plugin->getWidget()) {
           m_pDeviceInfoWidget = plugin->getWidget();
           m_pDeviceInfoWidget->setParent(ui->Content_widget);
           m_pDeviceInfoWidget->setWindowFlags(Qt::Widget);
           if (QWidget *closeButton = m_pDeviceInfoWidget->findChild<QWidget *>("DeviceManageClose_pushButton")) {
               closeButton->hide();
           }
           addWidget(QString::fromLocal8Bit("设备信息"), m_pDeviceInfoWidget);
           m_pDeviceInfoWidget->hide();
       }
    }

    vPlugins = UiPluginFactory::getInstance()->getPlugins("OcrRecognition");
    foreach (IUiPluginBase * plugin, vPlugins) {
       connect(this,SIGNAL(signalZLMParPushStatus(int)),
                plugin->getWidget(),SLOT(slotZLMParPushStatus(int))); //将推流状态传递给OcrRecognition插件
       connect(m_pOtherSetWidget,SIGNAL(signalSwitchHttp(QString)),
                   plugin->getWidget(),SLOT(slotSwitchHttp(QString)));//向OcrRecognition插件发送http通信地址切换
    }

    vPlugins = UiPluginFactory::getInstance()->getPlugins("TargetTable");
    foreach (IUiPluginBase * plugin, vPlugins) {
       connect(m_pOtherSetWidget,SIGNAL(signalTargetReportAddress(QString,int)),
                   plugin->getWidget(),SLOT(slotTargetReportAddress(QString,int)));//向TargetTable插件发送目标上报IP port通信地址切换
    }


}

/**
 * @brief addWidget
 * 将设置和模型算法两个参数设置主界面按钮对应的UI界面加到垂直布局，根据点击不同按钮显示不同界面。
 * @param name（加入到布局的界面别名） widget（接入到布局的UI界面指针）
 * @return 无返回值
 */
void ParameterSetWidget::addWidget(QString name,QWidget *widget)
{
    ui->verticalLayout->addWidget(widget);
    m_widgetMap[name] = widget;
}

/**
 * @brief slotPara_pushButton
 * 点击参数按钮执行的槽函数(按钮高亮，参数UI界面显示)。
 * @param void
 * @return 无返回值
 */
void ParameterSetWidget::slotPara_pushButton()
{
    ui->Para_pushButton->setChecked(true);
    ui->Model_pushButton->setChecked(false);

    emit signalListClicked(QString::fromLocal8Bit("参数"));
}

/**
 * @brief slotModel_pushButton
 * 点击模型算法按钮执行的槽函数(按钮高亮，模型算法UI界面显示)。
 * @param void
 * @return 无返回值
 */
void ParameterSetWidget::slotModel_pushButton()
{
    ui->Model_pushButton->setChecked(true);
    ui->Para_pushButton->setChecked(false);

    emit signalListClicked(QString::fromLocal8Bit("模型算法"));
}

/**
 * @brief slotCloseUi
 * 点击设置界面中的关闭按钮，界面进行隐藏。
 * @param void
 * @return 无返回值
 */
void ParameterSetWidget::slotCloseUi()
{
    this->close();
}

/**
 * @brief slotListClicked
 * 根据点击加入到布局界面的按钮，显示对应界面。
 * @param name(加入到布局的界面别名)
 * @return 无返回值
 */
void ParameterSetWidget::slotListClicked(QString name)
{
    for(auto itr = m_widgetMap.begin();itr != m_widgetMap.end();++itr)
    {
        itr.value()->hide();
    }

    if(m_widgetMap.find(name) == m_widgetMap.end())
    {
        return;
    }

    m_widgetMap[name]->show();
}

/**
 * @brief slotPluginMsg
 * 接收中转信号槽消息。
 * @param pMsg(中转信息槽参数)
 * @return 无返回值
 */
void ParameterSetWidget::slotPluginMsg(pluginmsg pMsg)
{
    if (pMsg.id == 3 && pMsg.pluginid != m_pPlugin->getId()) {
        m_pOtherSetWidget->slotVideoReportStateChanged(pMsg.state);
    }

    if(pMsg.id == 7)
    {
        if(this->isVisible()){
            this->close();
        }else{
            ui->Para_pushButton->setChecked(true);
            slotUIBtnClicked(0);
            this->show();
            m_pParaContentWidget->slotsendpullStreamList();
        }
    }else{
        if(this->isVisible()){
            if(pMsg.id!=2 && pMsg.id!=3 && pMsg.id!=5 && pMsg.id!=8)
                this->close();
        }

    }

    // 接收目标识别消息用于发送目标识别指令
    if(pMsg.curtype<0||pMsg.curtype>255)
        return;
    if((pMsg.id == 1) && (pMsg.pluginid != m_pPlugin->getId()) && (pMsg.curtype != 4)&& (pMsg.curtype != 6))
    {
        emit signalIdentify(pMsg.id, pMsg.state);
    }
    else if((pMsg.id == 2) && (pMsg.pluginid != m_pPlugin->getId())&& (pMsg.curtype != 4) && (pMsg.curtype != 6))
    {
        emit signalIdentify(pMsg.id, pMsg.state);
    }
}

void ParameterSetWidget::slotVideoReportControl(int state)
{
    if (state != 1 && state != 2) {
        return;
    }

    pluginmsg msg = {};
    msg.pluginid = m_pPlugin->getId();
    msg.type = m_pPlugin->getId();
    msg.curtype = 2;
    msg.id = 3;
    msg.state = state;
    emit signalPluginMsg(msg);
}

/**
 * @brief slotIdentifyRespons
 * 识别结果反馈。
 * @param nType(1:OCR识别 2:目标识别) nStatus(1:失败 2:成功)
 * @return 无返回值
 */
void ParameterSetWidget::slotIdentifyRespons(int nType,int nStatus)
{
    pluginmsg msg;
    msg.pluginid =m_pPlugin->getId();
    msg.id =nType;
    msg.state=nStatus;
    msg.curtype=4;//防止形成死循环
    emit signalPluginMsg(msg);
}

/**
 * @brief slotSwitchBigScreen  接收大屏切换信号，并转发zmq
 * @param json  小屏窗口号
 */
void ParameterSetWidget::slotSwitchBigScreen(QJsonObject json)
{

    qDebug() << __FUNCTION__;
    //JSON文件外层索引
    QJsonObject inputJson;

    //外索引：枚举：车辆挂挡
    inputJson.insert("screen","1");
    BackendData backData;
    backData.ID = 1;
    QJsonDocument doc;
    doc.setObject(json);
    QByteArray ba = doc.toJson(QJsonDocument::Compact);
    backData.DataJson = QString(ba).toStdString();
    if(m_pBackendData)
    {
        m_pBackendData->publishMessage(backData);
        FrameLogManager::getInstance()->log(QString("ParameterSet"),QString::fromLocal8Bit("Send BackendData.ID:%1,BackendData.DataJson:%2")
                                            .arg(QString::number(backData.ID)).arg(QString::fromStdString(backData.DataJson)),IFrameLogManager::DEBUG_TYPE);
    }

}

/**
 * @brief slotUIBtnClicked 界面切换
 * @param index 0流媒体 1桌面推流 2网口设置 3OCR设置 4模型算法 5其他设置 6设备信息
 */
void ParameterSetWidget::slotUIBtnClicked(int index)
{
    m_pParaContentWidget->hide();
    m_pModelContentWidget->hide();
    m_pOcrRecognitionSetWidget->hide();
    m_pScreenPushSetWidget->hide();
    m_pNetworkSetWidget->hide();
    m_pOtherSetWidget->hide();
    if (m_pDeviceInfoWidget) {
        m_pDeviceInfoWidget->hide();
    }
    switch (index) {
    case 0:
        m_pParaContentWidget->show();
        m_pParaContentWidget->slotsendpullStreamList();
        break;
    case 1:
        m_pScreenPushSetWidget->updateStreamInfo();
        m_pScreenPushSetWidget->show();
        break;
    case 2:
        m_pNetworkSetWidget->show();
        break;
    case 3:
        m_pOcrRecognitionSetWidget->show();
        break;
    case 4:
        m_pModelContentWidget->show();
        break;
    case 5:
        m_pOtherSetWidget->show();
        break;
    case 6:
        if (m_pDeviceInfoWidget) {
            m_pDeviceInfoWidget->show();
        }
        break;
    default:
        break;
    }

    m_currentPageIndex = index;

    // 窗口尺寸在 ParameterSet::initUi() 中统一固定，切页仅切换内容。
}

/**
 * @brief slotBackendData 处理大屏切换反馈数据
 */
void ParameterSetWidget::slotBackendData(BackendData data)
{
    if(data.ID==100)
    {
        QJsonParseError parseError;
        QJsonDocument jDoc = QJsonDocument::fromJson(QString::fromStdString(data.DataJson).toUtf8(),&parseError);
        if(parseError.error != QJsonParseError::NoError){
            qDebug() << __FUNCTION__
                     << QString::fromUtf8(u8"输入的std::string转换为QJsonObject出错.error:1.");
            return ;
        }
        if(!jDoc.isObject()){
            qDebug() << __FUNCTION__
                     << QString::fromUtf8(u8"输入的std::string转换为QJsonObject出错.error:2");
            return;
        }

        qDebug()<<"BackendData"<<QString::fromStdString(data.DataJson);
        FrameLogManager::getInstance()->log(QString("ParameterSet"),QString("OnMsgData BackendData.DataJson:%1").arg(QString::fromStdString(data.DataJson)),IFrameLogManager::DEBUG_TYPE);
        if(jDoc.object().isEmpty())
            return;

        QJsonParseError error;
        QJsonObject json = QJsonDocument::fromJson(QString::fromStdString(data.DataJson).toLocal8Bit(),&error).object();
        if(json.contains("screen"))
        {
           int index=json.value("screen").toInt();
        }

    }
}

/**
 * @brief loadQssStyleFile
 * 设置主设置界面Qss样式。
 * @param void
 * @return 无返回值
 */
void ParameterSetWidget::loadQssStyleFile()
{
    QString strPath = m_pPlugin->getStylePath() + QString("/ParameterSetWidget.qss");

    Q_ASSERT(!strPath.isEmpty() && "path is empty");
    QFile textFile(strPath);

    Q_ASSERT(textFile.exists() && "file can't open");
    textFile.open(QIODevice::ReadOnly | QIODevice::Text);
    QTextStream textStream(&textFile);

    QString style = textStream.readAll();
    this->setStyleSheet(style);
}

/**
* @brief onMsgData 函数
* 接收数据
* @param data 前后端自定义协议 。
* @return 无返回值
*/
void ParameterSetWidget::OnMsgData(BackendData data)
{
    emit signalRecBackendData(data);
}

/**
 * @brief slotRecQPointFromMianBoard 从MianBoard插件接收位置信息
 */
void ParameterSetWidget::slotRecQPointFromMianBoard(QPoint pos)
{
    m_pParaContentWidget->showDeviceListWidget(pos);
}
