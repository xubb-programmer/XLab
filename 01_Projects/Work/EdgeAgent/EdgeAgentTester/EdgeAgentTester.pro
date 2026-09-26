QT += core gui widgets network serialport serialbus

CONFIG += c++17
CONFIG += warn_on
TEMPLATE = app
TARGET = edgeagent-tester

SOURCES += \
    src/main.cpp \
    src/TestFrameDecoder.cpp \
    src/TestMainWindow.cpp

HEADERS += \
    src/TestFrameDecoder.h \
    src/TestMainWindow.h

unix:!macx {
    QMAKE_CXXFLAGS += -Wall -Wextra -Wpedantic
}

win32 {
    QMAKE_CXXFLAGS += /W4 /permissive-
}
