#-------------------------------------------------
#
# Project created by QtCreator 2025-07-29T09:26:14
#
#-------------------------------------------------

QT       += core gui xml
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets
TARGET = com_uiplugin_ButtonMenu
TEMPLATE = lib
CONFIG += plugin
DEFINES += BUTTONMENU_LIBRARY
QMAKE_CXXFLAGS_RELEASE += -g
QMAKE_LFLAGS_RELEASE   += -g
# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

win32 {
    CONFIG += force_debug_info
    CONFIG += separate_debug_info
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
    win32|unix: LIBS += -L$$PWD/../../../../bin/ -lFrameFactory
    win32|unix: LIBS += -L$$PWD/../../../../bin/ -lFrameTools
}else{
    DEFINES += NDEBUG
    DEPENDPATH += $$PWD/../../../../binr
    DESTDIR += $$PWD/../../../../binr/store/scoutmonitor/com_uiplugin_ButtonMenu/1.0
    win32|unix: LIBS += -L$$PWD/../../../../binr/ -lFrameTools
    win32|unix: LIBS += -L$$PWD/../../../../binr/ -lFrameFactory
}

INCLUDEPATH += $$PWD/dds
INCLUDEPATH += $$PWD/../../../../include
INCLUDEPATH += $$PWD/../../../../include/API/FrameFactory
INCLUDEPATH += $$PWD/../../../../include/API/FrameTools
DEPENDPATH += $$PWD/../../../../lib

SOURCES += \
        ButtonMenu.cpp \
    ButtonMenuWidget.cpp \
    MenuWidget.cpp

HEADERS += \
        ButtonMenu.h \
        buttonmenu_global.h \ 
    ButtonMenuWidget.h \
    MenuWidget.h

unix {
    target.path = /usr/lib
    INSTALLS += target
}

FORMS += \
    ButtonMenuWidget.ui \
    MenuWidget.ui

RESOURCES += \
    resource.qrc


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
