#include "TargetTable.h"
#include <QScreen>
#include "LicenseCheck.h"
#include <QMessageBox>

TargetTable* gContent = nullptr;


#ifdef WIN32
static QString toQString(const std::string &text)
{
    if (text.empty()) {
        return QString();
    }
    return QString::fromLocal8Bit(text.c_str());
}

static bool checkLicense(QString *message)
{
    std::string error;
    const bool ok = regist(&error);
    if (message) {
        *message = ok ? QString::fromLocal8Bit("授权有效")
                      : (error.empty() ? QString::fromLocal8Bit("授权无效")
                                       : toQString(error));
    }
    return ok;
}
#endif

TargetTable::TargetTable(QWidget *pParent)
{
// #if defined(WIN32) && !defined(_WIN64)
//     QString Message;
//     if (!checkLicense(&Message)) {
//         QMessageBox::warning(nullptr,
//                              QString::fromLocal8Bit("提示"),
//                              QString::fromLocal8Bit("目标插件加载失败！") );
//         return ;
//     }
// #endif

    m_pWidget = new TargetTableWidget(pParent);
}

//获取UI widget
QWidget* TargetTable::getWidget()
{
    return m_pWidget;
}

/**
 * @brief initUi
 * 切换皮肤
 * @param void
 * @return 无返回值
 */
void TargetTable::resetQss(const QString& strStyleName)
{
    if(!m_pWidget)
        return;

    m_pWidget->resetQss(strStyleName);
}

/**
 * @brief initUi
 * 初始化界面
 * @param void
 * @return 无返回值
 */
void TargetTable::initUi()
{

    if(!m_pWidget)
        return;

    m_pWidget->initUi(this);
    QScreen *screen =QApplication::primaryScreen();
    QSize screenSize =screen->size();
    m_pWidget->resize(250*(((double)(screenSize.width()-768)/screenSize.width())+1),(screenSize.height())-88);
    m_pWidget->move((screenSize.width()-(250*(((double)(screenSize.width()-768)/screenSize.width())+1))),0);
    m_pWidget->hide();
}

/**
 * @brief initUi
 * 插件数据初始化交互
 * @param void
 * @return 无返回值
 */
void TargetTable::initData()
{
    if(!m_pWidget)
        return;

    m_pWidget->initData();
}

/**
 * @brief close
 * 软件退出执行的卸载接口
 * @param void
 * @return 无返回值
 */
void TargetTable::close()
{
    delete m_pWidget;
}

/**
 * @brief registerProtocols
 * 注册连接，插件间通信需要进行注册
 * @param void
 * @return 无返回值
 */
void TargetTable::registerProtocols()
{
    if(!m_pWidget)
        return;
    m_pWidget->registerProtocols();
}

/**
 * @brief version
 * 插件版本号获取
 * @param void
 * @return QString（插件版本信息）
 */
QString TargetTable::version()
{
    return QString::fromLocal8Bit("1.0");
}

/**
 * @brief pluginName
 * 插件名称获取
 * @param void
 * @return QString（插件名称）
 */
QString TargetTable::pluginName()
{
    return QString::fromLocal8Bit("TargetTable");
}

/**
 * @brief introduction
 * 概要描述信息
 * @param void
 * @return QString（插件简介）
 */
QString TargetTable::introduction()
{
    return QString::fromLocal8Bit("目标列表插件");
}

/**
 * @brief updateDescribe
 * 更新概要描述
 * @param void
 * @return QString（更新概要描述）
 */
QString TargetTable::updateDescribe()
{
    return QString::fromLocal8Bit("完成目标列表插件的基础功能");
}

IUiPluginBase *installUiPlugin(QWidget *pParent)
{
    gContent = new TargetTable(pParent);
    return gContent;
}
