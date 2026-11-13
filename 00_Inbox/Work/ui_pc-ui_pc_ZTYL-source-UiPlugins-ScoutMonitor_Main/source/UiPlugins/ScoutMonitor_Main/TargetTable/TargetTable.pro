#-------------------------------------------------
#
# Project created by QtCreator 2025-08-14T13:09:59
#
#-------------------------------------------------

QT       += widgets network
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets
TARGET = com_uiplugin_TargetTable
TEMPLATE = lib
msvc:QMAKE_CXXFLAGS += /source-charset:utf-8

DEFINES += TARGETTABLE_LIBRARY
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
    DESTDIR += $$PWD/../../../../binr/store/scoutmonitor/com_uiplugin_TargetTable/1.0
    win32|unix: LIBS += -L$$PWD/../../../../bin/ -lFrameFactory
    win32|unix: LIBS += -L$$PWD/../../../../bin/ -lFrameTools
    # LicenseCheck 仅有 x86 预编译库；x64 构建跳过（与既有 binr_x64 产物一致）
    !contains(QT_ARCH, x86_64): win32|unix: LIBS += -L$$PWD/../../../../bin/ -lLicenseCheck
}else{
    DEFINES += NDEBUG
    win32:contains(QT_ARCH, x86_64) {
        DEPENDPATH += $$PWD/../../../../binr
        DESTDIR = $$PWD/../../../../binr/store/scoutmonitor/com_uiplugin_TargetTable/1.0
        LIBS += -L$$PWD/../../../../binr/ -lFrameFactory -lFrameTools
    }
    win32:!contains(QT_ARCH, x86_64) {
        DEPENDPATH += $$PWD/../../../../binr_x86
        DESTDIR = $$PWD/../../../../binr_x86/store/scoutmonitor/com_uiplugin_TargetTable/1.0
        LIBS += -L$$PWD/../../../../binr_x86/ -lFrameFactory -lFrameTools -lLicenseCheck
    }
    unix {
        DEPENDPATH += $$PWD/../../../../binr
        DESTDIR = $$PWD/../../../../binr/store/scoutmonitor/com_uiplugin_TargetTable/1.0
        LIBS += -L$$PWD/../../../../binr/ -lFrameFactory -lFrameTools
    }
}
INCLUDEPATH += $$PWD/dds
INCLUDEPATH += $$PWD/../../../../include
INCLUDEPATH += $$PWD/../../../../include/API/FrameFactory
INCLUDEPATH += $$PWD/../../../../include/API/FrameTools
DEPENDPATH += $$PWD/../../../../lib

win32:contains(QT_ARCH, x86_64):CONFIG(release, debug|release): LIBS += -L$$PWD/../../../../binr/ -lCoordinate
win32:!contains(QT_ARCH, x86_64):CONFIG(release, debug|release): LIBS += -L$$PWD/../../../../binr_x86/ -lCoordinate
unix:CONFIG(release, debug|release): LIBS += -L$$PWD/../../../../binr/ -lCoordinate
else:win32:CONFIG(debug, debug|release): LIBS += -L$$PWD/../../../../binr/ -lCoordinated
else:unix:CONFIG(debug, debug|release): LIBS += -L$$PWD/../../../../binr/ -lCoordinate

SOURCES += \
        TargetTable.cpp \
    TargetTableWidget.cpp \
    Component/TargetItem.cpp \
    Comm/TargetInfoSocket.cpp \
    Comm/TcpComm.cpp

HEADERS += \
        TargetTable.h \
    TargetTableWidget.h \
    Component/TargetItem.h \
    definestruct.h \
    Comm/TargetInfoSocket.h \
    Comm/TcpComm.h

unix {
    target.path = /usr/lib
    INSTALLS += target
}

FORMS += \
    TargetTableWidget.ui \
    Component/TargetItem.ui

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



