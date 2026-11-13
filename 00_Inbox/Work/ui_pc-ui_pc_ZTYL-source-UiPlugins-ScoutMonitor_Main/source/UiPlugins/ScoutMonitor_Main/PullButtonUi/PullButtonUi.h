#ifndef PULLBUTTONUI_H
#define PULLBUTTONUI_H

#include "IUiPluginBase.h"
#include <QtCore/qglobal.h>
#include "PullButtonUiWidget.h"

#if defined(PULLBUTTONUI_LIBRARY)
#  define PULLBUTTONUISHARED_EXPORT Q_DECL_EXPORT
#else
#  define PULLBUTTONUISHARED_EXPORT Q_DECL_IMPORT
#endif

class PULLBUTTONUISHARED_EXPORT PullButtonUi : public IUiPluginBase
{

public:
    PullButtonUi(QWidget *pParent);

    //获取UI widget
    virtual QWidget* getWidget();

    /**
     * @brief resetQss
     * 切换皮肤
     * @param void
     * @return 无返回值
     */
    virtual void resetQss(const QString& strStyleName);

    /**
     * @brief initUi
     * 初始化界面
     * @param void
     * @return 无返回值
     */
    virtual void initUi();

    /**
     * @brief initData
     * 插件数据初始化交互
     * @param void
     * @return 无返回值
     */
    virtual void initData();

    /**
     * @brief close
     * 软件退出执行的卸载接口
     * @param void
     * @return 无返回值
     */
    virtual void close();

    /**
     * @brief registerProtocols
     * 注册连接，插件间通信需要进行注册
     * @param void
     * @return 无返回值
     */
    virtual void registerProtocols();

    /**
     * @brief version
     * 插件版本号获取
     * @param void
     * @return QString（插件版本信息）
     */
    virtual QString version();

    /**
     * @brief pluginName
     * 插件名称获取
     * @param void
     * @return QString（插件名称）
     */
    virtual QString pluginName();

    /**
     * @brief introduction
     * 概要描述信息
     * @param void
     * @return QString（插件简介）
     */
    virtual QString introduction();

    /**
     * @brief updateDescribe
     * 更新概要描述
     * @param void
     * @return QString（更新概要描述）
     */
    virtual QString updateDescribe();

private:
    PullButtonUiWidget *m_pWidget = nullptr;
};

#endif // PULLBUTTONUI_H
