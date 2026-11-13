#ifndef VIDEOPROCESSFFMPEG_H
#define VIDEOPROCESSFFMPEG_H

#include "videoprocessffmpeg_global.h"
#include "VideoProcessWidget.h"
#include "IUiPluginBase.h"

class VIDEOPROCESSFFMPEGSHARED_EXPORT VideoProcessFFmpeg: public IUiPluginBase
{

public:
    VideoProcessFFmpeg(QWidget *pParent);
    //获取UI widget
    virtual QWidget* getWidget();

    //切换皮肤
    virtual void resetQss(const QString& strStyleName);

    //初始化界面
    virtual void initUi();

    //初始化数据交互
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
    VideoProcessWidget *m_pWidget = nullptr;
};

#endif // VIDEOPROCESSFFMPEG_H
