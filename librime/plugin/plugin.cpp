// SPDX-License-Identifier: GPL-3.0-or-later
#include "plugin.h"
#include "pinyindecoderservice.h"

#include <qqml.h>

void MoyuPinyinPlugin::registerTypes(const char *uri)
{
    qmlRegisterType<MoyuPinyinDecoderService>(uri, 1, 0, "MoyuPinyin");
}
