#include "VideoProcessFFmpeg.h"
#include <QScreen>
VideoProcessFFmpeg* gContent = nullptr;

VideoProcessFFmpeg::VideoProcessFFmpeg(QWidget *pParent)
{
    m_pWidget = new VideoProcessWidget(pParent);
}

QWidget *VideoProcessFFmpeg::getWidget()
{
    return m_pWidget;
}

void VideoProcessFFmpeg::resetQss(const QString &strStyleName)
{
    m_pWidget->resetQss(strStyleName);
}

void VideoProcessFFmpeg::initUi()
{
    m_pWidget->initUi(this);
//    m_pWidget->resize(getRect().size());
//    m_pWidget->move(getRect().x(),getRect().y());
    QScreen *screen = QApplication::primaryScreen();
    QSize screenSize = screen->size();
    m_pWidget->resize(screenSize.width(),screenSize.height()-44);
    m_pWidget->move(0,44);
    m_pWidget->show();
}

void VideoProcessFFmpeg::initData()
{
    m_pWidget->initData();
}

void VideoProcessFFmpeg::close()
{
    //m_pWidget->sltStopPlayClicked();
    //m_pWidget->stopScreenStreamPush();
    delete m_pWidget;
}

void VideoProcessFFmpeg::registerProtocols()
{
    m_pWidget->registerProtocols();
}

QString VideoProcessFFmpeg::version()
{
    return QString::fromLocal8Bit("1.0");
}

QString VideoProcessFFmpeg::pluginName()
{
    return QString::fromLocal8Bit("VideoProcessFFmpeg");
}

QString VideoProcessFFmpeg::introduction()
{
    return QString::fromLocal8Bit("视频显示插件");
}

QString VideoProcessFFmpeg::updateDescribe()
{
    return QString::fromLocal8Bit("完成视频处理的基础功能");

}
IUiPluginBase *installUiPlugin(QWidget *pParent)
{
    gContent = new VideoProcessFFmpeg(pParent);
    return gContent;
}
