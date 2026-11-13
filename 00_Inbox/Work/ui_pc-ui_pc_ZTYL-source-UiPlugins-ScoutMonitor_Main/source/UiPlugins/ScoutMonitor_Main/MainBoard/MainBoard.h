#ifndef MAINBOARD_H
#define MAINBOARD_H

#include "mainboard_global.h"
#include "IUiPluginBase.h"
#include <QWidget>
#include "MianBoardWidget.h"
class MAINBOARDSHARED_EXPORT MainBoard: public IUiPluginBase
{

public:
    MainBoard(QWidget *pParent);

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

    MianBoardWidget *m_pWidget = nullptr;

};

#endif // MAINBOARD_H
