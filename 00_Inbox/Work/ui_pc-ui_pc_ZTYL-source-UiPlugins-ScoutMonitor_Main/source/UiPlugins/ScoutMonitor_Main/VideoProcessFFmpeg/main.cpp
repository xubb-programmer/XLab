#include "VideoProcessWidget.h"
#include <QApplication>
#include "VideoProcessFFmpeg.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    //VideoProcessWidget w;
    //w.show();
    VideoProcessFFmpeg *plugin = new VideoProcessFFmpeg(nullptr);
    plugin->initUi();
    plugin->initData();
    VideoProcessWidget *m_pWidget =(VideoProcessWidget *)plugin->getWidget();
    m_pWidget->resize(800,600);
    m_pWidget->show();
    return a.exec();
}
