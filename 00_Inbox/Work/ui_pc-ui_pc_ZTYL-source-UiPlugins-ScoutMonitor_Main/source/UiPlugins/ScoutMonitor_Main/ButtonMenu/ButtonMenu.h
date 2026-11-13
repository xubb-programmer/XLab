/*
* ButtonMenuWidget.h
* Author：王泽文
* Created：2025-7-29
* Last Modified：2025-7-29
* Description：定义软件一级菜单界面软件框架。
*/
#ifndef BUTTONMENU_H
#define BUTTONMENU_H

#include "buttonmenu_global.h"
#include "IUiPluginBase.h"
#include "ButtonMenuWidget.h"
#include <QWidget>
/**
* @brief The ButtonMenu 类
* 用于生成菜单插件界面软件框架。
*/
class BUTTONMENUSHARED_EXPORT ButtonMenu: public IUiPluginBase
{

public:
    ButtonMenu(QWidget *pParent);

    //获取UI widget
    virtual QWidget* getWidget();

    //切换皮肤
    virtual void resetQss(const QString& strStyleName);

    //初始化界面
    virtual void initUi();

    //初始化数据
    virtual void initData();

    //退出
    virtual void close();

    //注册连接
    virtual void registerProtocols();

    //版本号
    virtual QString version();

    //插件名称
    virtual QString pluginName();

    //概要描述信息
    virtual QString introduction();

    //更新概要描述
    virtual QString updateDescribe();
private:

    ButtonMenuWidget *m_pWidget = nullptr;

};

#endif // BUTTONMENU_H
