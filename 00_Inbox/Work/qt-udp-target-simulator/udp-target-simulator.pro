QT += widgets network
CONFIG += c++11
TEMPLATE = app
TARGET = udp-target-simulator

SOURCES += main.cpp packet1501.cpp
HEADERS += packet1501.h

win32-msvc*: QMAKE_CXXFLAGS += /utf-8
