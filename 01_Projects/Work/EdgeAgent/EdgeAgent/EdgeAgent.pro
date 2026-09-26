QT += core network serialport serialbus

CONFIG += console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = edgeagent

SOURCES += \
    src/main.cpp \
    src/Config.cpp \
    src/FrameCodec.cpp \
    src/UdpChannel.cpp \
    src/TcpChannel.cpp \
    src/SerialChannel.cpp \
    src/CanChannel.cpp \
    src/EdgeAgent.cpp \
    src/MqttClient.cpp \
    src/Logging.cpp

HEADERS += \
    src/Config.h \
    src/FrameCodec.h \
    src/FrameEvent.h \
    src/Channel.h \
    src/UdpChannel.h \
    src/TcpChannel.h \
    src/SerialChannel.h \
    src/CanChannel.h \
    src/EdgeAgent.h \
    src/MqttClient.h \
    src/Logging.h

LIBS += -lmosquitto
LIBS += -lspdlog -lfmt

unix:!macx {
    QMAKE_CXXFLAGS += -Wall -Wextra -Wpedantic
}

win32 {
    QMAKE_CXXFLAGS += /W4 /permissive-
}
