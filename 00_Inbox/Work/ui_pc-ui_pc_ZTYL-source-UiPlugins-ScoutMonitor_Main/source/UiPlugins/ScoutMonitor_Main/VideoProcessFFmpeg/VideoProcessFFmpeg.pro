#-------------------------------------------------
#
# Project created by QtCreator 2025-08-13T17:24:53
#
#-------------------------------------------------

QT       += widgets network

TEMPLATE = lib
TARGET = com_uiplugin_VideoProcessFFmpeg
msvc:QMAKE_CXXFLAGS += /source-charset:utf-8
DEFINES += VIDEOPROCESSFFMPEG_LIBRARY
CONFIG += plugin
CONFIG += console
CONFIG(debug,debug|release){
}else{
    #DEFINES += NDEBUG
    win32:contains(QT_ARCH, x86_64) {
        DEPENDPATH += $$PWD/../../../../binr
        DESTDIR = $$PWD/../../../../binr/store/scoutmonitor/com_uiplugin_VideoProcessFFmpeg/1.0
        LIBS += -L$$PWD/../../../../binr -lFrameFactory -lFrameTools
    }
    win32:!contains(QT_ARCH, x86_64) {
        DEPENDPATH += $$PWD/../../../../binr_x86
        DESTDIR = $$PWD/../../../../binr_x86/store/scoutmonitor/com_uiplugin_VideoProcessFFmpeg/1.0
        LIBS += -L$$PWD/../../../../binr_x86 -lFrameFactory -lFrameTools
    }
    unix: LIBS += -L$$PWD/../../../../binr_arm -lFrameFactory
    unix: LIBS += -L$$PWD/../../../../binr_arm -lFrameTools
    unix:DESTDIR += $$PWD/../../../../binr_arm/store/scoutmonitor/com_uiplugin_VideoProcessFFmpeg/1.0
    #unix:DESTDIR += $$PWD/../../../../binr_arm

}
DEFINES += MY_PLUGIN

unix {
    target.path = /usr/lib
    INSTALLS += target
}

# Windows XP 定向：按目标架构选择 SDK 库目录。
win32 {
    WIN_SDK_ROOT = C:/Program Files (x86)/Microsoft SDKs/Windows/v7.1A
    exists($$WIN_SDK_ROOT/Include) {
        DEFINES += _ATL_XP_TARGETING
        INCLUDEPATH += $$quote($$WIN_SDK_ROOT/Include)
        contains(QT_ARCH, x86_64) {
            QMAKE_LFLAGS_WINDOWS = /SUBSYSTEM:WINDOWS,5.02
            QMAKE_LFLAGS_CONSOLE = /SUBSYSTEM:CONSOLE,5.02
            LIBS += -L$$quote($$WIN_SDK_ROOT/Lib/x64)
        } else {
            QMAKE_LFLAGS_WINDOWS = /SUBSYSTEM:WINDOWS,5.01
            QMAKE_LFLAGS_CONSOLE = /SUBSYSTEM:CONSOLE,5.01
            LIBS += -L$$quote($$WIN_SDK_ROOT/Lib)
        }
    }
}


INCLUDEPATH += $$PWD/../../../../include/API/
INCLUDEPATH += $$PWD/../../../../include/API/FrameTools
INCLUDEPATH += $$PWD/../../../../include/API/FrameFactory
win32:INCLUDEPATH += $$PWD/../../../../include

# FFmpeg/SDL 按目标架构选择，避免 x64 链接到 x86 导入库。
win32:!contains(QT_ARCH, x86_64) {
    INCLUDEPATH += $$PWD/../../../../include/ffmpeg3.2.12/include
    # 优先使用开发目录中的 x86 SDL2 导入库，避免把 binr_x86/SDL2.dll 当作链接库。
    LIBS = -L$$PWD/../../../../lib/SDL2/ -lSDL2 $$LIBS
    LIBS += -L$$PWD/../../../../lib/ffmpeg3.2.12/lib/ -lavcodec -lavdevice -lavfilter -lavformat -lavutil -lswresample -lswscale
}
win32:contains(QT_ARCH, x86_64) {
    INCLUDEPATH += $$PWD/../../../../include/ffmpeg6.1.1/include
    LIBS += -L$$PWD/../../../../lib/ffmpeg6.1.1/lib/ -lavcodec -lavformat -lavutil -lswscale -lswresample -lavdevice -lavfilter
    LIBS += -L$$PWD/../../../../lib/SDL2_x64/ -lSDL2
}
#win32: LIBS += -L$$PWD/../../../../lib/SDL3-3.4.2/lib/x86/ -lSDL3


win32: LIBS += -luser32 -lgdi32
DEFINES += SDL_MAIN_HANDLED
unix:INCLUDEPATH += $$PWD/include_arm/

unix:LIBS += -L/usr/lib/aarch64-linux-gnu/ -lavcodec -lavformat -lavutil -lswresample -lswscale

unix:LIBS += -L$$PWD/lib_arm/SDL/ -lSDL2

#unix:LIBS += -L/usr/local/lib/ -lSDL2
#unix:INCLUDEPATH += /usr/local/include

unix: INCLUDEPATH += /usr/include/gstreamer-1.0 \
               /usr/include/glib-2.0  \
               /usr/lib/aarch64-linux-gnu/glib-2.0/include

unix:LIBS += -L$$PWD/lib_arm/gst_link/ \
    -lgstreamer-1.0 -lgobject-2.0 -lglib-2.0 \
    -lgstvideo-1.0 -lgstbase-1.0 -lgstapp-1.0


FORMS += \
    VideoProcessWidget.ui \
    SnapShotShowWidget.ui

HEADERS += \
    FFmpegWorker.h \
    VideoProcessFFmpeg.h \
    videoprocessffmpeg_global.h \
    VideoProcessWidget.h \
    utils/DecodeThread.h \
    utils/PullThread.h \
    utils/RecordThread.h \
    utils/RelayThread.h \
    utils/RenderThread.h \
    utils/RenderThread_gst.h \
    utils/RenderThread_sdl.h \
    utils/SafeQueue.h \
    utils/FFmpegWorkerCommon.h \
    resource.rc \
    SnapShotShowWidget.h \
    utils/DiskSpaceChecker.h \
    utils/ScreenStreamPusher.h

SOURCES += \
    FFmpegWorker.cpp \
    main.cpp \
    VideoProcessFFmpeg.cpp \
    VideoProcessWidget.cpp \
    utils/DecodeThread.cpp \
    utils/PullThread.cpp \
    utils/RecordThread.cpp \
    utils/RelayThread.cpp \
    utils/RenderThread.cpp \
    utils/RenderThread_gst.cpp \
    utils/RenderThread_sdl.cpp \
    SnapShotShowWidget.cpp \
    utils/DiskSpaceChecker.cpp \
    utils/ScreenStreamPusher.cpp

RESOURCES += \
    resource.qrc

# 与投屏助手共用实例 1 的采集设置和最近设备
INCLUDEPATH += $$PWD/../../../Tool/ScreenPushTool
SOURCES += $$PWD/../../../Tool/ScreenPushTool/ScreenPushInstance.cpp
HEADERS += $$PWD/../../../Tool/ScreenPushTool/ScreenPushInstance.h
win32: LIBS += -luser32

# 禁用 NMake 批量推导，避免误编助手目录中的同名源文件
win32-msvc*: CONFIG += no_batch
CONFIG += c++11
