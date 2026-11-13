/*
* File：RenderThread.h
* Author：huangjin
* Description：渲染线程抽象基类
*              通过 RenderThread::create() 工厂函数根据配置文件
*              决定使用 GStreamer（gst）或 SDL 后端。
*
*  配置项（<appDir>/config/VideoProcess.ini）：
*    [Video]
*    renderBackend = gst    ; 可填 gst / sdl，默认 sdl
*/
#ifndef RENDERTHREAD_H
#define RENDERTHREAD_H

#include <QThread>
#include <QString>
#include "utils/FFmpegWorkerCommon.h"

extern "C" {
#include <libavutil/frame.h>
}

class RenderThread : public QThread
{
    Q_OBJECT
public:
    explicit RenderThread(SafeQueue<AVFrame*> *frameQueue, QObject *parent = nullptr);
    virtual ~RenderThread() = default;

    /**
     * @brief init  初始化渲染后端并绑定到宿主窗口
     * @param winId  平台窗口句柄（X11 WId / HWND，统一以 unsigned long 传递）
     * @param width  视频源宽度（像素）
     * @param height 视频源高度（像素）
     */
    virtual void init(unsigned long winId, int width, int height) = 0;

    /** 渲染循环，由 QThread::start() 在工作线程执行 */
    void run() override = 0;

    /** 请求停止并等待线程安全退出 */
    virtual void stop() = 0;

    /**
     * @brief snapShot  排队一次异步截图请求
     * @param path  输出文件绝对路径（PNG / JPG …）
     */
    virtual void snapShot(const QString &path) = 0;

    /**
     * @brief saveFrame  将当前帧转 RGB32 并保存为图像文件
     *        由渲染循环内部调用，请勿从外部调用。
     */
    virtual void saveFrame(AVFrame *pFrame) = 0;

    /**
     * @brief create  工厂函数 — 读取配置文件决定后端类型
     *
     * 读取 <applicationDir>/config/VideoProcess.ini：
     *   [Video]
     *   renderBackend = gst   ; gst → GStreamer XV 后端
     *                         ; sdl → SDL2 软件渲染后端（默认）
     *
     * @return 堆分配的 RenderThread 子类实例，调用方负责 delete
     */
    static RenderThread* create(SafeQueue<AVFrame*> *frameQueue, QObject *parent = nullptr);

signals:
    void errorOccured(ErrorType);
    void signalShowSnapShot(QString imgPath);

protected:
    SafeQueue<AVFrame*> *frameQueue  = nullptr;
    int     m_videoWidth             = 1024;
    int     m_videoHeight            = 768;
    bool    m_stopFlag               = false;
    bool    m_snapShotFlag           = false;
    QString m_snapFilePathName;
};

#endif // RENDERTHREAD_H
