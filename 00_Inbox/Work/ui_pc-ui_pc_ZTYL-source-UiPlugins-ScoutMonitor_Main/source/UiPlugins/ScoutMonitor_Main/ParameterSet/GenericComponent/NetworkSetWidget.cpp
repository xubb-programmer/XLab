#include "NetworkSetWidget.h"
#include "ui_NetworkSetWidget.h"
#include "FrameLogManager.h"
#include <QSettings>
#include <QTextCodec>
#include <QJsonArray>
#include <QFile>
#include <QRegularExpression>
#include "../Comm/CommunicationIniUtils.h"

QString g_productNum="";

NetworkSetWidget::NetworkSetWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::NetworkSetWidget)
{
    ui->setupUi(this);
}

NetworkSetWidget::~NetworkSetWidget()
{
    delete ui;
}

/**
 * @brief initData
 * 插件数据初始化交互
 * @param void
 * @return 无返回值
 */
void NetworkSetWidget::initUi(IUiPluginBase *plugin)
{
    m_pPlugin = plugin;
    m_pPlugin->setStyle("day");
    initCfg();
    loadQssStyle();
    initData();

    //网口1界面
    m_pNetWorkItem1 = new NetWorkItemWidget(this);
    m_pNetWorkItem1->initUi(m_pPlugin,1);
    ui->gridLayout_Network->addWidget(m_pNetWorkItem1,1,1);

    //网口2界面
    m_pNetWorkItem2 = new NetWorkItemWidget(this);
    m_pNetWorkItem2->initUi(m_pPlugin,2);
    ui->gridLayout_Network->addWidget(m_pNetWorkItem2,1,2);
    // 连接信号槽
    qRegisterMetaType<NetSetData>("NetSetData");
    connect(m_pNetWorkItem1,SIGNAL(signalSendNetsetData(NetSetData)),this,SLOT(slotSendNetsetData(NetSetData)));
    connect(m_pNetWorkItem2,SIGNAL(signalSendNetsetData(NetSetData)),this,SLOT(slotSendNetsetData(NetSetData)));

    m_netData1.netName=QString::fromUtf8(u8"网口1");
    m_netData1.ip="192.168.0.8";
    m_netData1.subnet="192.168.0.1";
    m_netData1.gateway="255.255.255.0";
    m_netData1.status=true;

    m_netData2.netName=QString::fromUtf8(u8"网口2");
    m_netData2.ip="192.168.0.7";
    m_netData2.subnet="192.168.0.1";
    m_netData2.gateway="255.255.255.0";
    m_netData2.status=false;

    m_pNetWorkItem1->updateStatu(m_netData1);
    m_pNetWorkItem2->updateStatu(m_netData2);


}

/**
 * @brief initCfg
 * 读取配置文件获取发送时的基础UI
 * @param void
 * @return 无返回值
 */
void NetworkSetWidget::initCfg()
{
    QSettings settings(QApplication::applicationDirPath() + "/config/communication.ini" , QSettings::IniFormat);
    settings.setIniCodec(QTextCodec::codecForName("UTF-8"));
    settings.beginGroup("HTTP");
    m_strUrl = settings.value("url").toString();
    settings.endGroup();

}

/**
 * @brief initData
 * 插件数据初始化交互
 * @param void
 * @return 无返回值
 */
void NetworkSetWidget::initData()
{
    //判断网口设置是否成功
    m_pNetWorkSet=new QTimer(this);
    m_pNetWorkSet->setInterval(10000);
    connect(m_pNetWorkSet,SIGNAL(timeout()),this,SLOT(slotNetWorkSetTimeout()));

    //判断网络是否在线
    m_pNethttp=new QTimer(this);
    m_pNethttp->setInterval(9000);
    connect(m_pNethttp,SIGNAL(timeout()),this,SLOT(slotHttpTimeout()));

}


/**
 * @brief loadQssStyle
 * 设置界面Qss初始化样式表
 * @param void
 * @return void
 */
void NetworkSetWidget::loadQssStyle()
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

void NetworkSetWidget::slotSendNetsetData(NetSetData netdata)
{
    QJsonObject json;

    if(netdata.netName==QString::fromLocal8Bit("网口1")){
        json[QString::fromLocal8Bit("net_interface")] = 1;
    }
    if(netdata.netName==QString::fromLocal8Bit("网口2")){
        json[QString::fromLocal8Bit("net_interface")] = 2;
    }

    json[QString::fromLocal8Bit("ip_address")] = netdata.ip;
    json[QString::fromLocal8Bit("netmask")] = netdata.subnet;
    json[QString::fromLocal8Bit("gateway")] = netdata.gateway;

    if (netdata.ip.isEmpty() || netdata.subnet.isEmpty() || netdata.gateway.isEmpty()) {
        emit signalMessageInfoText(QString::fromUtf8(u8"网口设置"),
                                   QString::fromUtf8(u8"IP地址、子网掩码或网关不能为空!"),
                                   MessageType_Info);
        return;
    }

    QString url = "http://" + m_strUrl + "/execute-network-script";
    emit signalSendNetSetPara(url,json);

    m_setNetData=netdata;
    if(bIshttp){
        m_pNetWorkSet->start();
         emit signalMessageInfoText(QString::fromUtf8(u8"网口设置"),
                                    QString::fromUtf8(u8"正在设置网络，请稍后!"),
                                    MessageType_Info);
    }


}

/**
 * @brief slotTimeout 判断网口设置是否成功 10s未收到回执默认成功
 */
void NetworkSetWidget::slotNetWorkSetTimeout()
{
    m_pNetWorkSet->stop();
    // 网口设置成功，将修改后的IP存储到本地
    QSettings settings(QApplication::applicationDirPath() + "/config/communication.ini" , QSettings::IniFormat);
    settings.setIniCodec(QTextCodec::codecForName("UTF-8"));
    settings.beginGroup("HTTP");
    settings.setValue("url",m_setNetData.ip+QString::fromLocal8Bit(":3000"));
    settings.endGroup();
    CommunicationIniUtils::syncPreservingPullStreamKeys(settings);

   /* settings.beginGroup("PushStream");
    QString pushStreamip = settings.value("IP").toString();
    if(!pushStreamip.isEmpty()){
        QRegularExpression regex("//.*?:");//匹配替换//与：之间字符
        pushStreamip.replace(regex,"//"+m_setNetData.ip+":");
        settings.setValue("IP",pushStreamip);
    }
    settings.endGroup();*/


    QSettings settings1(QApplication::applicationDirPath() + "/config/VideoProcess.ini" , QSettings::IniFormat);
    settings1.beginGroup("Video");
    QString inputUrl = settings1.value("inputUrl").toString();
    if(!inputUrl.isEmpty()){
        QRegularExpression regex("//.*?:");//匹配替换//与：之间字符
        inputUrl.replace(regex,QString::fromLocal8Bit("//")+m_setNetData.ip+QString::fromLocal8Bit(":"));
        settings1.setValue("inputUrl",inputUrl);
    }
    settings1.endGroup();

    if(!bIshttp){
        emit signalMessageInfoText(QString::fromUtf8(u8"网口设置"),
                                   QString::fromUtf8(u8"网口设置成功，请重启软件!"),
                                   MessageType_Success);
    }

}

/**
 * @brief slotHttpTimeout 判断HTTP是否正常 9s未收到状态默认不在线(4s查询一次)
 */
void NetworkSetWidget::slotHttpTimeout()
{
    m_pNethttp->stop();
    g_productNum="";
    bIshttp=false;
    m_netData1.status=false;
    m_pNetWorkItem1->updateStatu(m_netData1);
    m_netData2.status=false;
    m_pNetWorkItem2->updateStatu(m_netData2);
    emit signalOnLineToSSE("");//向sse发送重连信号
    emit signalproductNum(g_productNum);

}


/**
 * @brief slotFinishedForNetWorkSet
 * 接收网口设置http反馈
 * @param strData（反馈消息内容）
 * @return void
 */
void NetworkSetWidget::slotFinishedForNetWorkSet(QString strData)
{
    FrameLogManager::getInstance()->log("ParameterSet","slotFinishedForNetWorkSet:" + strData,IFrameLogManager::DEBUG_TYPE);
    QTextCodec *tc = QTextCodec::codecForName("UTF-8");
    Q_UNUSED(tc);
    QJsonDocument root_Doc = QJsonDocument::fromJson(strData.toUtf8().data());
    if(root_Doc.isObject())
    {
        bool nCode = root_Doc.object().value("success").toBool();
        if(nCode)
        {
            slotNetWorkSetTimeout();
            emit signalMessageInfoText(QString::fromUtf8(u8"网口设置"),
                                       QString::fromUtf8(u8"网口设置成功，请重启软件!"),
                                       MessageType_Success);
        }
        else
        {
            emit signalMessageInfoText(QString::fromUtf8(u8"网口设置"),
                                       QString::fromUtf8(u8"网口设置失败!"),
                                       MessageType_Warn);
        }
    }
}

/**
 * @brief slotFinishedForgetdeviceStatus
 * 从DeviceManagement插件接收获取设备状态
 * @param strData（反馈消息内容）
 * @return void
 */
void NetworkSetWidget::slotFinishedForgetdeviceStatus(QString strData,int statusCode)
{
    bIshttp=true;
    m_pNethttp->start();
    Q_UNUSED(statusCode);
//    FrameLogManager::getInstance()->log("ParameterSetWidget","slotFinishedForgetdeviceStatus:" + strData,IFrameLogManager::DEBUG_TYPE);
    QTextCodec *tc = QTextCodec::codecForName("UTF-8");
    Q_UNUSED(tc);
    QJsonDocument root_Doc = QJsonDocument::fromJson(strData.toUtf8().data());
//    qDebug()<<"slotFinishedForgetdeviceStatus11111111111"<<strData;
    if(root_Doc.isObject())
       {
           QJsonObject obj=root_Doc.object();
           if(obj.contains(QString::fromLocal8Bit("data")))
           {
               QJsonValue value=obj.value(QString::fromLocal8Bit("data"));
               if(value.isObject()){
                   QJsonObject value_obj=value.toObject();
                   if(value_obj.contains("interfaces")&&value_obj["interfaces"].isArray()){
                       QJsonArray dataArray=value_obj["interfaces"].toArray();
                       //清空数据
                       if(!dataArray.isEmpty()){}

                       g_productNum=value_obj.value("device_id").toString();//产品编号
                       emit signalproductNum(g_productNum);

                       for(const QJsonValue &dataValue : dataArray){
                           if(dataValue.isObject()){
                               QJsonObject data_obj=dataValue.toObject();

                               if((data_obj.contains(QString::fromLocal8Bit("接口名"))) && (data_obj.contains(QString::fromLocal8Bit("IP地址"))) &&
                                  data_obj.contains(QString::fromLocal8Bit("子网掩码")) && data_obj.contains(QString::fromLocal8Bit("网关")) &&
                                  data_obj[QString::fromLocal8Bit("接口名")].isString() && data_obj[QString::fromLocal8Bit("IP地址")].isString() &&
                                  data_obj[QString::fromLocal8Bit("子网掩码")].isString() && data_obj[QString::fromLocal8Bit("网关")].isString()){

                                   if(data_obj[QString::fromLocal8Bit("接口名")].toString().contains(QString::fromLocal8Bit("网口1")))
                                   {
                                       if(m_netData1.ip!=data_obj[QString::fromLocal8Bit("IP地址")].toString() || m_netData1.subnet!=data_obj[QString::fromLocal8Bit("子网掩码")].toString()
                                               || m_netData1.gateway!=data_obj[QString::fromLocal8Bit("网关")].toString() || m_netData1.status!=data_obj[QString::fromLocal8Bit("接口状态")].toBool()){
                                           m_netData1.netName=data_obj[QString::fromLocal8Bit("接口名")].toString();
                                           m_netData1.ip=data_obj[QString::fromLocal8Bit("IP地址")].toString();
                                           m_netData1.subnet=data_obj[QString::fromLocal8Bit("子网掩码")].toString();
                                           m_netData1.gateway=data_obj[QString::fromLocal8Bit("网关")].toString();
                                           m_netData1.status=data_obj[QString::fromLocal8Bit("接口状态")].toBool();
                                           m_pNetWorkItem1->updateStatu(m_netData1);
                                       }
                                   }

                                   if(data_obj[QString::fromLocal8Bit("接口名")].toString().contains(QString::fromLocal8Bit("网口2")))
                                   {
                                       if(m_netData2.ip!=data_obj[QString::fromLocal8Bit("IP地址")].toString() || m_netData2.subnet!=data_obj[QString::fromLocal8Bit("子网掩码")].toString()
                                               || m_netData2.gateway!=data_obj[QString::fromLocal8Bit("网关")].toString() || m_netData2.status!=data_obj[QString::fromLocal8Bit("接口状态")].toBool()){
                                           m_netData2.netName=data_obj[QString::fromLocal8Bit("接口名")].toString();
                                           m_netData2.ip=data_obj[QString::fromLocal8Bit("IP地址")].toString();
                                           m_netData2.subnet=data_obj[QString::fromLocal8Bit("子网掩码")].toString();
                                           m_netData2.gateway=data_obj[QString::fromLocal8Bit("网关")].toString();
                                           m_netData2.status=data_obj[QString::fromLocal8Bit("接口状态")].toBool();
                                           m_pNetWorkItem2->updateStatu(m_netData2);
                                       }
                                   }
                               }

                           }else{
                               FrameLogManager::getInstance()->log("ParameterSetWidget","slotFinishedForgetpullStreamList:Array contains non-string value!" ,IFrameLogManager::DEBUG_TYPE);
                           }
                       }
                   }
               }
           }
       }

}
