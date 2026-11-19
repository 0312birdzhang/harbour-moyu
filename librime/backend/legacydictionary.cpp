// SPDX-License-Identifier: GPL-3.0-or-later
#include "legacydictionary.h"

#include <QtEndian>

static quint32 readUInt32(const QByteArray &data, int offset)
{
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(data.constData() + offset));
}

bool readLegacyDictionary(const QByteArray &data, const QStringList &spellings,
                          QStringList *entries, QString *error)
{
    entries->clear();
    if (data.size() < 36 || data.size() > 128 * 1024 * 1024 ||
            readUInt32(data, 0) != 0x0abcdef0) {
        *error = QString::fromUtf8("旧词典文件头无效，原文件已保留");
        return false;
    }

    int footer = -1;
    quint32 count = 0;
    quint32 lemmaSize = 0;
    // Older builds used both prediction and sync variants of this format.
    for (int infoSize : {36, 32}) {
        const int offset = data.size() - infoSize;
        const quint32 possibleCount = readUInt32(data, offset + 12);
        const quint32 possibleSize = readUInt32(data, offset + 16);
        const quint32 syncCount = infoSize == 36 ? readUInt32(data, offset + 28) : 0;
        for (int indexBytes : {12, 8}) {
            const quint64 expectedSize = 4ULL + possibleSize +
                    quint64(possibleCount) * indexBytes + quint64(syncCount) * 4 + infoSize;
            if (expectedSize == quint64(data.size()) && possibleCount <= 1000000) {
                footer = offset;
                count = possibleCount;
                lemmaSize = possibleSize;
                break;
            }
        }
        if (footer >= 0)
            break;
    }
    if (footer < 0) {
        *error = QString::fromUtf8("旧词典长度校验失败，原文件已保留");
        return false;
    }

    const int offsetsStart = 4 + int(lemmaSize);
    for (quint32 index = 0; index < count; ++index) {
        const quint32 storedOffset = readUInt32(data, offsetsStart + int(index * 4));
        if (storedOffset & 0x80000000)
            continue;
        const quint32 offset = storedOffset & 0x7fffffff;
        if (offset > lemmaSize || lemmaSize - offset < 2) {
            *error = QString::fromUtf8("旧词典词条偏移无效，原文件已保留");
            return false;
        }
        const int record = 4 + int(offset);
        if (uchar(data.at(record)) & 1)
            continue;
        const int characters = uchar(data.at(record + 1));
        if (characters < 1 || characters > 8 || quint32(2 + 4 * characters) > lemmaSize - offset) {
            *error = QString::fromUtf8("旧词典词条长度无效，原文件已保留");
            return false;
        }

        QString phrase;
        QStringList code;
        for (int character = 0; character < characters; ++character) {
            const quint16 spellingId = qFromLittleEndian<quint16>(
                        reinterpret_cast<const uchar *>(data.constData() + record + 2 + 2 * character));
            const quint16 unicode = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(
                        data.constData() + record + 2 + 2 * characters + 2 * character));
            const int spellingIndex = int(spellingId) - 30;
            if (spellingIndex < 0 || spellingIndex >= spellings.size() ||
                    unicode < 0x3400 || unicode > 0x9fff) {
                *error = QString::fromUtf8("旧词典拼音或汉字无效，原文件已保留");
                return false;
            }
            phrase.append(QChar(unicode));
            code.append(spellings.at(spellingIndex));
        }
        entries->append(phrase + QLatin1Char('\t') + code.join(QLatin1Char(' ')));
    }
    return true;
}
