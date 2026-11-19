// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MOYU_RIME_BACKEND_H
#define MOYU_RIME_BACKEND_H

#include <QObject>
#include <QMap>
#include <QStringList>
#include <QSet>
#include <QJsonArray>
#include <memory>
#include <rime_api.h>
// Rime's C API uses a macro that collides with QVariant::Bool.
#undef Bool

namespace rime {
class Db;
class Dictionary;
class UserDictionary;
class ReverseLookupDictionary;
}

class RimeBackend : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "xyz.birdzhang.moyu.Rime")

public:
    explicit RimeBackend(QObject *parent = nullptr);
    ~RimeBackend() override;
    bool deploy();

public slots:
    bool init();
    bool releaseForKeyboard();
    void setUserDictionary(bool enabled);
    int search(const QString &spelling);
    void resetSearch();
    QString candidateAt(int index);
    int chooceCandidate(int index);
    int backspace();
    QString searchComposition(const QString &spelling, int pageSize);
    QString backspaceComposition(int pageSize);
    QString selectComposition(int index, int pageSize, int revision);
    QString candidatePage(int offset, int limit, int revision);
    QString takeCommit();
    QString commitComposition(bool raw);
    QString preeditText();
    QString pinyinString(bool decoded);
    QStringList spellingStartPositions();
    int getSplStart();
    int fixedLength();
    void flushCache();
    QStringList predictionList(const QString &history, int fetchSize);
    void recordPrediction(const QString &text);
    QString addUserPhrase(const QString &phrase, const QString &pinyin);
    int userDictionaryEntryCount();
    QStringList userDictionaryEntryList();
    QStringList userDictionaryEntryPage(int offset, int limit);
    QString removeUserPhrase(const QString &phrase, const QString &pinyin);
    QString updateUserPhrase(const QString &oldPhrase, const QString &oldPinyin,
                             const QString &newPhrase, const QString &newPinyin);
    QString userDictionaryPath();
    bool resetUserDictionary();
    bool reloadUserDictionaryIfChanged();
    QString migrationStatus();
    QString engineVersion();
    QString dictionaryFiles();
    QString importDictionaryFile(const QString &path);
    QString setDictionaryFileEnabled(const QString &name, bool enabled);
    QString removeDictionaryFile(const QString &name);
    QString exportDictionaryFile(const QString &name);
    QString exportUserDictionary();

signals:
    void ownershipReleased();

private:
    bool initializeLibrary();
    void destroySession();
    void updateCandidates(int limit = -1);
    void ensureCandidates(int limit);
    int searchInput(const QString &spelling, int limit);
    int selectInputCandidate(int index, int limit);
    int deleteInput(int limit);
    QString compositionSnapshot(int pageSize, bool ok);
    QString normalizePinyin(const QString &spelling, int characters) const;
    QString validatePhrase(const QString &phrase, const QString &spelling,
                           QString *normalized, int minimumLength = 2) const;
    bool migrateLegacyDictionary();
    QString dictionaryKey(const QString &phrase, const QString &spelling) const;
    bool writePhrase(const QString &phrase, const QString &spelling, bool remove);
    bool loadPredictionDictionary();
    QStringList recentPhrases() const;
    bool writeRecentPhrases(const QStringList &phrases);
    void rememberPhrase(const QString &text);
    QString applyDictionaryFiles(const QJsonArray &files, const QString &importName = QString(),
                                 const QByteArray &importData = QByteArray());
    bool prepareDictionaryFiles();

    RimeApi *api;
    RimeSessionId session = 0;
    bool libraryInitialized = false;
    bool ready = false;
    bool ownershipTransferred = false;
    QByteArray sharedDataDirectory;
    QByteArray userDataDirectory;
    rime::Db *userDatabase = nullptr;
    std::unique_ptr<rime::UserDictionary> userDictionary;
    std::unique_ptr<rime::Dictionary> systemDictionary;
    std::unique_ptr<rime::ReverseLookupDictionary> reverseDictionary;
    QSet<QString> syllables;
    QMap<QString, double> systemPhrases;
    QStringList commonPhrases;
    QStringList candidates;
    bool candidatesExhausted = true;
    int compositionRevision = 0;
    QString pendingCommit;
    QString migrationMessage;
    int committedLength = 0;
};

#endif
