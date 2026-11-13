#-------------------------------------------------
#
# Project created by QtCreator 2025-08-13T17:24:53
#
#-------------------------------------------------

QT       += widgets network

TEMPLATE = lib
TARGET = com_uiplugin_VideoProcessFFmpeg_XP
DEFINES += VIDEOPROCESSFFMPEG_LIBRARY
CONFIG += plugin
CONFIG += console
CONFIG(debug,debug|release){
}else{
    DEFINES += NDEBUG
    #DEPENDPATH += $$PWD/../../../../binr
    #DESTDIR += $$PWD/../../../../binr
    DESTDIR += $$PWD/../../../../binr/store/scoutmonitor/com_uiplugin_VideoProcessFFmpeg/1.0
    win32|unix: LIBS += -L$$PWD/../../../../binr -lFrameFactory
    win32|unix: LIBS += -L$$PWD/../../../../binr -lFrameTools
}

unix {
    target.path = /usr/lib
    INSTALLS += target
}

win32{
QMAKE_LFLAGS_WINDOWS = /SUBSYSTEM:WINDOWS,5.01
QMAKE_LFLAGS_CONSOLE = /SUBSYSTEM:CONSOLE,5.01

DEFINES += _ATL_XP_TARGETING
#QMAKE_CFLAGS += /D_WIN32_WINNT=0x0501
#QMAKE_CXXFLAGS += /D_WIN32_WINNT=0x0501
#QMAKE_CFLAGS += /D_USING_V140_SDK71_
#QMAKE_CXXFLAGS += /D_USING_V140_SDK71_
LIBS += -L$$quote(C:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A\Lib)
INCLUDEPATH += $$quote(C:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A\Include)
}


INCLUDEPATH += $$PWD/../../../../include/API/
INCLUDEPATH += $$PWD/../../../../include/API/FrameTools
INCLUDEPATH += $$PWD/../../../../include/API/FrameFactory
win32:INCLUDEPATH += $$PWD/../../../../include

#xp系统下需要定义
DEFINES += FFMPEG_252
win32:INCLUDEPATH += $$PWD/../../../../include/ffmpeg2.5.2/include
win32: LIBS += -L$$PWD/../../../../lib/ffmpeg2.5.2/lib/ -lavcodec -lavdevice -lavfilter -lavformat -lavutil -lpostproc -lswresample -lswscale

#32 x86
#win32:INCLUDEPATH += $$PWD/../../../../include/ffmpeg421/include
#win32: LIBS += -L$$PWD/../../../../lib/ffmpeg421_w32/lib/ -lavcodec -lavdevice -lavfilter -lavformat -lavutil -lpostproc -lswresample -lswscale


win32: LIBS += -L$$PWD/../../../../lib/SDL2/ -lSDL2
win32: LIBS += -luser32 -lgdi32
DEFINES += SDL_MAIN_HANDLED

unix:LIBS += -L/usr/lib/aarch64-linux-gnu/ -lavcodec -lavformat -lavutil -lswresample -lswscale
unix:LIBS += -L$$PWD/lib_arm/SDL/ -lSDL2

unix:INCLUDEPATH += $$PWD/include_arm/
FORMS += \
    VideoProcessWidget.ui

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
    utils/SafeQueue.h \
    utils/FFmpegWorkerCommon.h

SOURCES += \
    FFmpegWorker.cpp \
    main.cpp \
    VideoProcessFFmpeg.cpp \
    VideoProcessWidget.cpp \
    utils/DecodeThread.cpp \
    utils/PullThread.cpp \
    utils/RecordThread.cpp \
    utils/RelayThread.cpp \
    utils/RenderThread.cpp

RESOURCES += \
    resource.qrc

