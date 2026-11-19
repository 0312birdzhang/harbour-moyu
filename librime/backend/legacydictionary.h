// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MOYU_LEGACY_DICTIONARY_H
#define MOYU_LEGACY_DICTIONARY_H

#include <QByteArray>
#include <QStringList>

// Read the old Android/Google dictionary without loading its decoder.
bool readLegacyDictionary(const QByteArray &data, const QStringList &spellings,
                          QStringList *entries, QString *error);

#endif
