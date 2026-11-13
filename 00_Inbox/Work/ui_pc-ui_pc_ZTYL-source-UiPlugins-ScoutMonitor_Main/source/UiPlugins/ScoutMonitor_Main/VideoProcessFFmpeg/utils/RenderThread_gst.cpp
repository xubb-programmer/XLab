#include "RenderThread_gst.h"

#include <QDebug>
#include <QImage>
#include <QFileInfo>
#include <cstring>
#ifdef Q_OS_UNIX
// ── GStreamer ──────────────────────────────────────────────────────────────
#include <gst/gst.h>
#include <gst/video/videooverlay.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/video.h>
#endif
// ── FFmpeg ─────────────────────────────────────────────────────────────────
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

// ═══════════════════════════════════════════════════════════════════════════
//  Format mapping table
// ═══════════════════════════════════════════════════════════════════════════
struct FmtEntry {
    AVPixelFormat avFmt;
    const char   *gstFmt;
    int           nPlanes;
    int           planeWDiv[4];
    int           planeHDiv[4];
};

// clang-format off
static const FmtEntry kFmtTable[] = {
    { AV_PIX_FMT_YUV420P,   "I420",  3, {1,2,2,0}, {1,2,2,0} },
    { AV_PIX_FMT_YUVJ420P,  "I420",  3, {1,2,2,0}, {1,2,2,0} },
    { AV_PIX_FMT_YUV420P10LE, nullptr, 0, {}, {} },
    { AV_PIX_FMT_YUV422P,   "Y42B",  3, {1,2,2,0}, {1,1,1,0} },
    { AV_PIX_FMT_YUVJ422P,  "Y42B",  3, {1,2,2,0}, {1,1,1,0} },
    { AV_PIX_FMT_YUV444P,   "Y444",  3, {1,1,1,0}, {1,1,1,0} },
    { AV_PIX_FMT_YUVJ444P,  "Y444",  3, {1,1,1,0}, {1,1,1,0} },
    { AV_PIX_FMT_YUV411P,   "Y41B",  3, {1,4,4,0}, {1,1,1,0} },
    { AV_PIX_FMT_YUV410P,   "YUV9",  3, {1,4,4,0}, {1,4,4,0} },
    { AV_PIX_FMT_NV12,      "NV12",  2, {1,1,0,0}, {1,2,0,0} },
    { AV_PIX_FMT_NV21,      "NV21",  2, {1,1,0,0}, {1,2,0,0} },
    { AV_PIX_FMT_NV16,      "NV16",  2, {1,1,0,0}, {1,1,0,0} },
    { AV_PIX_FMT_UYVY422,   "UYVY",  1, {1,0,0,0}, {1,0,0,0} },
    { AV_PIX_FMT_YUYV422,   "YUY2",  1, {1,0,0,0}, {1,0,0,0} },
    { AV_PIX_FMT_YVYU422,   "YVYU",  1, {1,0,0,0}, {1,0,0,0} },
    { AV_PIX_FMT_RGB24,     "RGB",   1, {1,0,0,0}, {1,0,0,0} },
    { AV_PIX_FMT_BGR24,     "BGR",   1, {1,0,0,0}, {1,0,0,0} },
    { AV_PIX_FMT_RGBA,      "RGBA",  1, {1,0,0,0}, {1,0,0,0} },
    { AV_PIX_FMT_BGRA,      "BGRA",  1, {1,0,0,0}, {1,0,0,0} },
    { AV_PIX_FMT_ARGB,      "ARGB",  1, {1,0,0,0}, {1,0,0,0} },
    { AV_PIX_FMT_0RGB,      "xRGB",  1, {1,0,0,0}, {1,0,0,0} },
    { AV_PIX_FMT_0BGR,      "xBGR",  1, {1,0,0,0}, {1,0,0,0} },
    { AV_PIX_FMT_RGB0,      "RGBx",  1, {1,0,0,0}, {1,0,0,0} },
    { AV_PIX_FMT_BGR0,      "BGRx",  1, {1,0,0,0}, {1,0,0,0} },
    { AV_PIX_FMT_NONE, nullptr, 0, {}, {} }
};
// clang-format on

static int packedBpp(AVPixelFormat fmt)
{
    switch (fmt) {
    case AV_PIX_FMT_UYVY422:
    case AV_PIX_FMT_YUYV422:
    case AV_PIX_FMT_YVYU422:  return 2;
    case AV_PIX_FMT_RGB24:
    case AV_PIX_FMT_BGR24:    return 3;
    case AV_PIX_FMT_RGBA:
    case AV_PIX_FMT_BGRA:
    case AV_PIX_FMT_ARGB:
    case AV_PIX_FMT_0RGB:
    case AV_PIX_FMT_0BGR:
    case AV_PIX_FMT_RGB0:
    case AV_PIX_FMT_BGR0:     return 4;
    default:                   return 1;
    }
}

static const FmtEntry *findEntry(AVPixelFormat fmt)
{
    for (const FmtEntry *e = kFmtTable; e->avFmt != AV_PIX_FMT_NONE; ++e) {
        if (e->avFmt == fmt && e->gstFmt != nullptr)
            return e;
    }
    return nullptr;
}
#ifdef Q_OS_UNIX
// ── GLib main-loop thread ──────────────────────────────────────────────────
static gpointer glibLoopThread(gpointer data)
{
    g_main_loop_run(static_cast<GMainLoop *>(data));
    return nullptr;
}
#endif
// ═══════════════════════════════════════════════════════════════════════════
//  Construction / destruction
// ═══════════════════════════════════════════════════════════════════════════
RenderThreadGst::RenderThreadGst(SafeQueue<AVFrame *> *fQueue, QObject *parent)
    : RenderThread(fQueue, parent)
{
#ifdef Q_OS_UNIX
    if (!gst_is_initialized())
        gst_init(nullptr, nullptr);
#endif
}

RenderThreadGst::~RenderThreadGst()
{
    destroyPipeline();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Public API
// ═══════════════════════════════════════════════════════════════════════════
void RenderThreadGst::init(unsigned long winId, int width, int height)
{
    destroyPipeline();
    m_videoWidth  = width;
    m_videoHeight = height;
    display_ctx.src_width  = width;
    display_ctx.src_height = height;
    buildPipeline(winId, width, height);
}

void RenderThreadGst::run()
{
    m_stopFlag  = false;
    m_activeFmt = AV_PIX_FMT_NONE;
#ifdef Q_OS_UNIX
    if (!m_pipeline || !m_appsrc) {
        qWarning() << "[RenderThreadGst] Pipeline not initialised – call init() first";
        return;

    }

    GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        qWarning() << "[RenderThreadGst] Failed to set pipeline to PLAYING";
        return;
    }

    while (!m_stopFlag) {
        AVFrame *pFrame = nullptr;
        if (!frameQueue->tryPop(pFrame, 20))
            continue;
        if (!pFrame) continue;

        if (!m_appsrc) {
            av_frame_free(&pFrame);
            continue;
        }

        pushFrameToAppsrc(pFrame);

        if (m_snapShotFlag)
            saveFrame(pFrame);

        av_frame_free(&pFrame);
    }

    gst_app_src_end_of_stream(GST_APP_SRC(m_appsrc));
    gst_element_set_state(m_pipeline, GST_STATE_NULL);
#endif
    return;
}

void RenderThreadGst::stop()
{
    m_stopFlag = true;
    if (frameQueue) frameQueue->wakeAll();
    if (!wait(3000)) {
        qWarning() << "[RenderThreadGst] wait timeout – force terminate";
        terminate();
        wait();
    }
    destroyPipeline();
}

void RenderThreadGst::snapShot(const QString &path)
{
    m_snapFilePathName = path;
    m_snapShotFlag     = true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Pipeline build / destroy
// ═══════════════════════════════════════════════════════════════════════════
void RenderThreadGst::buildPipeline(unsigned long winId, int width, int height)
{
#ifdef Q_OS_UNIX
    Q_UNUSED(width); Q_UNUSED(height);

    m_appsrc           = gst_element_factory_make("appsrc",       "src");
    GstElement *queue  = gst_element_factory_make("queue",        "buf");
    GstElement *conv   = gst_element_factory_make("videoconvert", "conv");
    m_xvsink           = gst_element_factory_make("xvimagesink",  "sink");

    if (!m_appsrc || !queue || !conv || !m_xvsink) {
        qCritical() << "[RenderThreadGst] Could not create pipeline elements."
                    << "Ensure gstreamer1.0-plugins-good and gstreamer1.0-x are installed.";
        if (m_appsrc) { gst_object_unref(m_appsrc); m_appsrc = nullptr; }
        if (queue)      gst_object_unref(queue);
        if (conv)       gst_object_unref(conv);
        if (m_xvsink) { gst_object_unref(m_xvsink); m_xvsink = nullptr; }
        return;
    }

    // block=FALSE: push_buffer 立即返回，不阻塞渲染线程
    // do-timestamp=TRUE: 由 appsrc 按管道时钟自动打时间戳，避免手动 PTS 偏差
    g_object_set(m_appsrc,
                 "format",       GST_FORMAT_TIME,
                 "is-live",      TRUE,
                 "do-timestamp", TRUE,
                 "stream-type",  GST_APP_STREAM_TYPE_STREAM,
                 "block",        FALSE,
                 nullptr);

    // leaky=2(downstream): 队列满时丢最旧帧，始终显示最新画面
    g_object_set(queue,
                 "max-size-buffers", (guint)3,
                 "max-size-bytes",   (guint)0,
                 "max-size-time",    (guint64)0,
                 "leaky",            (guint)2,
                 nullptr);

    // "sync"=FALSE: 不对齐时钟（正确属性名，"synchronous" 在 GStreamer 1.x 无效）
    // "qos"=FALSE:  关闭 QoS 上报，避免触发上游丢帧
    g_object_set(m_xvsink,
                 "force-aspect-ratio", TRUE,
                 "sync",               FALSE,
                 "qos",                FALSE,
                 nullptr);

    m_pipeline = gst_pipeline_new("render-pipeline");
    gst_bin_add_many(GST_BIN(m_pipeline), m_appsrc, queue, conv, m_xvsink, nullptr);

    if (!gst_element_link_many(m_appsrc, queue, conv, m_xvsink, nullptr)) {
        qCritical() << "[RenderThreadGst] Failed to link pipeline elements";
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
        m_appsrc   = nullptr;
        m_xvsink   = nullptr;
        return;
    }

    gst_video_overlay_set_window_handle(
                GST_VIDEO_OVERLAY(m_xvsink),
                static_cast<guintptr>(winId));

    m_gloop      = g_main_loop_new(nullptr, FALSE);
    m_glibThread = g_thread_new("gst-bus", glibLoopThread, m_gloop);

    GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(m_pipeline));
    gst_bus_add_watch(bus, [](GstBus *, GstMessage *msg, gpointer d) -> gboolean {
        auto *self = static_cast<RenderThreadGst *>(d);
        GError *err = nullptr;
        switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR:
            gst_message_parse_error(msg, &err, nullptr);
            qCritical() << "[GStreamer] Error:" << (err ? err->message : "unknown");
            if (err) g_error_free(err);
            self->m_stopFlag = true;
            break;
        case GST_MESSAGE_WARNING:
            gst_message_parse_warning(msg, &err, nullptr);
            qWarning() << "[GStreamer] Warning:" << (err ? err->message : "unknown");
            if (err) g_error_free(err);
            break;
        default: break;
        }
        return TRUE;
    }, this);
    gst_object_unref(bus);

    qDebug() << "[RenderThreadGst] Pipeline built. winId=" << winId;
#endif
    return;
}

void RenderThreadGst::destroyPipeline()
{
#ifdef Q_OS_UNIX
    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
        m_appsrc   = nullptr;
        m_xvsink   = nullptr;
    }
    if (m_gloop) {
        g_main_loop_quit(m_gloop);
        if (m_glibThread) {
            g_thread_join(m_glibThread);
            m_glibThread = nullptr;
        }
        g_main_loop_unref(m_gloop);
        m_gloop = nullptr;
    }
    m_activeFmt = AV_PIX_FMT_NONE;
#endif
}

// ═══════════════════════════════════════════════════════════════════════════
//  Per-format caps update
// ═══════════════════════════════════════════════════════════════════════════
void RenderThreadGst::updateAppsrcCaps(AVPixelFormat fmt, int w, int h,
                                       const char *gstFmt)
{
#ifdef Q_OS_UNIX
    gchar *capStr = g_strdup_printf(
                "video/x-raw,format=%s,width=%d,height=%d,framerate=0/1",
                gstFmt, w, h);
    GstCaps *caps = gst_caps_from_string(capStr);
    g_free(capStr);

    gst_element_set_state(m_pipeline, GST_STATE_READY);
    g_object_set(m_appsrc, "caps", caps, nullptr);
    gst_caps_unref(caps);
    gst_element_set_state(m_pipeline, GST_STATE_PLAYING);

    m_activeFmt = fmt;

    qDebug() << "[RenderThreadGst] appsrc caps updated →" << gstFmt << w << "x" << h;
#endif
    return;
}

// ═══════════════════════════════════════════════════════════════════════════
//  pushFrameToAppsrc
// ═══════════════════════════════════════════════════════════════════════════
bool RenderThreadGst::pushFrameToAppsrc(AVFrame *pFrame)
{
#ifdef Q_OS_UNIX
    const int          w      = m_videoWidth;
    const int          h      = m_videoHeight;
    const AVPixelFormat srcFmt = static_cast<AVPixelFormat>(pFrame->format);

    const FmtEntry *entry   = findEntry(srcFmt);
    const char     *gstFmt  = entry ? entry->gstFmt : "I420";
    const bool      needSws = (entry == nullptr);

    if (srcFmt != m_activeFmt)
        updateAppsrcCaps(srcFmt, w, h, gstFmt);

    gsize bufSize;
    if (needSws) {
        bufSize = static_cast<gsize>(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, w, h, 1));
    } else if (entry->nPlanes == 1) {
        bufSize = static_cast<gsize>(h * w * packedBpp(srcFmt));
    } else {
        bufSize = 0;
        for (int p = 0; p < entry->nPlanes; ++p) {
            int pw = w / entry->planeWDiv[p];
            int ph = h / entry->planeHDiv[p];
            bufSize += static_cast<gsize>(pw * ph);
        }
    }

    GstBuffer *buf = gst_buffer_new_allocate(nullptr, bufSize, nullptr);
    if (!buf) {
        qWarning() << "[RenderThreadGst] gst_buffer_new_allocate failed (size=" << bufSize << ")";
        return false;
    }

    GstMapInfo map;
    if (!gst_buffer_map(buf, &map, GST_MAP_WRITE)) {
        gst_buffer_unref(buf);
        return false;
    }

    uint8_t *dst = map.data;

    if (needSws) {
        AVFrame *tmp = av_frame_alloc();
        av_image_fill_arrays(tmp->data, tmp->linesize,
                             dst, AV_PIX_FMT_YUV420P, w, h, 1);
        SwsContext *sws = sws_getContext(
                    w, h, srcFmt, w, h, AV_PIX_FMT_YUV420P,
                    SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
        if (sws) {
            sws_scale(sws,
                      (const uint8_t * const *)pFrame->data, pFrame->linesize,
                      0, h, tmp->data, tmp->linesize);
            sws_freeContext(sws);
        }
        av_frame_free(&tmp);

    } else if (entry->nPlanes == 1) {
        const int rowBytes = w * packedBpp(srcFmt);
        for (int row = 0; row < h; ++row) {
            memcpy(dst, pFrame->data[0] + row * pFrame->linesize[0], rowBytes);
            dst += rowBytes;
        }
    } else {
        for (int p = 0; p < entry->nPlanes; ++p) {
            int pw        = w / entry->planeWDiv[p];
            int ph        = h / entry->planeHDiv[p];
            int srcStride = pFrame->linesize[p];
            for (int row = 0; row < ph; ++row) {
                memcpy(dst, pFrame->data[p] + row * srcStride, pw);
                dst += pw;
            }
        }
    }

    gst_buffer_unmap(buf, &map);

    // do-timestamp=TRUE 时 appsrc 自动打时间戳；不设置 PTS 避免手动值干扰
    GST_BUFFER_PTS(buf)      = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION(buf) = GST_CLOCK_TIME_NONE;

    GstFlowReturn flow = gst_app_src_push_buffer(GST_APP_SRC(m_appsrc), buf);
    // block=FALSE 时偶发 FLUSHING 属正常，不停止管道
    if (flow != GST_FLOW_OK && flow != GST_FLOW_FLUSHING) {
        qWarning() << "[RenderThreadGst] push_buffer returned" << flow;
    }
#endif
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Snapshot
// ═══════════════════════════════════════════════════════════════════════════
void RenderThreadGst::saveFrame(AVFrame *pFrame)
{
#ifdef Q_OS_UNIX
    if (!pFrame || !pFrame->data[0]) {
        qWarning() << "[RenderThreadGst] Invalid frame for snapshot";
        m_snapShotFlag = false;
        emit errorOccured(Error_Snapshot_Invaild_Frame);
        return;
    }
    m_snapShotFlag = false;

    const int w = m_videoWidth;
    const int h = m_videoHeight;

    AVFrame *rgbFrame = av_frame_alloc();
    if (!rgbFrame) { emit errorOccured(Error_Snapshot_Fail); return; }

    const int  numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB32, w, h, 1);
    uint8_t   *rgbBuf   = static_cast<uint8_t *>(av_malloc(numBytes));
    if (!rgbBuf) {
        av_frame_free(&rgbFrame);
        emit errorOccured(Error_Snapshot_Fail);
        return;
    }

    av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize,
                         rgbBuf, AV_PIX_FMT_RGB32, w, h, 1);

    SwsContext *sws = sws_getContext(
                w, h, static_cast<AVPixelFormat>(pFrame->format),
                w, h, AV_PIX_FMT_RGB32,
                SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);

    if (sws) {
        sws_scale(sws,
                  (const uint8_t * const *)pFrame->data, pFrame->linesize,
                  0, h, rgbFrame->data, rgbFrame->linesize);
        sws_freeContext(sws);
    }

    QImage img(rgbBuf, w, h, QImage::Format_ARGB32);
    QFileInfo fi(m_snapFilePathName);
    if (!img.save(m_snapFilePathName, fi.suffix().toUpper().toLatin1().data())) {
        qWarning() << "[RenderThreadGst] Failed to save snapshot:" << m_snapFilePathName;
        emit errorOccured(Error_Snapshot_Fail);
    }

    av_free(rgbBuf);
    av_frame_free(&rgbFrame);
    emit signalShowSnapShot(m_snapFilePathName);
    m_snapFilePathName.clear();
#endif
    return;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Geometry
// ═══════════════════════════════════════════════════════════════════════════
void RenderThreadGst::calculateShowRect(DispCtx *ctx)
{
    if (ctx->win_height == 0 || ctx->src_height == 0) return;

    float srcRatio = static_cast<float>(ctx->src_width)  / ctx->src_height;
    float winRatio = static_cast<float>(ctx->win_width)  / ctx->win_height;

    if (srcRatio > winRatio) {
        ctx->show_w = ctx->win_width;
        ctx->show_h = static_cast<int>(ctx->win_width / srcRatio);
        ctx->show_x = 0;
        ctx->show_y = (ctx->win_height - ctx->show_h) / 2;
    } else {
        ctx->show_w = static_cast<int>(ctx->win_height * srcRatio);
        ctx->show_h = ctx->win_height;
        ctx->show_x = (ctx->win_width - ctx->show_w) / 2;
        ctx->show_y = 0;
    }
}
