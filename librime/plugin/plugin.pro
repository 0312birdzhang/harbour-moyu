TEMPLATE = lib
TARGET = xyz.birdzhang.moyu.ime
VERSION = 1.0.0
QT += qml quick dbus
CONFIG += qt plugin c++1z

TARGET = $$qtLibraryTarget($$TARGET)
uri = xyz.birdzhang.moyu.ime

HEADERS += plugin.h pinyindecoderservice.h
SOURCES += plugin.cpp pinyindecoderservice.cpp
include(../backend/rimeengine.pri)

qmldir.files = qmldir
qmldir.path = $$[QT_INSTALL_QML]/$$replace(uri, \\., /)
target.path = $$qmldir.path
layoutScript.files = MoyuLayout.js
layoutScript.path = $$qmldir.path
INSTALLS += target qmldir layoutScript
