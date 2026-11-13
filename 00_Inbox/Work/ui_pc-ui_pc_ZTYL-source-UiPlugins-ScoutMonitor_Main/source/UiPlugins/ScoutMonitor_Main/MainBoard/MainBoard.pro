#-------------------------------------------------
#
# Project created by QtCreator 2018-12-11T17:15:19
#
#-------------------------------------------------


QT       += widgets core network

QT       += gui

TARGET = com_uiplugin_MainBoard
TEMPLATE = lib
CONFIG += plugin
msvc:QMAKE_CXXFLAGS += /source-charset:utf-8
DEFINES += MAINBOARD_LIBRARY

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS
RC_FILE += resource.rc

win32 {
    # VS2015 x64 linker crashes in IMAGE::BuildImage.FinalPhase when this
    # small resource-bearing plugin is linked with release PDB generation.
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
    win32: LIBS += -L$$PWD/../../../../bin/ -lFrameFactory
    win32: LIBS += -L$$PWD/../../../../bin/ -lFrameTools
}else{
    DEFINES += NDEBUG
    win32:contains(QT_ARCH, x86_64) {
        DEPENDPATH += $$PWD/../../../../binr
        DESTDIR = $$PWD/../../../../binr/store/scoutmonitor/com_uiplugin_MainBoard/1.0
        LIBS += -L$$PWD/../../../../binr/ -lFrameTools -lFrameFactory
    }
    win32:!contains(QT_ARCH, x86_64) {
        DEPENDPATH += $$PWD/../../../../binr_x86
        DESTDIR = $$PWD/../../../../binr_x86/store/scoutmonitor/com_uiplugin_MainBoard/1.0
        LIBS += -L$$PWD/../../../../binr_x86/ -lFrameTools -lFrameFactory
    }
    unix {
        DEPENDPATH += $$PWD/../../../../binr
        DESTDIR = $$PWD/../../../../binr/store/scoutmonitor/com_uiplugin_MainBoard/1.0
        LIBS += -L$$PWD/../../../../binr/ -lFrameTools -lFrameFactory
    }
}

INCLUDEPATH += $$PWD/dds
INCLUDEPATH += $$PWD/../../../../include
INCLUDEPATH += $$PWD/../../../../include/API/FrameFactory
INCLUDEPATH += $$PWD/../../../../include/API/FrameTools
DEPENDPATH += $$PWD/../../../../lib

SOURCES += \
        MainBoard.cpp \
    MianBoardWidget.cpp

HEADERS += \
        MainBoard.h \
        mainboard_global.h  \
    MianBoardWidget.h \
    resource.rc

unix {
    target.path = /usr/lib
    INSTALLS += target
}

FORMS += \
    MianBoardWidget.ui

RESOURCES += \
    mainboard.qrc
