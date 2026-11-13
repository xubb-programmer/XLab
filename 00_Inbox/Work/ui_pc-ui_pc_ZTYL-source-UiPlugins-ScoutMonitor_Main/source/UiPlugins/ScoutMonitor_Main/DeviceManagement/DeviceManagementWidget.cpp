#include "DeviceManagementWidget.h"
#include "ui_DeviceManagementWidget.h"
#include <QFile>
#include "UiPluginFactory.h"
#include <QJsonParseError>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSettings>
#include <QTextCodec>
#include <FrameLogManager.h>

DeviceManagementWidget::DeviceManagementWidget(QWidget *parent) :
    CustomMoveWidget(parent),
    ui(new Ui::DeviceManagementWidget)
{
    ui->setupUi(this);
    setTitle(ui->DeviceManagementContent);
    setWindowFlags(Qt::FramelessWindowHint /*| Qt::WindowStaysOnTopHint*/ |Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground,true);
}

DeviceManagementWidget::~DeviceManagementWidget()
{
    delete ui;
}

/**
 * @brief initUi
 * 界面初始化
 * @param pPlugin(插件指针)
 * @return 无返回值
 */
void DeviceManagementWidget::initUi(IUiPluginBase *pPlugin)
{
    m_pPlugin = pPlugin;
    m_pPlugin->setStyle("day");

    initCfg();

    m_pDeviceQTimer = new QTimer(this);
    m_pDeviceQTimer->setInterval(4000);
    m_pDeviceQTimer->start();
    connect(m_pDeviceQTimer,SIGNAL(timeout()),this,SLOT(slotDeviceQTimerOut()));

    m_pCheckOutLineQTimer = new QTimer(this);
    m_pCheckOutLineQTimer->setInterval(10000);
    connect(m_pCheckOutLineQTimer,SIGNAL(timeout()),this,SLOT(slotCheckOutLineQTimerOut()));
    m_pCheckOutLineQTimer->start();

    initComponent();

    // 设备信息作为设置页显示时采用紧凑布局，保持与其他设置页相同的窗口高度。
    ui->verticalLayout_5->setContentsMargins(20, 8, 15, 8);
    ui->verticalLayout_5->setSpacing(4);
    ui->IntelligenceLinkIcon_label->setFixedHeight(34);
    ui->DeviceManageIcon_widget->setFixedSize(105, 123);
    ui->productName_label->setFixedHeight(25);
    ui->productName_value_label->setFixedHeight(25);
    ui->productNumber_label->setFixedHeight(25);
    ui->productNumber_value_label->setFixedHeight(25);
    ui->productDate_label->setFixedHeight(25);
    ui->productDate_value_label->setFixedHeight(25);
    ui->productStatus_label->setFixedHeight(25);
    ui->productStatus_value_label->setFixedHeight(25);
    ui->HDMI_label->setFixedHeight(22);
    ui->Eth_label->setFixedHeight(22);
    ui->HDMI_widget->setFixedHeight(78);
    ui->Eth_widget->setFixedHeight(78);

    HardwareInterface *hardwareWidgets[] = {m_HDMI, m_HDMI1, m_HDMI2, m_Eth, m_Eth1, m_Typc};
    for (HardwareInterface *hardwareWidget : hardwareWidgets) {
        if (hardwareWidget) {
            hardwareWidget->setFixedSize(280, 78);
        }
    }

    loadQssStyleFile();
}

/**
 * @brief initCfg
 * 读取配置文件获取发送时的基础UI
 * @param void
 * @return 无返回值
 */
void DeviceManagementWidget::initCfg()
{
    QSettings settings(QApplication::applicationDirPath() + "/config/communication.ini" , QSettings::IniFormat);
    settings.setIniCodec(QTextCodec::codecForName("GBK"));
    settings.beginGroup("HTTP");
    m_strUrl = settings.value("url").toString();
    settings.endGroup();
}

/**
 * @brief initComponent
 * 初始化控制组成
 * @param void
 * @return 无返回值
 */
void DeviceManagementWidget::initComponent()
{
    updateProductNameDisplay(QString::fromUtf8(u8"智图影链"));
    ui->productNumber_value_label->setText(QString::fromLocal8Bit("--"));
    ui->productDate_value_label->setText(QString::fromLocal8Bit("--"));

    m_HDMI = new HardwareInterface(ui->DeviceManagementContent);
    if(m_HDMI)
    {
        m_HDMI->initUi(m_pPlugin);
        m_HDMI->setIcon("HDMI","Outline");
        m_HDMI->setDeviceName("HDMI");
        m_HDMI->setDevice1Name(QString::fromUtf8(u8"采集状态："));
        m_HDMI->setDevice1Value(QString::fromLocal8Bit("--"));
        m_HDMI->hideDevice2Name();
        m_HDMI->hideDevice2Value();
        ui->horizontalLayout_8->addWidget(m_HDMI);
    }

    m_HDMI1 = new HardwareInterface(ui->DeviceManagementContent);
    if(m_HDMI1)
    {
        m_HDMI1->initUi(m_pPlugin);
        m_HDMI1->setIcon("HDMI","Outline");
        m_HDMI1->setDeviceName("HDMI1");
        m_HDMI1->setDevice1Name(QString::fromUtf8(u8"采集状态："));
        m_HDMI1->setDevice1Value(QString::fromLocal8Bit("--"));
        m_HDMI1->hideDevice2Name();
        m_HDMI1->hideDevice2Value();
        ui->horizontalLayout_8->addWidget(m_HDMI1);
    }

    m_HDMI2 = new HardwareInterface(ui->DeviceManagementContent);
    if(m_HDMI2)
    {
        m_HDMI2->initUi(m_pPlugin);
        m_HDMI2->setIcon("HDMI","Outline");
        m_HDMI2->setDeviceName("HDMI2");
        m_HDMI2->setDevice1Name(QString::fromUtf8(u8"采集状态："));
        m_HDMI2->setDevice1Value(QString::fromLocal8Bit("--"));
        m_HDMI2->hideDevice2Name();
        m_HDMI2->hideDevice2Value();
        ui->horizontalLayout_8->addWidget(m_HDMI2);
    }

    m_Eth = new HardwareInterface(ui->DeviceManagementContent);
    if(m_Eth)
    {
        m_Eth->initUi(m_pPlugin);
        m_Eth->setIcon("ETH","Outline");
        m_Eth->setDeviceName(QString::fromUtf8(u8"网口"));
        m_Eth->setDevice1Name(QString::fromUtf8(u8"传输速率："));
        m_Eth->setDevice1Value(QString::fromLocal8Bit("--"));
        m_Eth->hideDevice2Name();
        m_Eth->hideDevice2Value();
        ui->horizontalLayout_9->addWidget(m_Eth);
    }

    m_Eth1 = new HardwareInterface(ui->DeviceManagementContent);
    if(m_Eth1)
    {
        m_Eth1->initUi(m_pPlugin);
        m_Eth1->setIcon("ETH","Outline");
        m_Eth1->setDeviceName(QString::fromUtf8(u8"网口"));
        m_Eth1->setDevice1Name(QString::fromUtf8(u8"传输速率："));
        m_Eth1->setDevice1Value(QString::fromLocal8Bit("--"));
        m_Eth1->hideDevice2Name();
        m_Eth1->hideDevice2Value();
        ui->horizontalLayout_9->addWidget(m_Eth1);
    }

    m_Typc = new HardwareInterface(ui->DeviceManagementContent);
    if(m_Typc)
    {
        m_Typc->initUi(m_pPlugin);
        m_Typc->setIcon("TYPE-C","Outline");
        m_Typc->setDeviceName("TYPE-C");
        m_Typc->setDevice1Name(QString::fromUtf8(u8"电压："));
        m_Typc->setDevice1Value(QString::fromLocal8Bit("--"));
        m_Typc->setDevice2Name(QString::fromUtf8(u8"电流："));
        m_Typc->setDevice2Value(QString::fromLocal8Bit("--"));
        m_Typc->hideDeviceIcon();
        ui->horizontalLayout_9->addWidget(m_Typc);
    }
}

/**
 * @brief initData
 * 初始化数据
 * @param void
 * @return 无返回值
 */
void DeviceManagementWidget::initData()
{
    // 创建工作线程和工作对象
    m_worker = new HttpWorker();

    // 连接信号槽
    connect(this, &DeviceManagementWidget::signalGetDeviceInfo, m_worker, &HttpWorker::doGetWork);
    connect(m_worker, &HttpWorker::requestFinishedForDeviceStatus, this, &DeviceManagementWidget::slotFinishedForDeviceStatus);
    connect(ui->DeviceManageClose_pushButton,SIGNAL(clicked()),this,SLOT(slotCloseUi()));
    connect(this,SIGNAL(signalFlushUi()),this,SLOT(slotFlushUi()));
    connect(this,SIGNAL(signalDeviceStatusShow(bool)),this,SLOT(slotDeviceStatusShow(bool)));
}

/**
 * @brief resetQss
 * 切换皮肤
 * @param void
 * @return 无返回值
 */
void DeviceManagementWidget::resetQss(const QString& strStyleName)
{
    //设置换肤路径
    m_pPlugin->setStyle(strStyleName);
    loadQssStyleFile();
}

/**
 * @brief registerProtocols
 * 注册连接
 * @param void
 * @return 无返回值
 */
void DeviceManagementWidget::registerProtocols()
{
    std::vector<IUiPluginBase *> vPlugins = UiPluginFactory::getInstance()->getPlugins("SignalTransit");
    foreach (IUiPluginBase * plugin, vPlugins) {
        connect(plugin->getWidget(),SIGNAL(signalPluginMsg(pluginmsg)),this,SLOT(slotPluginMsg(pluginmsg)));
        connect(this,SIGNAL(signalPluginMsg(pluginmsg)),plugin->getWidget(),SIGNAL(signalPluginMsg(pluginmsg)));
    }

}

/**
 * @brief loadQssStyleFile
 * 设置Qss消息
 * @param void
 * @return 无返回值
 */
void DeviceManagementWidget::loadQssStyleFile()
{
    QString strPath = m_pPlugin->getStylePath() + QString("/DeviceManagementWidget.qss");

    Q_ASSERT(!strPath.isEmpty() && "path is empty");
    QFile textFile(strPath);

    Q_ASSERT(textFile.exists() && "file can't open");
    textFile.open(QIODevice::ReadOnly | QIODevice::Text);
    QTextStream textStream(&textFile);

    QString style = textStream.readAll();
    this->setStyleSheet(style);
}

/**
 * @brief setDeviceOutline
 * 设备离线
 * @param void
 * @return 无返回值
 */
void DeviceManagementWidget::setDeviceOutline()
{
    updateProductNameDisplay(QString::fromUtf8(u8"智图影链"));
    ui->productNumber_value_label->setText(QString::fromLocal8Bit("--"));
    ui->productDate_value_label->setText(QString::fromLocal8Bit("--"));
    ui->productStatus_value_label->setText(QString::fromUtf8(u8"离线"));
    if(m_HDMI)
    {
        m_HDMI->setIcon("HDMI","Outline");
        m_HDMI->setDeviceName("HDMI_IN");
        m_HDMI->setDevice1Value(QString::fromLocal8Bit("--"));
    }

    if(m_HDMI1)
    {
        m_HDMI1->setIcon("HDMI","Outline");
        m_HDMI1->setDeviceName("HDMI_OUT1");
        m_HDMI1->setDevice1Value(QString::fromLocal8Bit("--"));
    }

    if(m_HDMI2)
    {
        m_HDMI2->setIcon("HDMI","Outline");
        m_HDMI2->setDeviceName("HDMI_OUT2");
        m_HDMI2->setDevice1Value(QString::fromLocal8Bit("--"));
    }


    if(m_Eth)
    {
        m_Eth->setIcon("ETH","Outline");
        m_Eth->setDeviceName(QString::fromUtf8(u8"网口1"));
        m_Eth->setDevice1Value("--");
    }

    if(m_Eth1)
    {
        m_Eth1->setIcon("ETH","Outline");
        m_Eth1->setDeviceName(QString::fromUtf8(u8"网口2"));
        m_Eth1->setDevice1Value("--");
    }

    m_vInterFacesData.clear();
}

/**
 * @brief setMenuButtonStatus
 * 设置一级菜单按钮状态。
 * @param nId(按钮编号) nStatus(1:未开启 2:开启种)
 * @return 无返回值
 */
void DeviceManagementWidget::setMenuButtonStatus(int nId,int nStatus)
{
    pluginmsg msg;
    msg.pluginid =m_pPlugin->getId();
    msg.id = nId;
    msg.state=nStatus;
    emit signalPluginMsg(msg);
}


/**
 * @brief slotSwitchHttp 从ParameterSet插件接收http通信地址切换
 * @param address
 */
void DeviceManagementWidget::slotSwitchHttp(QString address)
{
    if(address==""/* || m_strUrl==address*/)
        return;

    emit signalDeviceStatusShow(false);
    setDeviceOutline();
    m_strUrl=address;
}

/**
 * @brief slotPluginMsg
 * 中转信号槽处理
 * @param pMsg(消息内容)
 * @return 无返回值
 */
void DeviceManagementWidget::slotPluginMsg(pluginmsg pMsg)
{
    if(pMsg.id == 6)
    {

        if(this->isVisible())
        {
            this->close();

        }else
        {
            this->show();
        }
        return;
    }
    else
    {
        if(!this->isVisible())
        {
            return;
        }

        if(pMsg.id!=2 && pMsg.id!=3 && pMsg.id!=5 && pMsg.id!=8)
            this->close();
    }
}

/**
 * @brief slotDeviceQTimerOut
 * 定时发送设备状态查询指令
 * @param void
 * @return 无返回值
 */
void DeviceManagementWidget::slotDeviceQTimerOut()
{
    // 设置请求URL
    QString strUrl = "http://" + m_strUrl + "/proxy/status/deviceStatus";
    emit signalGetDeviceInfo(strUrl);
}

/**
 * @brief slotCheckOutLineQTimerOut
 * 超时将设备置为离线状态
 * @param void
 * @return 无返回值
 */
void DeviceManagementWidget::slotCheckOutLineQTimerOut()
{
    FrameLogManager::getInstance()->log("DeviceManagementWidget",QString::fromUtf8(u8"将设备置为离线状态。"),IFrameLogManager::DEBUG_TYPE);
    m_pCheckOutLineQTimer->stop();
    emit signalDeviceStatusShow(false);
    setDeviceOutline();
}

/**
 * @brief slotFinishedForDeviceStatus
 * http请求后的返回信息处理
 * @param void
 * @return 无返回值
 */
void DeviceManagementWidget::slotFinishedForDeviceStatus(QString strData,int statusCode)
{
    Q_UNUSED(statusCode);
    FrameLogManager::getInstance()->log("DeviceManagementWidget",strData,IFrameLogManager::DEBUG_TYPE);
    m_vInterFacesData.clear();
    QTextCodec *tc = QTextCodec::codecForName("UTF-8");
    Q_UNUSED(tc);
    QJsonDocument root_Doc = QJsonDocument::fromJson(strData.toUtf8().data());
    emit signalDeviceStatusToParameterSet(strData,statusCode);//将设备状态数据发送给ParameterSet插件
    if(root_Doc.isObject())
    {
        int nCode = root_Doc.object().value("code").toString().toInt();
        if(nCode == 0)
        {
            m_pCheckOutLineQTimer->start();
            QJsonObject strDeviceStatus = root_Doc.object().value("data").toObject();
            bool bOnline = strDeviceStatus.value("online").toBool();
            emit  signalDeviceStatusShow(bOnline);

            bool bRecognition = strDeviceStatus.value("recognition").toBool();
            if(bRecognition && !m_Recognition)
            {
                m_Recognition = true;

                setMenuButtonStatus(2,2);
            }
            else if(!bRecognition && m_Recognition)
            {
                m_Recognition = false;

                setMenuButtonStatus(2,1);
            }

            m_productInfo.productName=strDeviceStatus.value("device_name").toString();//产品名称
            m_productInfo.productNum=strDeviceStatus.value("device_id").toString();//产品编号
            m_productInfo.productTime=strDeviceStatus.value("production_time").toString();//生产日期

            QJsonArray arrayInterfaces = strDeviceStatus.value("interfaces").toArray();
            for(int i = 0; i < arrayInterfaces.size(); i++)
            {
                InterFacesData tempData;
                // JSON 文本是 UTF-8，字段名不能依赖构建机的系统代码页。
                tempData.InterfaceName = arrayInterfaces.at(i).toObject().value(QString::fromUtf8(u8"接口名")).toString();
                tempData.IsOnline = arrayInterfaces.at(i).toObject().value(QString::fromUtf8(u8"接口状态")).toBool();
                tempData.TransSpeed = arrayInterfaces.at(i).toObject().value(QString::fromUtf8(u8"传输速率")).toString();
                m_vInterFacesData.push_back(tempData);

                // 刷新界面显示信息
                emit signalFlushUi();
            }
        }
    }
}

/**
 * @brief slotFlushUi
 * 刷新界面显示信息
 * @param void
 * @return 无返回值
 */
void DeviceManagementWidget::slotFlushUi()
{
    updateProductNameDisplay(m_productInfo.productName);
    ui->productNumber_value_label->setText(m_productInfo.productNum);
    ui->productDate_value_label->setText(m_productInfo.productTime);

    for(int n = 0 ; n < m_vInterFacesData.size(); ++n)
    {
        InterFacesData tempData = m_vInterFacesData.at(0);
        if (tempData.InterfaceName == "HDMI_IN") {

            emit signalHDMIINStatusShow(tempData.IsOnline);
            if(!tempData.IsOnline)
            {
                if(m_HDMI)
                {
                    m_HDMI->setIcon("HDMI","Outline");
                }
            }
            else
            {
                if(m_HDMI)
                {
                    m_HDMI->setIcon("HDMI","Online");
                }
            }
            if(m_HDMI)
            {
               m_HDMI->setDeviceName("HDMI_IN");
            }


            if(m_HDMI)
            {
                m_HDMI->setDevice1Value(tempData.TransSpeed);
            }
        }
        else if (tempData.InterfaceName == "HDMI_OUT1") {
            if(!tempData.IsOnline)
            {
                if(m_HDMI1)
                {
                    m_HDMI1->setIcon("HDMI","Outline");
                }

            }
            else
            {
                if(m_HDMI1)
                {
                    m_HDMI1->setIcon("HDMI","Online");
                }
            }
            if(m_HDMI1)
            {
                m_HDMI1->setDeviceName("HDMI_OUT1");
            }


            if(tempData.IsOnline)
            {
                if(m_HDMI1)
                {
                    m_HDMI1->setDevice1Value(QString::fromUtf8(u8"输出中"));
                }
            }
            else
            {
                if(m_HDMI1)
                {
                    m_HDMI1->setDevice1Value(QString::fromUtf8(u8"未采集"));
                }
            }
        }
        else if (tempData.InterfaceName == "HDMI_OUT2") {
            if(!tempData.IsOnline)
            {
                if(m_HDMI2)
                {
                    m_HDMI2->setIcon("HDMI","Outline");
                }
            }
            else
            {
                if(m_HDMI2)
                {
                    m_HDMI2->setIcon("HDMI","Online");
                }
            }
            if(m_HDMI2)
            {
                m_HDMI2->setDeviceName("HDMI_OUT2");
            }

            if(tempData.IsOnline)
            {
                if(m_HDMI2)
                {
                    m_HDMI2->setDevice1Value(QString::fromUtf8(u8"输出中"));
                }
            }
            else
            {
                if(m_HDMI2)
                {
                    m_HDMI2->setDevice1Value(QString::fromUtf8(u8"未采集"));
                }
            }
        }
        else if (tempData.InterfaceName == QString::fromUtf8(u8"网口1")) {
            if(!tempData.IsOnline)
            {
                if(m_Eth)
                {
                    m_Eth->setIcon("ETH","Outline");
                }
            }
            else
            {
                if(m_Eth)
                {
                    m_Eth->setIcon("ETH","Online");
                }
            }
            if(m_Eth)
            {
                m_Eth->setDeviceName(QString::fromUtf8(u8"网口1"));
                m_Eth->setDevice1Value(tempData.TransSpeed);
            }
        }
        else if (tempData.InterfaceName == QString::fromUtf8(u8"网口2")) {
            if(!tempData.IsOnline)
            {
                if(m_Eth1)
                {
                    m_Eth1->setIcon("ETH","Outline");
                }
            }
            else
            {
                if(m_Eth1)
                {
                    m_Eth1->setIcon("ETH","Online");
                }
            }
            if(m_Eth1)
            {
                m_Eth1->setDeviceName(QString::fromUtf8(u8"网口2"));
                m_Eth1->setDevice1Value(tempData.TransSpeed);
            }
        }
    }
    m_vInterFacesData.clear();
}

void DeviceManagementWidget::updateProductNameDisplay(const QString &fullName)
{
    const QString deviceName = fullName.trimmed();
    ui->productName_value_label->setToolTip(deviceName);
    const int availableWidth = qMax(0, ui->productName_value_label->width() - 2);
    ui->productName_value_label->setText(
                ui->productName_value_label->fontMetrics().elidedText(
                    deviceName, Qt::ElideRight, availableWidth));
}

/**
 * @brief slotDeviceStatusShow
 * 刷新设备在线状态
 * @param bStatus(设备在线状态)
 * @return 无返回值
 */
void DeviceManagementWidget::slotDeviceStatusShow(bool bStatus)
{
    if(bStatus)
    {
        ui->productStatus_value_label->setText(QString::fromUtf8(u8"在线"));
    }
    else
    {
        ui->productStatus_value_label->setText(QString::fromUtf8(u8"离线"));
    }
}

/**
 * @brief slotCloseUi
 * 关闭弹窗
 * @param void
 * @return 无返回值
 */
void DeviceManagementWidget::slotCloseUi()
{
    this->close();
}
