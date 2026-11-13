#-------------------------------------------------
#
# Project created by QtCreator 2025-08-29T08:35:09
#
#-------------------------------------------------

QT       += widgets
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets
TARGET = com_uiplugin_PullButtonUi
TEMPLATE = lib

DEFINES += PULLBUTTONUI_LIBRARY
CONFIG += plugin
DEFINES += QT_DEPRECATED_WARNINGS


win32 {
    CONFIG += force_debug_info
    CONFIG += separate_debug_info
}

linux {
    QMAKE_CXXFLAGS_RELEASE += -g
    QMAKE_LFLAGS_RELEASE += -g
}

CONFIG(debug,debug|release){
    DEFINES += DEBUG
    DEPENDPATH += $$PWD/../../../../bin
    DESTDIR += $$PWD/../../../../bin
    DESTDIR += $$PWD/../../../../binr/store/scoutmonitor/com_uiplugin_PullButtonUi/1.0
    win32|unix: LIBS += -L$$PWD/../../../../bin/ -lFrameFactory
    win32|unix: LIBS += -L$$PWD/../../../../binr/ -lFrameTools
}else{
    DEFINES += NDEBUG
    DEPENDPATH += $$PWD/../../../../binr
    DESTDIR += $$PWD/../../../../binr/store/scoutmonitor/com_uiplugin_PullButtonUi/1.0
    win32|unix: LIBS += -L$$PWD/../../../../binr/ -lFrameFactory
    win32|unix: LIBS += -L$$PWD/../../../../binr/ -lFrameTools
}
INCLUDEPATH += $$PWD/dds
INCLUDEPATH += $$PWD/../../../../include
INCLUDEPATH += $$PWD/../../../../include/API/FrameFactory
INCLUDEPATH += $$PWD/../../../../include/API/FrameTools
DEPENDPATH += $$PWD/../../../../lib

SOURCES += PullButtonUi.cpp \
    PullButtonUiWidget.cpp

HEADERS += PullButtonUi.h\
    PullButtonUiWidget.h

unix {
    target.path = /usr/lib
    INSTALLS += target
}

FORMS += \
    PullButtonUiWidget.ui

RESOURCES += \
    res.qrc
