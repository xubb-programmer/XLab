#include "OcrRecognition.h"
#include <QScreen>


OcrRecognition* gContent = nullptr;
OcrRecognition::OcrRecognition(QWidget *pParent)
{
    m_pWidget = new OcrRecognitionWidget(pParent);
}

//获取UI widget
QWidget* OcrRecognition::getWidget()
{
    return m_pWidget;
}

/**
 * @brief initUi
 * 切换皮肤
 * @param void
 * @return 无返回值
 */
void OcrRecognition::resetQss(const QString& strStyleName)
{
    m_pWidget->resetQss(strStyleName);
}

/**
 * @brief initUi
 * 初始化界面
 * @param void
 * @return 无返回值
 */
void OcrRecognition::initUi()
{
    m_pWidget->initUi(this);
    QScreen *screen =QApplication::primaryScreen();
    QSize screenSize =screen->size();
    int Width_ = 745*(((double)((double)screenSize.width()-1024.0)/screenSize.width())+1.0);
    int Height_ = 656*(((double)(screenSize.height()-768)/screenSize.height())+1);
    m_pWidget->resize(Width_,Height_);
    m_pWidget->move((screenSize.width()/2-Width_/2),
                    screenSize.height()/2-Height_/2);
    m_pWidget->hide();

}

/**
 * @brief initData
 * 插件数据初始化交互
 * @param void
 * @return 无返回值
 */
void OcrRecognition::initData()
{
    m_pWidget->initData();
}

/**
 * @brief close
 * 软件退出执行的卸载接口
 * @param void
 * @return 无返回值
 */
void OcrRecognition::close()
{
    delete m_pWidget;
}

/**
 * @brief registerProtocols
 * 注册连接，插件间通信需要进行注册
 * @param void
 * @return 无返回值
 */
void OcrRecognition::registerProtocols()
{
    m_pWidget->registerProtocols();
}

/**
 * @brief version
 * 插件版本号获取
 * @param void
 * @return QString（插件版本信息）
 */
QString OcrRecognition::version()
{
    return QString::fromLocal8Bit("1.0");
}

/**
 * @brief pluginName
 * 插件名称获取
 * @param void
 * @return QString（插件名称）
 */
QString OcrRecognition::pluginName()
{
    return QString::fromLocal8Bit("OcrRecognition");
}

/**
 * @brief introduction
 * 概要描述信息
 * @param void
 * @return QString（插件简介）
 */
QString OcrRecognition::introduction()
{
    return QString::fromLocal8Bit("Ocr识别信息展示插件");
}

/**
 * @brief updateDescribe
 * 更新概要描述
 * @param void
 * @return QString（更新概要描述）
 */
QString OcrRecognition::updateDescribe()
{
    return QString::fromLocal8Bit("完成Ocr识别信息展示与上报的基础功能");
}

IUiPluginBase *installUiPlugin(QWidget *pParent)
{
    gContent = new OcrRecognition(pParent);
    return gContent;
}
