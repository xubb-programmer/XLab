#include "SignalTransit.h"

SignalTransit* gContent = nullptr;
SignalTransit::SignalTransit(QWidget *pParent)
{
    m_pWidget = new SignalTransitWidget(pParent);
    m_pWidget->hide();
}

QWidget *SignalTransit::getWidget()
{
    return m_pWidget;
}

void SignalTransit::resetQss(const QString &strStyleName)
{

}

//初始化界面
void SignalTransit::initUi()
{
}

//初始化数据交互
void SignalTransit::initData()
{
    m_pWidget->initData();
}

void SignalTransit::close()
{
 //析构前准备，释放内存时在析构里释放即可，最好不要在此释放
}

void SignalTransit::registerProtocols()
{
    m_pWidget->registerProtocols();
}

QString SignalTransit::version()
{
    return QString::fromLocal8Bit("1.0");
}

QString SignalTransit::pluginName()
{
    return QString::fromLocal8Bit("SignalTransit");
}

QString SignalTransit::introduction()
{
    return QString::fromLocal8Bit("信号转接插件,转接需要由多处地方统一发的相同信号");
}

QString SignalTransit::updateDescribe()
{
    return QString::fromLocal8Bit("");
}

IUiPluginBase *installUiPlugin(QWidget *pParent)
{
    gContent = new SignalTransit(pParent);
    return gContent;
}
