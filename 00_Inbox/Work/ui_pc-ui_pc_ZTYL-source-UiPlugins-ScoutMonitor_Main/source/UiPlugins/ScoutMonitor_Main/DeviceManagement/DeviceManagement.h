#ifndef DEVICEMANAGEMENT_H
#define DEVICEMANAGEMENT_H

#include "IUiPluginBase.h"
#include "DeviceManagementWidget.h"
#include <QtCore/qglobal.h>

#if defined(DEVICEMANAGEMENT_LIBRARY)
#  define DEVICEMANAGEMENTSHARED_EXPORT Q_DECL_EXPORT
#else
#  define DEVICEMANAGEMENTSHARED_EXPORT Q_DECL_IMPORT
#endif

class DEVICEMANAGEMENTSHARED_EXPORT DeviceManagement : public IUiPluginBase
{

public:
    DeviceManagement(QWidget *pParent);

    /**
     * @brief getWidget
     * 获取UI widget
     * @param void
     * @return QWidget*(界面指针)
     */
    virtual QWidget* getWidget();

    /**
     * @brief resetQss
     * 切换皮肤
     * @param strStyleName("day":白天 "night"黑夜)
     * @return void
     */
    virtual void resetQss(const QString& strStyleName);

    /**
     * @brief initUi
     * 初始化界面
     * @param void
     * @return void
     */
    virtual void initUi();

    /**
     * @brief initData
     * 初始化数据交互
     * @param void
     * @return void
     */
    virtual void initData();

    /**
     * @brief close
     * 退出
     * @param void
     * @return void
     */
    virtual void close();

    /**
     * @brief registerProtocols
     * 插件间信号槽通信需要注册
     * @param void
     * @return void
     */
    virtual void registerProtocols();

    /**
     * @brief version
     * 版本号
     * @param void
     * @return QString(版本号)
     */
    virtual QString version();

    /**
     * @brief pluginName
     * 插件名称
     * @param void
     * @return QString(插件名)
     */
    virtual QString pluginName();

    /**
     * @brief introduction
     * 概要描述信息
     * @param void
     * @return QString(概要描述信息)
     */
    virtual QString introduction();

    /**
     * @brief updateDescribe
     * 更新概要描述
     * @param void
     * @return QString(更新概要描述)
     */
    virtual QString updateDescribe();

private:
    DeviceManagementWidget *m_pWidget = nullptr;

};

#endif // DEVICEMANAGEMENT_H
