#include "SignalTransitWidget.h"
#include "ICanServerData.h"
#include "FrameLogManager.h"
#include <QDebug>
#include <UiPluginFactory.h>

SignalTransitWidget::SignalTransitWidget(QWidget *parent) :
    QWidget(parent)
{
}

void SignalTransitWidget::initData()
{
    m_lastTime = QDateTime::currentDateTime();
    m_pData = getCanServerDataInstance(0x17,QCoreApplication::applicationDirPath() + "/dds");
    if(m_pData)
    m_pData->registListener(this);
    m_WGstate.bWG2 = true;
    emit signalWGState(&m_bWG);
    emit signalInitMutual(&m_MutualExclusive);
}

//注册连接
void SignalTransitWidget::registerProtocols()
{
    std::vector<IUiPluginBase *> vPlugins  = UiPluginFactory::getInstance()->getPlugins("MessageBox");
    foreach (IUiPluginBase * plugin, vPlugins) {
        connect(this,SIGNAL(signalHintMessage(QString,QString)),plugin->getWidget(),SLOT(onHitleAndTitle(QString,QString)));
    }
}

void SignalTransitWidget::OnMsgData(CanServerData data)
{

    //升降桅杆动作命令
    unsigned char dzml[2];
    dzml[0] = 0x0d;
    dzml[1] =0x01;
    if(memcmp(dzml,data.Data,2) == 0 && data.ID == 0x81022FA)
    {

        if(data.Data[2] == 0x03 && data.Data[8] == 0x00)
        {

            m_WGstate.bWG2 = false;

            FrameLogManager::getInstance()->log("SignalTransitWidget",QString::fromLocal8Bit("bWG2:false"),
                                                IFrameLogManager::DEBUG_TYPE);
        }
        FrameLogManager::getInstance()->log("SignalTransitWidget",QString::fromLocal8Bit("升降桅杆状态:%1").arg((int)data.Data[2]),
                                            IFrameLogManager::DEBUG_TYPE);
        m_bWG = m_WGstate.bWG1 * m_WGstate.bWG2;
    }


    //升降桅杆状态上报命令
    unsigned char ztsb[2];
    ztsb[0] = 0x0d;
    ztsb[1] = 0x02;
    if(memcmp(ztsb,data.Data,2) == 0 && data.ID == 0x88002fa)
    {
        if(data.Data[4] == 0x71 || data.Data[4] == 0x73 || data.Data[4] == 0x79 || data.Data[4] == 0x7B || data.Data[4] == 0x7D || data.Data[4] == 0x7F || data.Data[4] == 0xA0)
        {
            m_WGstate.bWG2 = true;
            FrameLogManager::getInstance()->log("SignalTransitWidget",QString::fromLocal8Bit("bWG2:true"),
                                                IFrameLogManager::DEBUG_TYPE);
        }


        if(data.Data[4]== 0x76 || data.Data[4] == 0x75 || data.Data[4] == 0x77)
        {
             m_WGstate.bWG2 = true;
             FrameLogManager::getInstance()->log("SignalTransitWidget",QString::fromLocal8Bit("bWG2:true"),
                                                 IFrameLogManager::DEBUG_TYPE);
        }
        FrameLogManager::getInstance()->log("SignalTransitWidget",QString::fromLocal8Bit("升降桅杆状态:%1").arg((int)data.Data[4]),
                                            IFrameLogManager::DEBUG_TYPE);
        m_bWG = m_WGstate.bWG1 * m_WGstate.bWG2;
    }


    //倒伏调平工作状态查询应答
    unsigned char eqpDFTP[2];
    eqpDFTP[0] = 0x03;
    eqpDFTP[1] = 0x02;
    //倒伏调平工作状态查询应答
    if(memcmp(eqpDFTP,data.Data,2) == 0 && data.ID == 0x181022E6)
    {
        short sFy = 0;
        //当前俯仰角度D3_D4
        unsigned char data3_4[2] = "0";
        data3_4[1] = data.Data[4];
        data3_4[0] = data.Data[5];
        memcpy(&sFy,data3_4,2);

        if(sFy > -3000 && sFy < 3000)
        {
            m_WGstate.bWG1 = true;
        }
        else
        {
            m_WGstate.bWG1 = false;
        }
        FrameLogManager::getInstance()->log("SignalTransitWidget",QString::fromLocal8Bit("倒伏角度:%1").arg((float)sFy/100),
                                            IFrameLogManager::DEBUG_TYPE);
        m_bWG = m_WGstate.bWG1 * m_WGstate.bWG2;
    }
}

//按钮被点击 -- 筛选一次操作后在执行
void SignalTransitWidget::sltBtnClicked(QString name)
{
//    if(m_lastTime.msecsTo(QDateTime::currentDateTime()) < 1000){//点击太快了不响应
//        qDebug() << "ffffffffffffffffff" << name;
//        return;
//    }

//    m_lastTime = QDateTime::currentDateTime();
//    if(m_MutualExclusive.bGDZSSS && name != QString::fromLocal8Bit("光电探测")){
//        emit signalHintMessage(QString::fromLocal8Bit("提示"),QString::fromLocal8Bit("正在执行光电探测操作，请关闭后继续操作！"));
//        return;
//    }
//    if(m_MutualExclusive.bMYZSSS && name != QString::fromLocal8Bit("猫眼探测")){
//        emit signalHintMessage(QString::fromLocal8Bit("提示"),QString::fromLocal8Bit("正在执行猫眼探测操作，请关闭后继续操作！"));
//        return;
//    }

    emit btnClicked(name);
}

void SignalTransitWidget::sltPluginMsg(pluginmsg msg)
{
    emit signalPluginMsg(msg);
}
