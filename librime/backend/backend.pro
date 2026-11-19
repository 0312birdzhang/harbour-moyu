TEMPLATE = app
TARGET = harbour-moyu-rime
QT = core dbus
CONFIG += console c++1z
CONFIG -= app_bundle
QMAKE_LFLAGS += -pie

include(rimeengine.pri)
SOURCES += main.cpp

target.path = /usr/bin
dbusService.files = xyz.birdzhang.moyu.Rime.service
dbusService.path = /usr/share/dbus-1/services
systemdService.files = harbour-moyu-rime.service
systemdService.path = /usr/lib/systemd/user
data.files = $$nativeRoot/data/*
data.path = /usr/share/harbour-moyu/rime
licenses.files = $$nativeRoot/licenses/*
licenses.path = /usr/share/licenses/harbour-moyu/rime
INSTALLS += target dbusService systemdService data licenses
