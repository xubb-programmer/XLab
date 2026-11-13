/*
* File：RenderThread_sdl.h
* Author：huangjin
* Created：2025-08-08
* Last Modified：2025-08-12
* Description：渲染线程 —— SDL2 软件渲染后端（多格式自适应版）
*              继承自 RenderThread 抽象基类
*/
#ifndef RENDERTHREAD_SDL_H
#define RENDERTHREAD_SDL_H

#include "utils/RenderThread.h"

#ifdef Q_OS_WIN32
#include <windows.h>
#endif

class RenderThreadSdl : public RenderThread
{
    Q_OBJECT
public:
    explicit RenderThreadSdl(SafeQueue<AVFrame*> *frameQueue, QObject *parent = nullptr);
    ~RenderThreadSdl();

    /** 初始化 SDL2 窗口/渲染器/纹理并绑定宿主窗口 */
    void init(unsigned long winId, int width, int height) override;

    void run()      override;
    void stop()     override;
    void snapShot(const QString &path) override;
    void saveFrame(AVFrame *pFrame)    override;

private:
    void rebuildRender();
    void checkWindowStatus();
    void destroySDLResources();

#ifdef Q_OS_WIN32
    void getWindowSize(HWND hwnd, int *width, int *height);
#endif

    // ── 显示矩形（保持宽高比）──────────────────────────────────────
    struct DispCtx {
        int      src_width  = 0;
        int      src_height = 0;
        int      win_width  = 0;
        int      win_height = 0;
        SDL_Rect show_rect  = {0, 0, 0, 0};
    };
    void calculateShowRect(DispCtx *ctx);

    // ── SDL 对象 ───────────────────────────────────────────────────
    SDL_Window   *m_pSdlWindow   = nullptr;
    SDL_Renderer *m_pSdlRenderer = nullptr;
    SDL_Texture  *m_pSdlTexture  = nullptr;

    // ── 状态 ──────────────────────────────────────────────────────
    bool          m_bWindow_visiable     = true;
    bool          m_bNeed_RebuildRenderer = false;
    AVPixelFormat m_lastPixFmt           = AV_PIX_FMT_NONE;
    DispCtx       display_ctx;
};

#endif // RENDERTHREAD_SDL_H
