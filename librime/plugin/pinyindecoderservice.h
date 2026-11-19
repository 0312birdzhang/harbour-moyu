// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MOYU_PINYIN_DECODER_SERVICE_H
#define MOYU_PINYIN_DECODER_SERVICE_H

#include <QObject>
#include <QStringList>
#include <QVariant>
#include <QPointer>

class RimeBackend;

// Keyboard clients use a shared local engine. Settings clients use D-Bus.
class MoyuPinyinDecoderService : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(MoyuPinyinDecoderService)
    Q_PROPERTY(bool keyboardMode MEMBER keyboardMode)
    Q_PROPERTY(bool inProcess READ inProcess NOTIFY inProcessChanged)

public:
    explicit MoyuPinyinDecoderService(QObject *parent = nullptr);
    bool inProcess() const;

    Q_INVOKABLE bool init();
    Q_INVOKABLE void setUserDictionary(bool enabled);
    Q_INVOKABLE int search(const QString &spelling);
    Q_INVOKABLE void resetSearch();
    Q_INVOKABLE QString candidateAt(int index);
    Q_INVOKABLE int chooceCandidate(int index);
    Q_INVOKABLE int backspace();
    Q_INVOKABLE QVariantMap searchComposition(const QString &spelling, int pageSize);
    Q_INVOKABLE QVariantMap backspaceComposition(int pageSize);
    Q_INVOKABLE QVariantMap selectComposition(int index, int pageSize, int revision);
    Q_INVOKABLE QVariantMap candidatePage(int offset, int limit, int revision);
    Q_INVOKABLE QString takeCommit();
    Q_INVOKABLE QString commitComposition(bool raw);
    Q_INVOKABLE QString preeditText();
    Q_INVOKABLE QString pinyinString(bool decoded);
    Q_INVOKABLE QStringList spellingStartPositions();
    Q_INVOKABLE int getSplStart();
    Q_INVOKABLE int fixedLength();
    Q_INVOKABLE void flushCache();
    Q_INVOKABLE QStringList predictionList(const QString &history, int fetchSize);
    Q_INVOKABLE void recordPrediction(const QString &text);

    Q_INVOKABLE QString addUserPhrase(const QString &phrase, const QString &pinyin);
    Q_INVOKABLE int userDictionaryEntryCount();
    Q_INVOKABLE QVariantList userDictionaryEntries();
    Q_INVOKABLE QStringList userDictionaryEntryList();
    Q_INVOKABLE QStringList userDictionaryEntryPage(int offset, int limit);
    Q_INVOKABLE QString removeUserPhrase(const QString &phrase, const QString &pinyin);
    Q_INVOKABLE QString updateUserPhrase(const QString &oldPhrase, const QString &oldPinyin,
                                         const QString &newPhrase, const QString &newPinyin);
    Q_INVOKABLE QString userDictionaryPath();
    Q_INVOKABLE bool resetUserDictionary();
    Q_INVOKABLE bool reloadUserDictionaryIfChanged();
    Q_INVOKABLE QString migrationStatus();
    Q_INVOKABLE QString engineVersion();
    Q_INVOKABLE QString dictionaryFiles();
    Q_INVOKABLE void manageDictionaryFile(const QString &operation, const QString &value, bool enabled);

signals:
    void inProcessChanged();
    void dictionaryOperationFinished(const QString &operation, const QString &result);

private:
    QVariant call(const QString &method, const QVariantList &arguments = QVariantList());
    QVariant localCall(const QString &method, const QVariantList &arguments);
    QVariantMap compositionCall(const QString &method, const QVariantList &arguments);
    QString dictionaryCall(const QString &method, const QVariantList &arguments);
    QString lastError;
    bool keyboardMode = false;
    bool directAttempted = false;
    QPointer<RimeBackend> localBackend;
};

#endif
