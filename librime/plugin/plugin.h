// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MOYU_PINYIN_PLUGIN_H
#define MOYU_PINYIN_PLUGIN_H

#include <QQmlExtensionPlugin>

class MoyuPinyinPlugin : public QQmlExtensionPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QQmlExtensionInterface")

public:
    void registerTypes(const char *uri) override;
};

#endif
