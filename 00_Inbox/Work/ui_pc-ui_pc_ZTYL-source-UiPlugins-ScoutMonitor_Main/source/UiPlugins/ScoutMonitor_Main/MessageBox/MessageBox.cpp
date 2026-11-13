#include "MessageBox.h"

MessageBox* gContent = nullptr;
MessageBox::MessageBox(QWidget *pParent)
{
    m_pWidget = new MessageBoxWidget(pParent->topLevelWidget());
}

QWidget *MessageBox::getWidget()
{
    return m_pWidget;
}

void MessageBox::resetQss(const QString &strStyleName)
{
    m_pWidget->resetQss(strStyleName);
}

//初始化界面
void MessageBox::initUi()
{
    m_pWidget->init(this);
    m_pWidget->resetQss("day");
    m_pWidget->hide();
    m_pWidget->resize(getRect().size());
    m_pWidget->move(getRect().x(),getRect().y());
}

//初始化数据交互
void MessageBox::initData()
{
    m_pWidget->initData();
}

void MessageBox::close()
{
    //析构前准备，释放内存时在析构里释放即可，最好不要在此释放
}

void MessageBox::registerProtocols()
{
    m_pWidget->registerProtocols();
}

QString MessageBox::version()
{
    return QString::fromLocal8Bit("1.0");
}

QString MessageBox::pluginName()
{
    return QString::fromLocal8Bit("MessageBox");
}

QString MessageBox::introduction()
{
    return QString::fromLocal8Bit("提示弹窗");
}

QString MessageBox::updateDescribe()
{
    return QString::fromLocal8Bit("完成提示基础功能");
}

IUiPluginBase *installUiPlugin(QWidget *pParent)
{
    gContent = new MessageBox(pParent);
    return gContent;
}
