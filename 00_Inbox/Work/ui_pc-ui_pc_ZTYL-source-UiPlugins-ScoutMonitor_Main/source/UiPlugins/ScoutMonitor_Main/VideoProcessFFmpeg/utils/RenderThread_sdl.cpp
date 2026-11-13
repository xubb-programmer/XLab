#include "RenderThread_sdl.h"

#include <QDebug>
#include <QImage>
#include <QFileInfo>
#include <SDL2/SDL_render.h>

// 显示跟不上解码时最多积压的帧数；超过就丢掉旧帧只画最新的，
// 否则解码队列会攒到上限(100 帧)，显示延迟跟着涨到好几秒
static const int kMaxRenderBacklog = 2;

#ifdef Q_OS_WIN
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <QtWidgets/QApplication>

SDL_Event g_SDL_event_sdl;

LRESULT CALLBACK WndProcSdl(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_POWERBROADCAST:
        if (wParam == PBT_POWERSETTINGCHANGE) {
            POWERBROADCAST_SETTING* pbs = reinterpret_cast<POWERBROADCAST_SETTING*>(lParam);
            if (IsEqualGUID(pbs->PowerSetting, GUID_CONSOLE_DISPLAY_STATE)) {
                qDebug() << "DisPlay state changed to:" << (int)pbs->Data[0];
            }
        }
        break;
    case WM_DESTROY:
        break;
    default:
        break;
    }
    return 0;
}
#endif

// ═══════════════════════════════════════════════════════════════════════════
//  Construction / destruction
// ═══════════════════════════════════════════════════════════════════════════
RenderThreadSdl::RenderThreadSdl(SafeQueue<AVFrame *> *fQueue, QObject *parent)
    : RenderThread(fQueue, parent)
{
    SDL_Init(SDL_INIT_VIDEO);
#ifdef Q_OS_WIN32
    WNDCLASSEX wcex = {0};
    wcex.cbSize        = sizeof(WNDCLASSEX);
    wcex.lpfnWndProc   = WndProcSdl;
    wcex.hInstance     = GetModuleHandle(nullptr);
    wcex.lpszClassName = L"SDL_LockMonitorWindow_Sdl";
    RegisterClassEx(&wcex);
#endif
}

RenderThreadSdl::~RenderThreadSdl()
{
    m_stopFlag = false;
    destroySDLResources();
    SDL_Quit();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Public API
// ═══════════════════════════════════════════════════════════════════════════
void RenderThreadSdl::init(unsigned long winId, int width, int height)
{
    destroySDLResources();
    m_pSdlWindow = SDL_CreateWindowFrom(reinterpret_cast<void *>(winId));
    display_ctx.src_width  = width;
    display_ctx.src_height = height;
#ifdef Q_OS_WIN32
    getWindowSize(reinterpret_cast<HWND>(winId),
                  &display_ctx.win_width, &display_ctx.win_height);
#endif
    calculateShowRect(&display_ctx);

    m_pSdlRenderer = SDL_CreateRenderer(m_pSdlWindow, -1, SDL_RENDERER_SOFTWARE);
#ifdef Q_OS_WIN32
    m_pSdlTexture = SDL_CreateTexture(m_pSdlRenderer, SDL_PIXELFORMAT_IYUV,
                                      SDL_TEXTUREACCESS_STREAMING, width, height);
#endif
    m_videoWidth  = width;
    m_videoHeight = height;
    SDL_ShowWindow(m_pSdlWindow);

    int num_drivers = SDL_GetNumVideoDrivers();
    for (int i = 0; i < num_drivers; i++)
        qDebug() << "=================SDL_GetVideoDriver:" << SDL_GetVideoDriver(i);
    qDebug() << "current:" << SDL_GetCurrentVideoDriver();
}

void RenderThreadSdl::run()
{
    SDL_Event event;
    m_stopFlag = false;

    while (!m_stopFlag) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                m_stopFlag = true;
            } else if (event.type == SDL_WINDOWEVENT
                       && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                display_ctx.win_width  = event.window.data1;
                display_ctx.win_height = event.window.data2;
                calculateShowRect(&display_ctx);
                rebuildRender();
            } else if (event.window.event == SDL_WINDOWEVENT_RESTORED
                       || event.window.event == SDL_WINDOWEVENT_EXPOSED) {
                rebuildRender();
            }
        }

        AVFrame *pFrame = nullptr;
        if (!frameQueue->tryPop(pFrame, 20))
            continue;
        if (!pFrame) continue;
        AVFrame *newer = nullptr;
        while (frameQueue->size() > kMaxRenderBacklog && frameQueue->tryPop(newer, 0)) {
            av_frame_free(&pFrame);
            pFrame = newer;
        }
        if (!pFrame) continue;
#ifdef Q_OS_UNIX
        if (!m_pSdlTexture || m_lastPixFmt != pFrame->format) {
            if (pFrame->format == AV_PIX_FMT_NV12) {
                m_pSdlTexture = SDL_CreateTexture(m_pSdlRenderer, SDL_PIXELFORMAT_NV12,
                                                  SDL_TEXTUREACCESS_STREAMING,
                                                  m_videoWidth, m_videoHeight);
            } else {
                m_pSdlTexture = SDL_CreateTexture(m_pSdlRenderer, SDL_PIXELFORMAT_IYUV,
                                                  SDL_TEXTUREACCESS_STREAMING,
                                                  m_videoWidth, m_videoHeight);
            }
            m_lastPixFmt = static_cast<AVPixelFormat>(pFrame->format);
        }
#endif
        if (!m_pSdlTexture || !m_pSdlRenderer || !m_pSdlWindow) {
            av_frame_free(&pFrame);
            continue;
        }
#ifdef Q_OS_UNIX
        if (pFrame->format == AV_PIX_FMT_NV12 || pFrame->format == AV_PIX_FMT_NV21) {
            SDL_UpdateNVTexture(m_pSdlTexture, nullptr,
                                pFrame->data[0], pFrame->linesize[0],
                                pFrame->data[1], pFrame->linesize[1]);
        } else {
            SDL_UpdateYUVTexture(m_pSdlTexture, nullptr,
                                 pFrame->data[0], pFrame->linesize[0],
                                 pFrame->data[1], pFrame->linesize[1],
                                 pFrame->data[2], pFrame->linesize[2]);
        }
#else
        SDL_UpdateYUVTexture(m_pSdlTexture, nullptr,
                             pFrame->data[0], pFrame->linesize[0],
                             pFrame->data[1], pFrame->linesize[1],
                             pFrame->data[2], pFrame->linesize[2]);
#endif
        SDL_SetRenderDrawColor(m_pSdlRenderer, 0, 0, 0, 255);
        SDL_RenderClear(m_pSdlRenderer);
#ifdef Q_OS_WIN32
        SDL_RenderCopy(m_pSdlRenderer, m_pSdlTexture, nullptr, &display_ctx.show_rect);
#else
        SDL_RenderCopy(m_pSdlRenderer, m_pSdlTexture, nullptr, nullptr);
#endif
        SDL_RenderPresent(m_pSdlRenderer);

        if (m_snapShotFlag)
            saveFrame(pFrame);

        av_frame_free(&pFrame);
    }
}

void RenderThreadSdl::stop()
{
    m_stopFlag = true;
    if (frameQueue) frameQueue->wakeAll();
    if (!wait(3000)) {
        qDebug() << "[RenderThreadSdl] wait timeout, force terminate";
        terminate();
        wait();
    }
    destroySDLResources();
}

void RenderThreadSdl::snapShot(const QString &path)
{
    m_snapFilePathName = path;
    m_snapShotFlag     = true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Snapshot
// ═══════════════════════════════════════════════════════════════════════════
void RenderThreadSdl::saveFrame(AVFrame *pFrame)
{
    if (!pFrame || pFrame->data[0] == nullptr) {
        qDebug() << "Invalid frame for snapshot";
        m_snapShotFlag = false;
        emit errorOccured(Error_Snapshot_Invaild_Frame);
        return;
    }
    m_snapShotFlag = false;

    AVFrame *pFrameRGB = av_frame_alloc();
    if (!pFrameRGB) {
        emit errorOccured(Error_Snapshot_Fail);
        return;
    }
    int      iNumBytes  = av_image_get_buffer_size(AV_PIX_FMT_RGB32, m_videoWidth, m_videoHeight, 1);
    uint8_t *pRgbBuffer = static_cast<uint8_t *>(av_malloc(iNumBytes * sizeof(uint8_t)));
    if (!pRgbBuffer) {
        av_frame_free(&pFrameRGB);
        emit errorOccured(Error_Snapshot_Fail);
        return;
    }
    av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize,
                         pRgbBuffer, AV_PIX_FMT_RGB32,
                         m_videoWidth, m_videoHeight, 1);
    SwsContext *conversion = sws_getContext(m_videoWidth, m_videoHeight,
                                            static_cast<AVPixelFormat>(pFrame->format),
                                            m_videoWidth, m_videoHeight, AV_PIX_FMT_RGB32,
                                            SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    if (conversion) {
        sws_scale(conversion,
                  (const uint8_t * const *)pFrame->data, pFrame->linesize,
                  0, pFrame->height,
                  pFrameRGB->data, pFrameRGB->linesize);
        sws_freeContext(conversion);
    }
    QImage img(reinterpret_cast<uchar *>(pRgbBuffer),
               m_videoWidth, m_videoHeight, QImage::Format_ARGB32);
    QFileInfo file(m_snapFilePathName);
    if (!img.save(m_snapFilePathName, file.suffix().toUpper().toLatin1().data()))
        emit errorOccured(Error_Snapshot_Fail);

    av_free(pRgbBuffer);
    av_frame_free(&pFrameRGB);
    emit signalShowSnapShot(m_snapFilePathName);
    m_snapFilePathName.clear();
}

// ═══════════════════════════════════════════════════════════════════════════
//  SDL resource management
// ═══════════════════════════════════════════════════════════════════════════
void RenderThreadSdl::destroySDLResources()
{
    if (m_pSdlTexture)  { SDL_DestroyTexture(m_pSdlTexture);   m_pSdlTexture  = nullptr; }
    if (m_pSdlRenderer) { SDL_DestroyRenderer(m_pSdlRenderer); m_pSdlRenderer = nullptr; }
    if (m_pSdlWindow)   { SDL_DestroyWindow(m_pSdlWindow);     m_pSdlWindow   = nullptr; }
}

void RenderThreadSdl::rebuildRender()
{
    if (m_pSdlTexture)  { SDL_DestroyTexture(m_pSdlTexture);   m_pSdlTexture  = nullptr; }
    if (m_pSdlRenderer) { SDL_DestroyRenderer(m_pSdlRenderer); m_pSdlRenderer = nullptr; }
    m_pSdlRenderer = SDL_CreateRenderer(m_pSdlWindow, -1, SDL_RENDERER_SOFTWARE);
#ifdef Q_OS_WIN32
    m_pSdlTexture = SDL_CreateTexture(m_pSdlRenderer, SDL_PIXELFORMAT_IYUV,
                                      SDL_TEXTUREACCESS_STREAMING,
                                      m_videoWidth, m_videoHeight);
#endif
}

void RenderThreadSdl::checkWindowStatus()
{
    Uint32 flags     = SDL_GetWindowFlags(m_pSdlWindow);
    bool   minimized = (flags & SDL_WINDOW_MINIMIZED) != 0;
    if (minimized) {
        m_bWindow_visiable = false;
        return;
    }
    if (!m_bWindow_visiable) {
        m_bWindow_visiable        = true;
        m_bNeed_RebuildRenderer   = true;
    }
}

void RenderThreadSdl::calculateShowRect(DispCtx *ctx)
{
    if (ctx->win_height == 0 || ctx->src_height == 0) return;
    float src_ratio = static_cast<float>(ctx->src_width)  / ctx->src_height;
    float win_ratio = static_cast<float>(ctx->win_width)  / ctx->win_height;

    if (src_ratio > win_ratio) {
        ctx->show_rect.w = ctx->win_width;
        ctx->show_rect.h = static_cast<int>(ctx->win_width / src_ratio);
        ctx->show_rect.x = 0;
        ctx->show_rect.y = (ctx->win_height - ctx->show_rect.h) / 2;
    } else {
        ctx->show_rect.w = static_cast<int>(ctx->win_height * src_ratio);
        ctx->show_rect.h = ctx->win_height;
        ctx->show_rect.x = (ctx->win_width - ctx->show_rect.w) / 2;
        ctx->show_rect.y = 0;
    }
}

#ifdef Q_OS_WIN32
void RenderThreadSdl::getWindowSize(HWND hwnd, int *width, int *height)
{
    RECT rect;
    if (GetClientRect(hwnd, &rect)) {
        *width  = rect.right  - rect.left;
        *height = rect.bottom - rect.top;
    }
}
#endif
