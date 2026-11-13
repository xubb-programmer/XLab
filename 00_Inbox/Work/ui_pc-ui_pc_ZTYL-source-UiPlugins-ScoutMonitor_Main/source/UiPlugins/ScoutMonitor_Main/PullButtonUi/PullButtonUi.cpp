#include "PullButtonUi.h"
#include <QScreen>


PullButtonUi* gContent = nullptr;
PullButtonUi::PullButtonUi(QWidget *pParent)
{
    m_pWidget = new PullButtonUiWidget(pParent);
}

//获取UI widget
QWidget* PullButtonUi::getWidget()
{
    return m_pWidget;
}

/**
 * @brief resetQss
 * 切换皮肤
 * @param void
 * @return 无返回值
 */
void PullButtonUi::resetQss(const QString& strStyleName)
{
    m_pWidget->resetQss(strStyleName);
}

/**
 * @brief initUi
 * 初始化界面
 * @param void
 * @return 无返回值
 */
void PullButtonUi::initUi()
{
    m_pWidget->initUi(this);
    QScreen *screen =QApplication::primaryScreen();
    QSize screenSize =screen->size();
    m_pWidget->resize(getRect().size());
    m_pWidget->move((screenSize.width()-getRect().size().width()),
                    (screenSize.height()-132)/2-getRect().size().height()/2);
    m_pWidget->show();
}

/**
 * @brief initData
 * 插件数据初始化交互
 * @param void
 * @return 无返回值
 */
void PullButtonUi::initData()
{
    m_pWidget->initData();
}

/**
 * @brief close
 * 软件退出执行的卸载接口
 * @param void
 * @return 无返回值
 */
void PullButtonUi::close()
{
    delete m_pWidget;
}

/**
 * @brief registerProtocols
 * 注册连接，插件间通信需要进行注册
 * @param void
 * @return 无返回值
 */
void PullButtonUi::registerProtocols()
{
    m_pWidget->registerProtocols();
}

/**
 * @brief version
 * 插件版本号获取
 * @param void
 * @return QString（插件版本信息）
 */
QString PullButtonUi::version()
{
    return QString::fromLocal8Bit("1.0");
}

/**
 * @brief pluginName
 * 插件名称获取
 * @param void
 * @return QString（插件名称）
 */
QString PullButtonUi::pluginName()
{
    return QString::fromLocal8Bit("PullButtonUi");
}

/**
 * @brief introduction
 * 概要描述信息
 * @param void
 * @return QString（插件简介）
 */
QString PullButtonUi::introduction()
{
    return QString::fromLocal8Bit("目标列表显隐功能的按钮插件");
}

/**
 * @brief updateDescribe
 * 更新概要描述
 * @param void
 * @return QString（更新概要描述）
 */
QString PullButtonUi::updateDescribe()
{
    return QString::fromLocal8Bit("完成目标列表显隐功能的按钮插件的基础功能");
}

IUiPluginBase *installUiPlugin(QWidget *pParent)
{
    gContent = new PullButtonUi(pParent);
    return gContent;
}
