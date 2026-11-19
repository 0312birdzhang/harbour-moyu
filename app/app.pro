TEMPLATE = app
TARGET = harbour-moyu

CONFIG += sailfishapp


SOURCES += src/harbour-moyu.cpp

OTHER_FILES += qml/harbour-moyu.qml \
    qml/pages/FirstPage.qml \
    qml/pages/AboutPage.qml \
    qml/pages/UserDictionaryPage.qml
OTHER_FILES += qml/pages/KeyboardLayoutPage.qml
OTHER_FILES += qml/pages/DictionaryFilesPage.qml


target.path = /usr/bin

qml.files = qml
qml.path = /usr/share/harbour-moyu

desktop.files = harbour-moyu.desktop
desktop.path = /usr/share/applications

sailjail.files = harbour-moyu.profile
sailjail.path = /etc/sailjail/permissions

icon.files = harbour-moyu.png
icon.path = /usr/share/icons/hicolor/86x86/apps

maliit.files = maliit
maliit.path = /usr/share
INSTALLS += target qml desktop sailjail icon maliit

DISTFILES += \
    maliit/plugins/com/jolla/MoyuPinyinHandler.qml \
    maliit/plugins/com/jolla/layouts/zh_cn_moyu.qml \
    maliit/plugins/com/jolla/layouts/layouts_moyu.conf
