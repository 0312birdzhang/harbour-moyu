TEMPLATE = app
TARGET = moyu-rime-smoke
QT += core dbus qml quick testlib
CONFIG += console c++11
CONFIG -= app_bundle

SOURCES += rime-smoke.cpp ../librime/backend/legacydictionary.cpp
HEADERS += ../librime/backend/legacydictionary.h
