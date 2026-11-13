#include "ParameterSet.h"
#include <QScreen>


ParameterSet* gContent = nullptr;
ParameterSet::ParameterSet(QWidget *pParent)
{
    m_pWidget = new ParameterSetWidget(pParent);
}

//获取UI widget
QWidget* ParameterSet::getWidget()
{
    return m_pWidget;
}

/**
 * @brief resetQss
 * 切换皮肤
 * @param void
 * @return 无返回值
 */
void ParameterSet::resetQss(const QString& strStyleName)
{
    m_pWidget->resetQss(strStyleName);
}

/**
 * @brief initUi
 * 初始化界面
 * @param void
 * @return 无返回值
 */
void ParameterSet::initUi()
{
    m_pWidget->initUi(this);
    QScreen *screen =QApplication::primaryScreen();
    QSize screenSize =screen->size();
    const QSize windowSize = getRect().size();
    // 设置窗口尺寸只由插件配置决定，子页面的 sizeHint/最小尺寸不能在切页时撑大窗口。
    m_pWidget->setFixedSize(windowSize);
    m_pWidget->move((screenSize.width()/2-windowSize.width()/2),
                    screenSize.height()/2-windowSize.height()/2);

    m_pWidget->hide();
}

/**
 * @brief initData
 * 插件数据初始化交互
 * @param void
 * @return 无返回值
 */
void ParameterSet::initData()
{
    m_pWidget->initData();
}

/**
 * @brief close
 * 软件退出执行的卸载接口
 * @param void
 * @return 无返回值
 */
void ParameterSet::close()
{
    delete m_pWidget;
}

/**
 * @brief registerProtocols
 * 注册连接，插件间通信需要进行注册
 * @param void
 * @return 无返回值
 */
void ParameterSet::registerProtocols()
{
    m_pWidget->registerProtocols();
}

/**
 * @brief version
 * 插件版本号获取
 * @param void
 * @return QString（插件版本信息）
 */
QString ParameterSet::version()
{
    return QString::fromLocal8Bit("1.0");
}

/**
 * @brief pluginName
 * 插件名称获取
 * @param void
 * @return QString（插件名称）
 */
QString ParameterSet::pluginName()
{
    return QString::fromLocal8Bit("ParameterSet");
}

/**
 * @brief introduction
 * 概要描述信息
 * @param void
 * @return QString（插件简介）
 */
QString ParameterSet::introduction()
{
    return QString::fromLocal8Bit("参数设置插件");
}

/**
 * @brief updateDescribe
 * 更新概要描述
 * @param void
 * @return QString（更新概要描述）
 */
QString ParameterSet::updateDescribe()
{
    return QString::fromLocal8Bit("完成参数设置插件的基础功能");
}

IUiPluginBase *installUiPlugin(QWidget *pParent)
{
    gContent = new ParameterSet(pParent);
    return gContent;
}
