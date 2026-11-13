/*
* File：RenderThread_gst.h
* Author：huangjin
* Created：2025-08-08
* Last Modified：2025-08-12
* Description：渲染线程 —— GStreamer XV 后端，多格式兼容版
*              继承自 RenderThread 抽象基类
*/
#ifndef RENDERTHREAD_GST_H
#define RENDERTHREAD_GST_H

#include "utils/RenderThread.h"
#ifdef Q_OS_UNIX
#include <gst/gst.h>
#include <gst/video/videooverlay.h>
#include <gst/app/gstappsrc.h>
#endif
extern "C" {
#include <libavutil/pixfmt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

class RenderThreadGst : public RenderThread
{
    Q_OBJECT
public:
    explicit RenderThreadGst(SafeQueue<AVFrame*> *frameQueue, QObject *parent = nullptr);
    ~RenderThreadGst();

    /** 初始化 GStreamer 管道并绑定 X11 窗口 */
    void init(unsigned long winId, int width, int height) override;

    void run()      override;
    void stop()     override;
    void snapShot(const QString &path) override;
    void saveFrame(AVFrame *pFrame)    override;

private:
    // ── 管道生命周期 ───────────────────────────────────────────────
    void buildPipeline(unsigned long winId, int width, int height);
    void destroyPipeline();

    /** 像素格式或分辨率变化时重协商 appsrc caps */
    void updateAppsrcCaps(AVPixelFormat fmt, int w, int h, const char *gstFmt);

    /**
     * @brief pushFrameToAppsrc  逐帧推入 GStreamer appsrc
     *
     * 原生支持（零拷贝平面逐行复制）：
     *   YUV420P/YUVJ420P、YUV422P/YUVJ422P、YUV444P/YUVJ444P
     *   YUV411P、YUV410P、NV12、NV21、NV16
     *   UYVY422、YUYV422、YVYU422
     *   RGB24、BGR24、RGBA、BGRA、ARGB、0RGB/BGR/RGB0/BGR0
     * 其余格式 → swscale → I420 降级处理
     */
    bool pushFrameToAppsrc(AVFrame *pFrame);

    // ── 显示矩形（保持宽高比）──────────────────────────────────────
    struct DispCtx {
        int src_width  = 0;
        int src_height = 0;
        int win_width  = 0;
        int win_height = 0;
        int show_x = 0, show_y = 0, show_w = 0, show_h = 0;
    };
    void calculateShowRect(DispCtx *ctx);
#ifdef Q_OS_UNIX
    // ── GStreamer 对象 ─────────────────────────────────────────────
    GstElement   *m_pipeline   = nullptr;
    GstElement   *m_appsrc     = nullptr;
    GstElement   *m_xvsink     = nullptr;
    GMainLoop    *m_gloop      = nullptr;
    GThread      *m_glibThread = nullptr;
#endif
    // ── 状态 ──────────────────────────────────────────────────────
    AVPixelFormat  m_activeFmt  = AV_PIX_FMT_NONE;
    DispCtx        display_ctx;
};

#endif // RENDERTHREAD_GST_H
