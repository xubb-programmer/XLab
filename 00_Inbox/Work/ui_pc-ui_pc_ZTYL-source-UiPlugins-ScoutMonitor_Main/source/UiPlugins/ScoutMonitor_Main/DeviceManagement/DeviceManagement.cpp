#include "DeviceManagement.h"
#include <QScreen>

DeviceManagement* gContent = nullptr;
DeviceManagement::DeviceManagement(QWidget *pParent)
{
    m_pWidget = new DeviceManagementWidget(pParent);
}

//获取UI widget
QWidget* DeviceManagement::getWidget()
{
    return m_pWidget;
}

//切换皮肤
void DeviceManagement::resetQss(const QString& strStyleName)
{
    m_pWidget->resetQss(strStyleName);
}

//初始化界面
void DeviceManagement::initUi()
{
    m_pWidget->initUi(this);
    QScreen *screen =QApplication::primaryScreen();
    QSize screenSize =screen->size();
    m_pWidget->resize(getRect().size());
    m_pWidget->move((screenSize.width()/2-getRect().size().width()/2),
                    screenSize.height()/2-getRect().size().height()/2);
    m_pWidget->hide();
}

//初始化数据交互
void DeviceManagement::initData()
{
    m_pWidget->initData();
}

//退出
void DeviceManagement::close()
{
    delete m_pWidget;
}

//注册连接
void DeviceManagement::registerProtocols()
{
    m_pWidget->registerProtocols();
}

//版本号
QString DeviceManagement::version()
{
    return QString::fromLocal8Bit("1.0");
}

//插件名称
QString DeviceManagement::pluginName()
{
    return QString::fromLocal8Bit("DeviceManagement");
}

//概要描述信息
QString DeviceManagement::introduction()
{
    return QString::fromUtf8(u8"设备管理插件");
}

//更新概要描述
QString DeviceManagement::updateDescribe()
{
    return QString::fromUtf8(u8"完成设备管理插件的基础功能");
}

IUiPluginBase *installUiPlugin(QWidget *pParent)
{
    gContent = new DeviceManagement(pParent);
    return gContent;
}
