#-------------------------------------------------
#
# Project created by QtCreator 2025-08-29T15:57:30
#
#-------------------------------------------------

QT       += widgets network
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets
TARGET = com_uiplugin_OcrRecognition
TEMPLATE = lib
msvc:QMAKE_CXXFLAGS += /source-charset:utf-8
DEFINES += OCRRECOGNITION_LIBRARY
CONFIG += plugin
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


CONFIG(debug,debug|release){
    DEFINES += DEBUG
    DEPENDPATH += $$PWD/../../../../bin
    DESTDIR += $$PWD/../../../../bin
    DESTDIR += $$PWD/../../../../binr/store/scoutmonitor/com_uiplugin_OcrRecognition/1.0
    win32|unix: LIBS += -L$$PWD/../../../../bin/ -lFrameFactory
    win32|unix: LIBS += -L$$PWD/../../../../binr/ -lFrameTools
}else{
    DEFINES += NDEBUG
    win32:contains(QT_ARCH, x86_64) {
        DEPENDPATH += $$PWD/../../../../binr
        DESTDIR = $$PWD/../../../../binr/store/scoutmonitor/com_uiplugin_OcrRecognition/1.0
        LIBS += -L$$PWD/../../../../binr/ -lFrameFactory -lFrameTools
    }
    win32:!contains(QT_ARCH, x86_64) {
        DEPENDPATH += $$PWD/../../../../binr_x86
        DESTDIR = $$PWD/../../../../binr_x86/store/scoutmonitor/com_uiplugin_OcrRecognition/1.0
        LIBS += -L$$PWD/../../../../binr_x86/ -lFrameFactory -lFrameTools
    }
    unix {
        DEPENDPATH += $$PWD/../../../../binr
        DESTDIR = $$PWD/../../../../binr/store/scoutmonitor/com_uiplugin_OcrRecognition/1.0
        LIBS += -L$$PWD/../../../../binr/ -lFrameFactory -lFrameTools
    }
}
INCLUDEPATH += $$PWD/dds
INCLUDEPATH += $$PWD/../../../../include
INCLUDEPATH += $$PWD/../../../../include/API/FrameFactory
INCLUDEPATH += $$PWD/../../../../include/API/FrameTools
DEPENDPATH += $$PWD/../../../../lib

SOURCES += OcrRecognition.cpp \
    OcrRecognitionWidget.cpp \
    Comm/CustomMoveWidget.cpp \
    ImageDeal/imagewidget.cpp \
    ImageDeal/image_downloader.cpp \
    Comm/DataMgr.cpp \
    Comm/sseclient.cpp

HEADERS += OcrRecognition.h \
    OcrRecognitionWidget.h \
    Comm/CustomMoveWidget.h \
    ImageDeal/imagewidget.h \
    Comm/definestruct.h \
    ImageDeal/image_downloader.h \
    Comm/DataMgr.h \
    Comm/sseclient.h

unix {
    target.path = /usr/lib
    INSTALLS += target
}

FORMS += \
    OcrRecognitionWidget.ui

RESOURCES += \
    res.qrc
