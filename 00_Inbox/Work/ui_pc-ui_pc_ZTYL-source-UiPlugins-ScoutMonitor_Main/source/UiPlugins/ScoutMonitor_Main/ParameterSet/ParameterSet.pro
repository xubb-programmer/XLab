#-------------------------------------------------
#
# Project created by QtCreator 2025-08-13T13:40:39
#
#-------------------------------------------------

QT       += widgets network
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets
TARGET = com_uiplugin_ParameterSet
TEMPLATE = lib
msvc:QMAKE_CXXFLAGS += /source-charset:utf-8

DEFINES += PARAMETERSET_LIBRARY
CONFIG += plugin

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

win32 {
    !contains(QT_ARCH, x86_64) {
        CONFIG += force_debug_info
        CONFIG += separate_debug_info
    }
}

linux {
    QMAKE_CXXFLAGS_RELEASE += -g
    QMAKE_LFLAGS_RELEASE += -g
}

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0
CONFIG(debug,debug|release){
    DEFINES += DEBUG
    DEPENDPATH += $$PWD/../../../../bin
    DESTDIR += $$PWD/../../../../bin
    DESTDIR += $$PWD/../../../../binr/store/scoutmonitor/com_uiplugin_ParameterSet/1.0
    win32|unix: LIBS += -L$$PWD/../../../../bin/ -lFrameFactory
    win32|unix: LIBS += -L$$PWD/../../../../binr/ -lFrameTools
}else{
    DEFINES += NDEBUG
    win32:contains(QT_ARCH, x86_64) {
        DEPENDPATH += $$PWD/../../../../binr
        DESTDIR = $$PWD/../../../../binr/store/scoutmonitor/com_uiplugin_ParameterSet/1.0
        LIBS += -L$$PWD/../../../../binr/ -lFrameFactory -lFrameTools
    }
    win32:!contains(QT_ARCH, x86_64) {
        DEPENDPATH += $$PWD/../../../../binr_x86
        DESTDIR = $$PWD/../../../../binr_x86/store/scoutmonitor/com_uiplugin_ParameterSet/1.0
        LIBS += -L$$PWD/../../../../binr_x86/ -lFrameFactory -lFrameTools
    }
    unix {
        DEPENDPATH += $$PWD/../../../../binr
        DESTDIR = $$PWD/../../../../binr/store/scoutmonitor/com_uiplugin_ParameterSet/1.0
        LIBS += -L$$PWD/../../../../binr/ -lFrameFactory -lFrameTools
    }
}
INCLUDEPATH += $$PWD/dds
INCLUDEPATH += $$PWD/../../../../include
INCLUDEPATH += $$PWD/../../../../include/API/FrameFactory
INCLUDEPATH += $$PWD/../../../../include/API/FrameTools
DEPENDPATH += $$PWD/../../../../lib

SOURCES += \
    Comm/MulticastClient.cpp \
        ParameterSet.cpp \
    ParameterSetWidget.cpp \
    GenericComponent/ParaContentWidget.cpp \
    GenericComponent/ModelContentWidget.cpp \
    Comm/CustomMoveWidget.cpp \
    GenericComponent/VideoSetWidget.cpp \
    GenericComponent/ModelItemWdiget.cpp \
    Comm/httpworker.cpp \
    GenericComponent/OcrRecognitionSetWidget.cpp \
    GenericComponent/ScreenPushSetWidget.cpp \
    Comm/sseclient.cpp \
    GenericComponent/NetworkSetWidget.cpp \
    GenericComponent/NetWorkItemWidget.cpp \
    GenericComponent/OtherSetWidget.cpp \
    GenericComponent/DeviceListWidget.cpp \
    GenericComponent/DeviceListItemWidget.cpp

HEADERS += \
    Comm/CommunicationIniUtils.h \
    Comm/MulticastClient.h \
        ParameterSet.h \
    ParameterSetWidget.h \
    GenericComponent/ParaContentWidget.h \
    GenericComponent/ModelContentWidget.h \
    Comm/CustomMoveWidget.h \
    GenericComponent/VideoSetWidget.h \
    GenericComponent/definestruct.h \
    GenericComponent/ModelItemWdiget.h \
    Comm/httpworker.h \
    GenericComponent/OcrRecognitionSetWidget.h \
    GenericComponent/ScreenPushSetWidget.h \
    Comm/sseclient.h \
    GenericComponent/NetworkSetWidget.h \
    GenericComponent/NetWorkItemWidget.h \
    GenericComponent/OtherSetWidget.h \
    GenericComponent/DeviceListWidget.h \
    GenericComponent/DeviceListItemWidget.h

unix {
    target.path = /usr/lib
    INSTALLS += target
}

FORMS += \
    ParameterSetWidget.ui \
    GenericComponent/ParaContentWidget.ui \
    GenericComponent/ModelContentWidget.ui \
    GenericComponent/VideoSetWidget.ui \
    GenericComponent/ModelItemWdiget.ui \
    GenericComponent/OcrRecognitionSetWidget.ui \
    GenericComponent/ScreenPushSetWidget.ui \
    GenericComponent/NetworkSetWidget.ui \
    GenericComponent/NetWorkItemWidget.ui \
    GenericComponent/OtherSetWidget.ui \
    GenericComponent/DeviceListWidget.ui \
    GenericComponent/DeviceListItemWidget.ui

RESOURCES += \
    res.qrc

# Windows XP 定向 + Qt5.10 MSVC（32/64 位自动选择 SDK 库路径）
# 仅在已安装 Windows SDK 7.1A 时启用；否则使用 VS/当前 Windows Kit
win32-msvc* {
    WIN_SDK_ROOT = C:/Program Files (x86)/Microsoft SDKs/Windows/v7.1A
    exists($$WIN_SDK_ROOT/Include) {
        DEFINES += _ATL_XP_TARGETING
        QMAKE_CFLAGS += /D_USING_V140_SDK71_
        QMAKE_CXXFLAGS += /D_USING_V140_SDK71_
        INCLUDEPATH += "$$WIN_SDK_ROOT/Include"

        contains(QT_ARCH, x86_64) {
            WIN_SDK_LIB = "$$WIN_SDK_ROOT/Lib/x64"
            QMAKE_LFLAGS_WINDOWS += /SUBSYSTEM:WINDOWS,5.02
            QMAKE_LFLAGS_CONSOLE += /SUBSYSTEM:CONSOLE,5.02
            LIBS += "$$WIN_SDK_LIB/kernel32.lib"
            LIBS += "$$WIN_SDK_LIB/user32.lib"
        } else {
            WIN_SDK_LIB = "$$WIN_SDK_ROOT/Lib/x86"
            QMAKE_LFLAGS_WINDOWS += /SUBSYSTEM:WINDOWS,5.01
            QMAKE_LFLAGS_CONSOLE += /SUBSYSTEM:CONSOLE,5.01
        }
        LIBS += -L"$$WIN_SDK_LIB"
    }
}

# 与投屏助手共用实例 1 的采集设置和最近设备
INCLUDEPATH += $$PWD/../../../Tool/ScreenPushTool
SOURCES += $$PWD/../../../Tool/ScreenPushTool/ScreenPushInstance.cpp
HEADERS += $$PWD/../../../Tool/ScreenPushTool/ScreenPushInstance.h
win32: LIBS += -luser32

# 禁用 NMake 批量推导，避免误编助手目录中的同名源文件
win32-msvc*: CONFIG += no_batch
