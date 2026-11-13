#-------------------------------------------------
#
# Project created by QtCreator 2019-05-11T13:43:01
#
#-------------------------------------------------

QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = com_uiplugin_SignalTransit
TEMPLATE = lib
CONFIG += plugin
# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS
RC_FILE += resource.rc

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
    DESTDIR += $$PWD/../../../../binr/store/scoutmonitor/com_uiplugin_SignalTransit/1.0
    win32|unix: LIBS += -L$$PWD/../../../../binr/ -lFrameFactory
    win32|unix: LIBS += -L$$PWD/../../../../binr/ -lFrameTools
}

INCLUDEPATH += $$PWD/dds
INCLUDEPATH += $$PWD/../../../../include
INCLUDEPATH += $$PWD/../../../../include/API/FrameFactory
INCLUDEPATH += $$PWD/../../../../include/API/FrameTools
DEPENDPATH += $$PWD/../../../../lib

SOURCES += \
    SignalTransit.cpp \
    SignalTransitWidget.cpp

HEADERS += \
    SignalTransit.h \
    SignalTransit_global.h \
    SignalTransitWidget.h \
    resource.rc
