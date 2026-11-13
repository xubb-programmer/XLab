/*
* File：RenderThread.cpp
* Description：RenderThread 抽象基类的构造实现，以及工厂函数。
*
*  工厂函数读取配置文件：
*    <applicationDir>/config/VideoProcess.ini
*    [Video]
*    renderBackend = sdl    ; sdl（默认） → RenderThreadSdl
*                           ; gst         → RenderThreadGst
*/
#include "utils/RenderThread.h"
#include "utils/RenderThread_gst.h"
#include "utils/RenderThread_sdl.h"

#include <QSettings>
#include <QApplication>
#include <QDebug>

// ── 基类构造 ───────────────────────────────────────────────────────────────
RenderThread::RenderThread(SafeQueue<AVFrame *> *fq, QObject *parent)
    : QThread(parent)
    , frameQueue(fq)
{
}

// ── 工厂函数 ───────────────────────────────────────────────────────────────
RenderThread* RenderThread::create(SafeQueue<AVFrame *> *fq, QObject *parent)
{
    QSettings settings(
        QApplication::applicationDirPath() + "/config/VideoProcess.ini",
        QSettings::IniFormat);

    QString backend = settings.value("Video/renderBackend", "sdl").toString().trimmed().toLower();
    qDebug() << "[RenderThread] backend =" << backend;

    if (backend == "gst") {
        qDebug() << "[RenderThread] create → RenderThreadGst";
        return new RenderThreadGst(fq, parent);
    }

    qDebug() << "[RenderThread] create → RenderThreadSdl";
    return new RenderThreadSdl(fq, parent);
}
