// SPDX-License-Identifier: GPL-3.0-or-later
#include "../librime/backend/legacydictionary.h"

#include <QAbstractItemModel>
#include <QCryptographicHash>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QEvent>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <qqml.h>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickWindow>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSignalBlocker>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#include <mutex>
#include <thread>
#include <vector>

static const QString serviceName = QStringLiteral("xyz.birdzhang.moyu.Rime");
static const QStringList commonPredictions = QStringLiteral(
            "我 你 不 是 这 有 那 可以 还 一 在 好 这个 要 现在 没").split(QLatin1Char(' '));

// Maliit registers this module inside its server, so the isolated QML engine
// supplies the key-event enum while keyboard-harness.qml supplies the interface.
class SimulatedMaliit : public QObject
{
    Q_OBJECT
    Q_ENUMS(KeyEventType)

public:
    enum KeyEventType { KeyPress, KeyRelease, KeyClick };
};

static void appendUInt32(QByteArray *data, quint32 value)
{
    const quint32 encoded = qToLittleEndian(value);
    data->append(reinterpret_cast<const char *>(&encoded), sizeof(encoded));
}

static void appendUInt16(QByteArray *data, quint16 value)
{
    const quint16 encoded = qToLittleEndian(value);
    data->append(reinterpret_cast<const char *>(&encoded), sizeof(encoded));
}

static QByteArray legacyDictionary(const QStringList &spellings, bool sync, bool prediction,
                                  const QString &phrase = QString::fromUtf8("墨羽"),
                                  const QStringList &code = {QStringLiteral("mo"), QStringLiteral("yu")})
{
    QByteArray data;
    appendUInt32(&data, 0x0abcdef0);
    data.append(char(0));
    data.append(char(phrase.length()));
    for (const QString &syllable : code)
        appendUInt16(&data, quint16(spellings.indexOf(syllable) + 30));
    for (QChar character : phrase)
        appendUInt16(&data, character.unicode());
    appendUInt32(&data, 0); // Spelling index.
    if (prediction)
        appendUInt32(&data, 0);
    appendUInt32(&data, 5); // Frequency.
    for (quint32 value : {0U, 0U, 0U, 1U, quint32(2 + 4 * phrase.length()), 0U, 0U})
        appendUInt32(&data, value);
    if (sync)
        appendUInt32(&data, 0);
    appendUInt32(&data, 5);
    return data;
}

class RimeSmoke : public QObject
{
    Q_OBJECT

public:
    explicit RimeSmoke(const QString &configDirectory) : configDirectory(configDirectory) {}
    ~RimeSmoke() override
    {
        for (QProcess *process : {&server, &configurationServer}) {
            process->terminate();
            if (!process->waitForFinished(3000)) {
                process->kill();
                process->waitForFinished();
            }
        }
    }

private slots:
    void initTestCase()
    {
        // Sailfish's D-Bus activation entry delegates to systemd. Start the
        // configuration service directly on this private test bus instead.
        configurationServer.start(QStringLiteral("/usr/libexec/dconf-service"));
        QVERIFY(configurationServer.waitForStarted());
        QTRY_VERIFY(QDBusConnection::sessionBus().interface()
                    ->isServiceRegistered(QStringLiteral("ca.desrt.dconf")).value());
        dataDirectory = QString::fromLocal8Bit(qgetenv("MOYU_RIME_DATA_DIR"));
        if (dataDirectory.isEmpty())
            dataDirectory = QStringLiteral("/usr/share/harbour-moyu/rime");
        QVERIFY(QDir().mkpath(configDirectory));
        QFile userDirectories(configDirectory + QStringLiteral("/user-dirs.dirs"));
        QVERIFY(userDirectories.open(QIODevice::WriteOnly));
        userDirectories.write((QStringLiteral("XDG_DOCUMENTS_DIR=\"") + configDirectory +
                               QStringLiteral("/exports\"\n")).toUtf8());
        userDirectories.close();
        QFile spellingFile(dataDirectory + QStringLiteral("/legacy-spellings.txt"));
        QVERIFY(spellingFile.open(QIODevice::ReadOnly));
        spellings = QString::fromUtf8(spellingFile.readAll()).split(QLatin1Char('\n'), QString::SkipEmptyParts);
        QCOMPARE(spellings.size(), 416);

        legacyBytes = legacyDictionary(spellings, true, true);
        const QStringList foreignPaths = {
            configDirectory + QStringLiteral("/harbour-souniaoime/pinyin/usr_dict.dat"),
            configDirectory + QStringLiteral("/pinyin/usr_dict.dat")
        };
        for (const QString &path : foreignPaths) {
            QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(legacyBytes), qint64(legacyBytes.size()));
        }
        startServer();
        QVERIFY2(call(QStringLiteral("init")).toBool(), qPrintable(lastError));
        QCOMPARE(call(QStringLiteral("userDictionaryEntryCount")).toInt(), 0);
        QCOMPARE(call(QStringLiteral("migrationStatus")).toString(), QString());
        QCOMPARE(call(QStringLiteral("userDictionaryPath")).toString(),
                 configDirectory + QStringLiteral("/harbour-moyu/rime/moyu_pinyin.userdb"));
        QVERIFY(!QDir(configDirectory + QStringLiteral("/harbour-souniaoime/rime")).exists());
        for (const QString &path : foreignPaths) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), legacyBytes);
        }
        server.terminate();
        QVERIFY(server.waitForFinished(5000));

        legacyPath = configDirectory + QStringLiteral("/harbour-moyu/pinyin/usr_dict.dat");
        QVERIFY(QDir().mkpath(QFileInfo(legacyPath).absolutePath()));
        QFile legacyFile(legacyPath);
        QVERIFY(legacyFile.open(QIODevice::WriteOnly));
        QCOMPARE(legacyFile.write(legacyBytes), qint64(legacyBytes.size()));
        legacyFile.close();
        startServer();
        QVERIFY2(call(QStringLiteral("init")).toBool(), qPrintable(lastError));
        QVERIFY(call(QStringLiteral("userDictionaryEntryList")).toStringList()
                .contains(QString::fromUtf8("墨羽\tmo yu")));
        QCOMPARE(call(QStringLiteral("engineVersion")).toString(), QStringLiteral("1.13.1"));
    }

    void legacyFormats()
    {
        for (bool sync : {false, true}) {
            for (bool prediction : {false, true}) {
                QStringList entries;
                QString error;
                QVERIFY2(readLegacyDictionary(legacyDictionary(spellings, sync, prediction),
                                              spellings, &entries, &error), qPrintable(error));
                QCOMPARE(entries, QStringList{QString::fromUtf8("墨羽\tmo yu")});
            }
        }
        QStringList entries;
        QString error;
        QVERIFY(!readLegacyDictionary(legacyBytes.left(15), spellings, &entries, &error));
        QByteArray corrupt = legacyBytes;
        corrupt[15] = char(0x7f);
        QVERIFY(!readLegacyDictionary(corrupt, spellings, &entries, &error));
    }

    void emptyFieldPredictions()
    {
        call(QStringLiteral("setUserDictionary"), {false});
        const QStringList predictions = call(QStringLiteral("predictionList"), {QString(), 15}).toStringList();
        call(QStringLiteral("setUserDictionary"), {true});
        const QStringList limitedPredictions = commonPredictions.mid(0, 15);
        QCOMPARE(predictions, limitedPredictions);
        QCOMPARE(call(QStringLiteral("predictionList"), {QString(), 16}).toStringList(), commonPredictions);
        QVERIFY(call(QStringLiteral("predictionList"), {QString(), 0}).toStringList().isEmpty());
        QCOMPARE(call(QStringLiteral("predictionList"), {QString(), 1000}).toStringList(), commonPredictions);
    }

    void systemPhrasePredictions()
    {
        call(QStringLiteral("setUserDictionary"), {false});
        const QStringList single = call(QStringLiteral("predictionList"), {QString::fromUtf8("你"), 100}).toStringList();
        const QStringList greeting = call(QStringLiteral("predictionList"), {QString::fromUtf8("你好"), 15}).toStringList();
        const QStringList group = call(QStringLiteral("predictionList"), {QString::fromUtf8("我们"), 100}).toStringList();
        const QStringList boundary = call(QStringLiteral("predictionList"), {QString::fromUtf8("你好，"), 15}).toStringList();
        const QStringList defaults = call(QStringLiteral("predictionList"), {QString(), 15}).toStringList();
        call(QStringLiteral("setUserDictionary"), {true});
        QVERIFY(single.contains(QString::fromUtf8("好")));
        QVERIFY(greeting.contains(QString::fromUtf8("啊")));
        QVERIFY(greeting.contains(QString::fromUtf8("吗")));
        QVERIFY(group.contains(QString::fromUtf8("俩")));
        QCOMPARE(boundary, defaults);
    }

    void contextPredictions()
    {
        const QString phrase = QString::fromUtf8("中华人民共和国");
        const QString spelling = QStringLiteral("zhong hua ren min gong he guo");
        const QStringList originalPredictions = call(QStringLiteral("predictionList"),
                                                     {QString::fromUtf8("中华"), 100}).toStringList();
        QCOMPARE(call(QStringLiteral("addUserPhrase"), {phrase, spelling}).toString(), QString());
        const QStringList longer = call(QStringLiteral("predictionList"), {QString::fromUtf8("我在中华"), 15}).toStringList();
        const QStringList punctuation = call(QStringLiteral("predictionList"), {QString::fromUtf8("中华，"), 15}).toStringList();
        const QStringList defaults = call(QStringLiteral("predictionList"), {QString(), 15}).toStringList();
        QCOMPARE(call(QStringLiteral("removeUserPhrase"), {phrase, spelling}).toString(), QString());
        QCOMPARE(longer.value(0), QString::fromUtf8("人民共和国"));
        QCOMPARE(punctuation, defaults);
        QCOMPARE(longer.toSet().size(), longer.size());
        QCOMPARE(call(QStringLiteral("predictionList"), {QString::fromUtf8("中华"), 100}).toStringList(),
                 originalPredictions);
    }

    void recentPredictionsAndPersistence()
    {
        // This prefix has no continuations in the shipped dictionary.
        const QString history(QChar(0x9fff));
        for (const QString &input : {QStringLiteral("nihao"), QStringLiteral("ceshi")}) {
            call(QStringLiteral("resetSearch"));
            QVERIFY(call(QStringLiteral("search"), {input}).toInt() > 0);
            const QString text = call(QStringLiteral("candidateAt"), {0}).toString();
            QVERIFY(call(QStringLiteral("chooceCandidate"), {0}).toInt() >= 0);
            QCOMPARE(call(QStringLiteral("takeCommit")).toString(), text);
            QCOMPARE(call(QStringLiteral("predictionList"), {history, 15}).toStringList().value(0), text);
        }
        call(QStringLiteral("recordPrediction"), {QString::fromUtf8("俩")});
        QVERIFY2(lastError.isEmpty(), qPrintable(lastError));
        const QStringList recent = call(QStringLiteral("predictionList"), {history, 100}).toStringList();
        QCOMPARE(recent.value(0), QString::fromUtf8("俩"));
        call(QStringLiteral("recordPrediction"), {QString::fromUtf8("俩")});
        call(QStringLiteral("recordPrediction"), {QStringLiteral("hello")});
        QCOMPARE(call(QStringLiteral("predictionList"), {history, 100}).toStringList(), recent);
        QCOMPARE(call(QStringLiteral("predictionList"), {QString(), 100}).toStringList(), commonPredictions);

        const QStringList entries = call(QStringLiteral("userDictionaryEntryList")).toStringList();
        call(QStringLiteral("setUserDictionary"), {false});
        const QStringList disabled = call(QStringLiteral("predictionList"), {history, 100}).toStringList();
        QCOMPARE(disabled.value(0), QString::fromUtf8("的"));
        call(QStringLiteral("recordPrediction"), {QString::fromUtf8("最近候选")});
        call(QStringLiteral("resetSearch"));
        QVERIFY(call(QStringLiteral("search"), {QStringLiteral("shurufa")}).toInt() > 0);
        QVERIFY(call(QStringLiteral("chooceCandidate"), {0}).toInt() >= 0);
        QVERIFY(!call(QStringLiteral("takeCommit")).toString().isEmpty());
        QCOMPARE(call(QStringLiteral("userDictionaryEntryList")).toStringList(), entries);
        QCOMPARE(call(QStringLiteral("predictionList"), {history, 100}).toStringList(), disabled);
        call(QStringLiteral("setUserDictionary"), {true});
        QCOMPARE(call(QStringLiteral("predictionList"), {history, 100}).toStringList(), recent);

        server.terminate();
        QVERIFY(server.waitForFinished(5000));
        startServer();
        QVERIFY(call(QStringLiteral("init")).toBool());
        QCOMPARE(call(QStringLiteral("predictionList"), {history, 100}).toStringList(), recent);

        const QString phrase = QString::fromUtf8("墨羽最近");
        const QString replacement = QString::fromUtf8("墨羽联想");
        QCOMPARE(call(QStringLiteral("addUserPhrase"), {phrase, QStringLiteral("mo yu zui jin")}).toString(), QString());
        call(QStringLiteral("recordPrediction"), {phrase});
        QCOMPARE(call(QStringLiteral("updateUserPhrase"),
                      {phrase, QStringLiteral("mo yu zui jin"), replacement, QStringLiteral("mo yu lian xiang")})
                 .toString(), QString());
        QCOMPARE(call(QStringLiteral("predictionList"), {history, 15}).toStringList().value(0), replacement);
        QCOMPARE(call(QStringLiteral("removeUserPhrase"), {replacement, QStringLiteral("mo yu lian xiang")})
                 .toString(), QString());
        const QStringList remaining = call(QStringLiteral("predictionList"), {history, 100}).toStringList();
        QVERIFY(!remaining.contains(phrase));
        QVERIFY(!remaining.contains(replacement));
        QCOMPARE(remaining.value(0), QString::fromUtf8("俩"));
    }

    void fullPinyinAndCandidateIndices()
    {
        QVERIFY(call(QStringLiteral("search"), {QStringLiteral("NIHAO")}).toInt() > 0);
        QCOMPARE(call(QStringLiteral("pinyinString"), {false}).toString(), QStringLiteral("nihao"));
        const int greeting = candidateIndex(QString::fromUtf8("你好"));
        QVERIFY(greeting >= 0);
        QVERIFY(call(QStringLiteral("chooceCandidate"), {greeting}).toInt() >= 0);
        QCOMPARE(call(QStringLiteral("takeCommit")).toString(), QString::fromUtf8("你好"));

        const QStringList inputs = {QStringLiteral("ceshi"), QStringLiteral("shurufa")};
        const QStringList simplifiedWords = {QString::fromUtf8("测试"), QString::fromUtf8("输入法")};
        for (int index = 0; index < inputs.size(); ++index) {
            call(QStringLiteral("resetSearch"));
            QVERIFY(call(QStringLiteral("search"), {inputs[index]}).toInt() > 0);
            QCOMPARE(call(QStringLiteral("candidateAt"), {0}).toString(), simplifiedWords[index]);
        }

        QVERIFY(call(QStringLiteral("search"), {QStringLiteral("shi")}).toInt() > 25);
        const QString candidate = call(QStringLiteral("candidateAt"), {25}).toString();
        QVERIFY(!candidate.isEmpty());
        QVERIFY(call(QStringLiteral("chooceCandidate"), {25}).toInt() >= 0);
        QCOMPARE(call(QStringLiteral("takeCommit")).toString(), candidate);
        QVERIFY(call(QStringLiteral("candidateAt"), {-1}).toString().isEmpty());
    }

    void compositionPagesAndLegacyCalls()
    {
        const auto snapshot = [this](const QString &method, const QVariantList &arguments) {
            return QJsonDocument::fromJson(call(method, arguments).toString().toUtf8()).object();
        };
        call(QStringLiteral("resetSearch"));
        const int count = call(QStringLiteral("search"), {QStringLiteral("shi")}).toInt();
        QVERIFY(count > 50);
        QStringList expected;
        for (int index = 0; index < count; ++index)
            expected.append(call(QStringLiteral("candidateAt"), {index}).toString());

        call(QStringLiteral("resetSearch"));
        QJsonObject page = snapshot(QStringLiteral("searchComposition"), {QStringLiteral("SHI"), 20});
        QVERIFY(page.value(QStringLiteral("ok")).toBool());
        QCOMPARE(page.value(QStringLiteral("input")).toString(), QStringLiteral("shi"));
        QCOMPARE(page.value(QStringLiteral("preedit")).toString(), call(QStringLiteral("preeditText")).toString());
        QCOMPARE(page.value(QStringLiteral("candidates")).toArray(), QJsonArray::fromStringList(expected.mid(0, 20)));
        QVERIFY(page.value(QStringLiteral("hasMore")).toBool());
        const int revision = page.value(QStringLiteral("revision")).toInt();
        QCOMPARE(call(QStringLiteral("candidateAt"), {25}).toString(), expected[25]);
        QJsonObject remaining = snapshot(QStringLiteral("candidatePage"), {20, 30, revision});
        QVERIFY(remaining.value(QStringLiteral("ok")).toBool());
        QCOMPARE(remaining.value(QStringLiteral("candidates")).toArray(), QJsonArray::fromStringList(expected.mid(20, 30)));
        QVERIFY(remaining.value(QStringLiteral("hasMore")).toBool());
        remaining = snapshot(QStringLiteral("candidatePage"), {20, -1, revision});
        QCOMPARE(remaining.value(QStringLiteral("candidates")).toArray(), QJsonArray::fromStringList(expected.mid(20)));
        QVERIFY(!remaining.value(QStringLiteral("hasMore")).toBool());
        page = snapshot(QStringLiteral("selectComposition"), {25, 20, revision});
        QVERIFY(page.value(QStringLiteral("ok")).toBool());
        QCOMPARE(page.value(QStringLiteral("commit")).toString(), expected[25]);
        QVERIFY(call(QStringLiteral("takeCommit")).toString().isEmpty());
        QVERIFY(page.value(QStringLiteral("candidates")).toArray().isEmpty());

        page = snapshot(QStringLiteral("searchComposition"), {QStringLiteral("nihao"), 20});
        QCOMPARE(call(QStringLiteral("search"), {QStringLiteral("nihao")}).toInt() > 20, true);
        const int firstCharacter = candidateIndex(QString::fromUtf8("你"));
        QVERIFY(firstCharacter >= 0);
        page = snapshot(QStringLiteral("searchComposition"), {QStringLiteral("nihao"), 20});
        page = snapshot(QStringLiteral("selectComposition"),
                        {firstCharacter, 20, page.value(QStringLiteral("revision")).toInt()});
        QVERIFY(page.value(QStringLiteral("ok")).toBool());
        QVERIFY(page.value(QStringLiteral("commit")).toString().isEmpty());
        QVERIFY(page.value(QStringLiteral("preedit")).toString().startsWith(QString::fromUtf8("你")));
        page = snapshot(QStringLiteral("backspaceComposition"), {20});
        QVERIFY(page.value(QStringLiteral("ok")).toBool());
        QCOMPARE(page.value(QStringLiteral("preedit")).toString(), call(QStringLiteral("preeditText")).toString());

        const int oldRevision = page.value(QStringLiteral("revision")).toInt();
        call(QStringLiteral("resetSearch"));
        call(QStringLiteral("search"), {QStringLiteral("zhongguo")});
        QVERIFY(!snapshot(QStringLiteral("candidatePage"), {20, -1, oldRevision}).value(QStringLiteral("ok")).toBool());
        QVERIFY(!snapshot(QStringLiteral("selectComposition"), {0, 20, oldRevision}).value(QStringLiteral("ok")).toBool());
        QCOMPARE(call(QStringLiteral("pinyinString"), {false}).toString(), QStringLiteral("zhongguo"));
        QVERIFY(call(QStringLiteral("takeCommit")).toString().isEmpty());
        call(QStringLiteral("resetSearch"));
        page = snapshot(QStringLiteral("searchComposition"), {QString(), 20});
        QVERIFY(page.value(QStringLiteral("candidates")).toArray().isEmpty());
        QVERIFY(!page.value(QStringLiteral("hasMore")).toBool());
        QVERIFY(!snapshot(QStringLiteral("candidatePage"), {-1, 20, page.value(QStringLiteral("revision")).toInt()})
                .value(QStringLiteral("ok")).toBool());
    }

    void legacySpellingMigration()
    {
        const QStringList words = {QString::fromUtf8("战略"), QString::fromUtf8("疟疾"), QString::fromUtf8("黁鸟")};
        const QList<QStringList> codes = {{QStringLiteral("zhan"), QStringLiteral("lve")},
                                         {QStringLiteral("nve"), QStringLiteral("ji")},
                                         {QStringLiteral("nun"), QStringLiteral("niao")}};
        const QStringList normalized = {QStringLiteral("zhan lue"), QStringLiteral("nue ji"),
                                        QStringLiteral("nun niao")};
        for (int index = 0; index < words.size(); ++index) {
            server.terminate();
            QVERIFY(server.waitForFinished(5000));
            legacyBytes = legacyDictionary(spellings, true, true, words[index], codes[index]);
            QFile legacyFile(legacyPath);
            QVERIFY(legacyFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QCOMPARE(legacyFile.write(legacyBytes), qint64(legacyBytes.size()));
            legacyFile.close();
            startServer();
            QVERIFY(call(QStringLiteral("init")).toBool());
            QVERIFY(call(QStringLiteral("userDictionaryEntryList")).toStringList()
                    .contains(words[index] + QLatin1Char('\t') + normalized[index]));
            call(QStringLiteral("resetSearch"));
            QVERIFY(call(QStringLiteral("search"), {codes[index].join(QString())}).toInt() > 0);
            QVERIFY(candidateIndex(words[index]) >= 0);
            QCOMPARE(call(QStringLiteral("addUserPhrase"), {words[index], codes[index].join(QString())}).toString(), QString());
        }
        call(QStringLiteral("resetSearch"));
        QVERIFY(call(QStringLiteral("search"), {QStringLiteral("nun")}).toInt() > 0);
        QVERIFY(candidateIndex(QString::fromUtf8("黁")) >= 0);
    }

    void initializationRetry()
    {
        server.terminate();
        QVERIFY(server.waitForFinished(5000));
        const QString retryDirectory = configDirectory + QStringLiteral("/retry-data");
        QVERIFY(QDir().mkpath(retryDirectory));
        const QFileInfoList dataFiles = QDir(dataDirectory).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
        for (const QFileInfo &file : dataFiles) {
            if (file.fileName() != QStringLiteral("full-pinyin.txt"))
                QVERIFY(QFile::link(file.absoluteFilePath(), retryDirectory + QLatin1Char('/') + file.fileName()));
        }
        startServer(retryDirectory);
        QVERIFY(!call(QStringLiteral("init")).toBool());
        QVERIFY(!call(QStringLiteral("init")).toBool());
        QVERIFY(QFile::link(dataDirectory + QStringLiteral("/full-pinyin.txt"),
                            retryDirectory + QStringLiteral("/full-pinyin.txt")));
        QVERIFY(call(QStringLiteral("init")).toBool());
        QVERIFY(call(QStringLiteral("search"), {QStringLiteral("nihao")}).toInt() > 0);
        server.terminate();
        QVERIFY(server.waitForFinished(5000));
        startServer();
        QVERIFY(call(QStringLiteral("init")).toBool());
    }

    void migratedSingleCharacterEditing()
    {
        server.terminate();
        QVERIFY(server.waitForFinished(5000));
        legacyBytes = legacyDictionary(spellings, true, true, QString::fromUtf8("黁"),
                                        {QStringLiteral("nun")});
        QFile legacyFile(legacyPath);
        QVERIFY(legacyFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(legacyFile.write(legacyBytes), qint64(legacyBytes.size()));
        legacyFile.close();
        startServer();
        QVERIFY(call(QStringLiteral("init")).toBool());
        QCOMPARE(call(QStringLiteral("updateUserPhrase"),
                      {QString::fromUtf8("黁"), QStringLiteral("nun"),
                       QString::fromUtf8("黁"), QStringLiteral("nve")}).toString(), QString());
        QVERIFY(call(QStringLiteral("userDictionaryEntryList")).toStringList()
                .contains(QString::fromUtf8("黁\tnue")));
        QCOMPARE(call(QStringLiteral("removeUserPhrase"),
                      {QString::fromUtf8("黁"), QStringLiteral("nve")}).toString(), QString());
        QVERIFY(!call(QStringLiteral("addUserPhrase"),
                      {QString::fromUtf8("黁"), QStringLiteral("nun")}).toString().isEmpty());
    }

    void partialChoiceAndBackspace()
    {
        call(QStringLiteral("resetSearch"));
        QVERIFY(call(QStringLiteral("search"), {QStringLiteral("nihaoma")}).toInt() > 0);
        const int firstCharacter = candidateIndex(QString::fromUtf8("你"));
        QVERIFY(firstCharacter >= 0);
        QVERIFY(call(QStringLiteral("chooceCandidate"), {firstCharacter}).toInt() > 0);
        QVERIFY(call(QStringLiteral("takeCommit")).toString().isEmpty());
        QVERIFY(call(QStringLiteral("preeditText")).toString().startsWith(QString::fromUtf8("你")));
        QVERIFY(call(QStringLiteral("backspace")).toInt() > 0);
        QVERIFY(!call(QStringLiteral("preeditText")).toString().startsWith(QString::fromUtf8("你")));
        QCOMPARE(call(QStringLiteral("pinyinString"), {false}).toString(), QStringLiteral("nihaoma"));
        const int restored = candidateIndex(QString::fromUtf8("你"));
        QVERIFY(restored >= 0);
        call(QStringLiteral("chooceCandidate"), {restored});
        const QString committed = call(QStringLiteral("commitComposition"), {false}).toString();
        QVERIFY(committed.startsWith(QString::fromUtf8("你")));
        QVERIFY(!committed.contains(QStringLiteral("hao")));
    }

    void apostropheUmlautAndRawCommit()
    {
        call(QStringLiteral("resetSearch"));
        call(QStringLiteral("search"), {QStringLiteral("xi'an")});
        QVERIFY(candidateIndex(QString::fromUtf8("西安")) >= 0);
        call(QStringLiteral("resetSearch"));
        call(QStringLiteral("search"), {QStringLiteral("lvse")});
        QVERIFY(candidateIndex(QString::fromUtf8("绿色")) >= 0);
        call(QStringLiteral("resetSearch"));
        call(QStringLiteral("search"), {QStringLiteral("hello")});
        QCOMPARE(call(QStringLiteral("commitComposition"), {true}).toString(), QStringLiteral("hello"));
        const QStringList predictions = call(QStringLiteral("predictionList"), {QString::fromUtf8("你"), 15}).toStringList();
        QVERIFY(!predictions.isEmpty());
        QVERIFY(predictions.size() <= 15);
        QCOMPARE(predictions.toSet().size(), predictions.size());
    }

    void dictionaryEditingAndPersistence()
    {
        const QString original = QString::fromUtf8("墨羽输入法");
        const QString replacement = QString::fromUtf8("墨羽键盘");
        QCOMPARE(call(QStringLiteral("addUserPhrase"), {original, QStringLiteral("mo yu shu ru fa")}).toString(), QString());
        const int before = call(QStringLiteral("userDictionaryEntryCount")).toInt();
        QVERIFY(!call(QStringLiteral("updateUserPhrase"),
                      {original, QStringLiteral("mo yu shu ru fa"), replacement, QStringLiteral("mo")}).toString().isEmpty());
        QCOMPARE(call(QStringLiteral("userDictionaryEntryCount")).toInt(), before);
        QCOMPARE(call(QStringLiteral("updateUserPhrase"),
                      {original, QStringLiteral("mo yu shu ru fa"), replacement, QStringLiteral("moyujianpan")}).toString(), QString());
        QStringList entries = call(QStringLiteral("userDictionaryEntryList")).toStringList();
        QVERIFY(entries.contains(replacement + QStringLiteral("\tmo yu jian pan")));
        QVERIFY(!entries.contains(original + QStringLiteral("\tmo yu shu ru fa")));
        const QStringList page = call(QStringLiteral("userDictionaryEntryPage"), {0, 2}).toStringList();
        QVERIFY(page.first().startsWith(QStringLiteral("@next:2:")));
        QCOMPARE(page.size(), 3);

        server.terminate();
        QVERIFY(server.waitForFinished(5000));
        startServer();
        QVERIFY(call(QStringLiteral("init")).toBool());
        QCOMPARE(call(QStringLiteral("userDictionaryEntryList")).toStringList(), entries);
        QFile legacyFile(legacyPath);
        QVERIFY(legacyFile.open(QIODevice::ReadOnly));
        QCOMPARE(legacyFile.readAll(), legacyBytes);
    }

    void concurrentDictionaryClients()
    {
        const QStringList words = {QString::fromUtf8("墨羽甲"), QString::fromUtf8("墨羽乙"), QString::fromUtf8("墨羽丙")};
        const QStringList endings = {QStringLiteral("jia"), QStringLiteral("yi"), QStringLiteral("bing")};
        std::mutex mutex;
        QStringList errors;
        std::vector<std::thread> threads;
        for (int index = 0; index < words.size(); ++index) {
            threads.emplace_back([&, index]() {
                const QString connectionName = QStringLiteral("dictionary-%1").arg(index);
                QDBusConnection connection = QDBusConnection::connectToBus(
                            QString::fromLocal8Bit(qgetenv("DBUS_SESSION_BUS_ADDRESS")), connectionName);
                QDBusMessage request = QDBusMessage::createMethodCall(serviceName, QStringLiteral("/Rime"),
                                            serviceName, QStringLiteral("addUserPhrase"));
                request.setArguments({words[index], QStringLiteral("mo yu ") + endings[index]});
                const QDBusMessage reply = connection.call(request, QDBus::Block, 10000);
                if (reply.type() == QDBusMessage::ErrorMessage || !reply.arguments().value(0).toString().isEmpty()) {
                    std::lock_guard<std::mutex> lock(mutex);
                    errors.append(reply.errorMessage() + reply.arguments().value(0).toString());
                }
                QDBusConnection::disconnectFromBus(connectionName);
            });
        }
        for (std::thread &thread : threads)
            thread.join();
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
        const QStringList entries = call(QStringLiteral("userDictionaryEntryList")).toStringList();
        for (int index = 0; index < words.size(); ++index)
            QVERIFY(entries.contains(words[index] + QStringLiteral("\tmo yu ") + endings[index]));
    }

    void userDictionaryToggle()
    {
        const QString phrase = QString::fromUtf8("墨羽缓存");
        const QString spelling = QStringLiteral("ce shi ce shi");
        const QString predictionPhrase = QString::fromUtf8("你好缓存");
        const QString predictionSpelling = QStringLiteral("ni hao huan cun");
        QCOMPARE(call(QStringLiteral("addUserPhrase"), {phrase, spelling}).toString(), QString());
        QCOMPARE(call(QStringLiteral("addUserPhrase"), {predictionPhrase, predictionSpelling}).toString(), QString());
        QVERIFY(call(QStringLiteral("predictionList"), {QString::fromUtf8("你"), 100}).toStringList()
                .contains(QString::fromUtf8("好缓存")));
        call(QStringLiteral("resetSearch"));
        QVERIFY(call(QStringLiteral("search"), {QStringLiteral("ceshiceshi")}).toInt() > 0);
        QVERIFY(candidateIndex(phrase) >= 0);
        const QStringList originalEntries = call(QStringLiteral("userDictionaryEntryList")).toStringList();

        call(QStringLiteral("setUserDictionary"), {false});
        call(QStringLiteral("resetSearch"));
        QVERIFY(call(QStringLiteral("search"), {QStringLiteral("ceshiceshi")}).toInt() > 0);
        QVERIFY(candidateIndex(phrase) < 0);
        QVERIFY(call(QStringLiteral("chooceCandidate"), {0}).toInt() >= 0);
        QVERIFY(!call(QStringLiteral("takeCommit")).toString().isEmpty());
        const QStringList disabledEntries = call(QStringLiteral("userDictionaryEntryList")).toStringList();
        if (disabledEntries != originalEntries) {
            QStringList learnedEntries = disabledEntries;
            for (const QString &entry : originalEntries)
                learnedEntries.removeAll(entry);
            qWarning().noquote() << "Entries learned while disabled:" << learnedEntries.join(QLatin1Char('\n'));
        }
        QCOMPARE(disabledEntries, originalEntries);
        QVERIFY(!call(QStringLiteral("predictionList"), {QString::fromUtf8("你"), 100}).toStringList()
                .contains(QString::fromUtf8("好缓存")));
        call(QStringLiteral("resetSearch"));
        QVERIFY(call(QStringLiteral("search"), {QStringLiteral("ceshiceshi")}).toInt() > 0);
        QVERIFY(!call(QStringLiteral("commitComposition"), {false}).toString().isEmpty());
        QCOMPARE(call(QStringLiteral("userDictionaryEntryList")).toStringList(), originalEntries);

        const QString temporaryPhrase = QString::fromUtf8("墨羽管理");
        const QString temporarySpelling = QStringLiteral("mo yu guan li");
        const QString updatedPhrase = QString::fromUtf8("墨羽词库");
        const QString updatedSpelling = QStringLiteral("mo yu ci ku");
        QCOMPARE(call(QStringLiteral("addUserPhrase"), {temporaryPhrase, temporarySpelling}).toString(), QString());
        QCOMPARE(call(QStringLiteral("updateUserPhrase"),
                      {temporaryPhrase, temporarySpelling, updatedPhrase, updatedSpelling}).toString(), QString());
        QCOMPARE(call(QStringLiteral("removeUserPhrase"), {updatedPhrase, updatedSpelling}).toString(), QString());
        QCOMPARE(call(QStringLiteral("userDictionaryEntryList")).toStringList(), originalEntries);

        call(QStringLiteral("setUserDictionary"), {true});
        call(QStringLiteral("resetSearch"));
        QVERIFY(call(QStringLiteral("search"), {QStringLiteral("ceshiceshi")}).toInt() > 0);
        QVERIFY(candidateIndex(phrase) >= 0);
        QCOMPARE(call(QStringLiteral("removeUserPhrase"), {phrase, spelling}).toString(), QString());
        QCOMPARE(call(QStringLiteral("removeUserPhrase"), {predictionPhrase, predictionSpelling}).toString(), QString());
        for (int count = 1; count <= 5; ++count) {
            call(QStringLiteral("resetSearch"));
            QVERIFY(call(QStringLiteral("search"), {QStringLiteral("ceshiceshi")}).toInt() > 0);
            QVERIFY(call(QStringLiteral("chooceCandidate"), {0}).toInt() >= 0);
            const QString learnedText = call(QStringLiteral("takeCommit")).toString();
            QVERIFY(!learnedText.isEmpty());
            QCOMPARE(call(QStringLiteral("userDictionaryEntryList")).toStringList()
                     .contains(learnedText + QLatin1Char('\t') + spelling), count == 5);
        }
        call(QStringLiteral("resetSearch"));
    }

    void automaticLearningThreshold()
    {
        const QString input = QStringLiteral("xuexijishu");
        const QString spelling = QStringLiteral("xue xi ji shu");
        call(QStringLiteral("resetSearch"));
        QVERIFY(call(QStringLiteral("search"), {input}).toInt() > 0);
        const QString phrase = call(QStringLiteral("candidateAt"), {0}).toString();
        const QString row = phrase + QLatin1Char('\t') + spelling;
        QVERIFY(!call(QStringLiteral("userDictionaryEntryList")).toStringList().contains(row));
        for (int count = 1; count <= 4; ++count) {
            call(QStringLiteral("resetSearch"));
            QVERIFY(call(QStringLiteral("search"), {input}).toInt() > 0);
            QVERIFY(call(QStringLiteral("chooceCandidate"), {candidateIndex(phrase)}).toInt() >= 0);
            QCOMPARE(call(QStringLiteral("takeCommit")).toString(), phrase);
            QVERIFY(!call(QStringLiteral("userDictionaryEntryList")).toStringList().contains(row));
            if (count == 2) {
                server.terminate();
                QVERIFY(server.waitForFinished(5000));
                startServer();
                QVERIFY(call(QStringLiteral("init")).toBool());
                QVERIFY(!call(QStringLiteral("userDictionaryEntryList")).toStringList().contains(row));
            }
        }
        for (int count = 0; count < 5; ++count) {
            call(QStringLiteral("resetSearch"));
            QVERIFY(call(QStringLiteral("search"), {input}).toInt() > 0);
            call(QStringLiteral("resetSearch"));
            QVERIFY(call(QStringLiteral("search"), {input}).toInt() > 0);
            QCOMPARE(call(QStringLiteral("commitComposition"), {true}).toString(), input);
            QVERIFY(!call(QStringLiteral("userDictionaryEntryList")).toStringList().contains(row));
        }
        call(QStringLiteral("setUserDictionary"), {false});
        for (int count = 0; count < 5; ++count) {
            call(QStringLiteral("resetSearch"));
            QVERIFY(call(QStringLiteral("search"), {input}).toInt() > 0);
            QVERIFY(call(QStringLiteral("chooceCandidate"), {candidateIndex(phrase)}).toInt() >= 0);
            QCOMPARE(call(QStringLiteral("takeCommit")).toString(), phrase);
        }
        call(QStringLiteral("setUserDictionary"), {true});
        QVERIFY(!call(QStringLiteral("userDictionaryEntryList")).toStringList().contains(row));
        call(QStringLiteral("resetSearch"));
        QVERIFY(call(QStringLiteral("search"), {input}).toInt() > 0);
        QVERIFY(call(QStringLiteral("chooceCandidate"), {candidateIndex(phrase)}).toInt() >= 0);
        QCOMPARE(call(QStringLiteral("takeCommit")).toString(), phrase);
        QVERIFY(call(QStringLiteral("userDictionaryEntryList")).toStringList().contains(row));
        QCOMPARE(call(QStringLiteral("removeUserPhrase"), {phrase, spelling}).toString(), QString());
        for (int count = 1; count <= 5; ++count) {
            call(QStringLiteral("resetSearch"));
            QVERIFY(call(QStringLiteral("search"), {input}).toInt() > 0);
            QVERIFY(call(QStringLiteral("chooceCandidate"), {candidateIndex(phrase)}).toInt() >= 0);
            QCOMPARE(call(QStringLiteral("takeCommit")).toString(), phrase);
            QCOMPARE(call(QStringLiteral("userDictionaryEntryList")).toStringList().contains(row), count == 5);
        }
        QCOMPARE(call(QStringLiteral("removeUserPhrase"), {phrase, spelling}).toString(), QString());
        call(QStringLiteral("resetSearch"));
        QVERIFY(call(QStringLiteral("search"), {input}).toInt() > 0);
        QVERIFY(call(QStringLiteral("chooceCandidate"), {candidateIndex(phrase)}).toInt() >= 0);
        QCOMPARE(call(QStringLiteral("takeCommit")).toString(), phrase);
        QVERIFY(!call(QStringLiteral("userDictionaryEntryList")).toStringList().contains(row));
        QVERIFY(!call(QStringLiteral("removeUserPhrase"), {phrase, spelling}).toString().isEmpty());
        QCOMPARE(call(QStringLiteral("addUserPhrase"), {phrase, spelling}).toString(), QString());
        QVERIFY(call(QStringLiteral("userDictionaryEntryList")).toStringList().contains(row));
        QCOMPARE(call(QStringLiteral("removeUserPhrase"), {phrase, spelling}).toString(), QString());
    }

    void compositionLearningThreshold()
    {
        const QString input = QStringLiteral("cipinxuexi");
        const QString spelling = QStringLiteral("ci pin xue xi");
        call(QStringLiteral("resetSearch"));
        QVERIFY(call(QStringLiteral("search"), {input}).toInt() > 0);
        const QString phrase = call(QStringLiteral("candidateAt"), {0}).toString();
        const QString row = phrase + QLatin1Char('\t') + spelling;
        QVERIFY(!call(QStringLiteral("userDictionaryEntryList")).toStringList().contains(row));
        for (int count = 1; count <= 5; ++count) {
            call(QStringLiteral("resetSearch"));
            QVERIFY(call(QStringLiteral("search"), {input}).toInt() > 0);
            QCOMPARE(call(QStringLiteral("commitComposition"), {false}).toString(), phrase);
            QCOMPARE(call(QStringLiteral("userDictionaryEntryList")).toStringList().contains(row), count == 5);
        }
        QCOMPARE(call(QStringLiteral("removeUserPhrase"), {phrase, spelling}).toString(), QString());
    }

    void resetDoesNotReimportLegacy()
    {
        const QString input = QStringLiteral("qingkongjishu");
        for (int count = 0; count < 4; ++count) {
            call(QStringLiteral("resetSearch"));
            QVERIFY(call(QStringLiteral("search"), {input}).toInt() > 0);
            QVERIFY(!call(QStringLiteral("commitComposition"), {false}).toString().isEmpty());
        }
        QVERIFY(call(QStringLiteral("resetUserDictionary")).toBool());
        QCOMPARE(call(QStringLiteral("userDictionaryEntryCount")).toInt(), 0);
        QCOMPARE(call(QStringLiteral("predictionList"), {QString(), 16}).toStringList(), commonPredictions);
        server.terminate();
        QVERIFY(server.waitForFinished(5000));
        startServer();
        QVERIFY(call(QStringLiteral("init")).toBool());
        QCOMPARE(call(QStringLiteral("userDictionaryEntryCount")).toInt(), 0);
        QCOMPARE(call(QStringLiteral("predictionList"), {QString(), 16}).toStringList(), commonPredictions);
        QVERIFY(call(QStringLiteral("search"), {input}).toInt() > 0);
        QVERIFY(!call(QStringLiteral("commitComposition"), {false}).toString().isEmpty());
        QCOMPARE(call(QStringLiteral("userDictionaryEntryCount")).toInt(), 0);
        QVERIFY(call(QStringLiteral("resetUserDictionary")).toBool());
    }

    void installedQmlPredictions_data()
    {
        QTest::addColumn<QString>("handlerPath");
        QTest::newRow("main-directory")
                << QStringLiteral("/usr/share/maliit/plugins/com/jolla/MoyuPinyinHandler.qml");
        QTest::newRow("handlers-directory")
                << QStringLiteral("/usr/share/maliit/plugins/com/jolla/handlers/MoyuPinyinHandler.qml");
    }

    void installedQmlPredictions()
    {
        QFETCH(QString, handlerPath);
        QQmlEngine engine;
        const QVariantMap model = {{QStringLiteral("type"), QStringLiteral("china_pinyin")},
                                   {QStringLiteral("handler"), QStringLiteral("MoyuPinyinHandler.qml")},
                                   {QStringLiteral("languageCode"), QString::fromUtf8("墨羽拼音")},
                                   {QStringLiteral("index"), 0}};
        engine.rootContext()->setContextProperty(QStringLiteral("model"), model);
        engine.rootContext()->setContextProperty(QStringLiteral("testHandlerUrl"),
                    QUrl::fromLocalFile(handlerPath));
        engine.rootContext()->setContextProperty(QStringLiteral("testLayoutUrl"),
                    QUrl::fromLocalFile(QStringLiteral("/usr/share/maliit/plugins/com/jolla/layouts/zh_cn_moyu.qml")));
        const QString harness = QString::fromLocal8Bit(qgetenv("MOYU_RIME_TEST_HARNESS"));
        QVERIFY(!harness.isEmpty());
        QQmlComponent component(&engine, QUrl::fromLocalFile(harness));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QQuickRenderControl renderControl;
        QQuickWindow window(&renderControl);
        window.setGeometry(0, 0, 1080, 2340);
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        QQuickItem *rootItem = qobject_cast<QQuickItem *>(root.data());
        QVERIFY(rootItem);
        rootItem->setParentItem(window.contentItem());
        QObject *inputMethod = root->property("inputMethod").value<QObject *>();
        QVERIFY(inputMethod);
        engine.rootContext()->setContextProperty(QStringLiteral("MInputMethodQuick"), inputMethod);
        engine.rootContext()->setContextProperty(QStringLiteral("Clipboard"), root->property("clipboard"));
        inputMethod->setProperty("predictionEnabled", false);
        root->setProperty("ready", true);
        QTRY_VERIFY(root->property("loadedLayout").value<QObject *>());
        QObject *handler = root->property("handler").value<QObject *>();
        QVERIFY(handler);
        QObject *decoder = nullptr;
        for (QObject *child : handler->findChildren<QObject *>()) {
            if (child->property("moreCandidates").isValid()) {
                decoder = child;
                break;
            }
        }
        QVERIFY(decoder);
        QVERIFY(decoder->property("inProcess").toBool());
        QTRY_VERIFY(!candidateTexts(decoder).isEmpty());
        QStringList initialCandidates = commonPredictions;
        initialCandidates.append(QStringLiteral(" "));
        QCOMPARE(candidateTexts(decoder), initialCandidates);
        QQuickItem *candidateList = nullptr;
        for (QQuickItem *item : root->findChildren<QQuickItem *>()) {
            if (item->property("model").value<QObject *>() == decoder->property("candidates").value<QObject *>()
                    && item->property("orientation").toInt() == Qt::Horizontal) {
                candidateList = item;
                break;
            }
        }
        QVERIFY(candidateList);
        QTRY_VERIFY(candidateList->isVisible() && candidateList->width() > 0 && candidateList->height() > 0);
        QTRY_COMPARE(candidateList->property("count").toInt(), initialCandidates.size());
        QTRY_VERIFY(candidateList->property("currentItem").value<QQuickItem *>());
        QQuickItem *firstCandidate = candidateList->property("currentItem").value<QQuickItem *>();
        QVERIFY(firstCandidate->isVisible() && firstCandidate->width() > 0 && firstCandidate->height() > 0);
        setContext(root.data(), QString::fromUtf8("光标右边的文字"), 0);
        QTRY_COMPARE(candidateTexts(decoder), initialCandidates);
        setContext(root.data(), QString(), 0);

        click(root.data(), QStringLiteral("ni"), Qt::Key_unknown);
        QVERIFY(candidateTexts(decoder).contains(QString::fromUtf8("你")));
        const int character = candidateTexts(decoder).indexOf(QString::fromUtf8("你"));
        QVERIFY(QMetaObject::invokeMethod(handler, "accept", Q_ARG(QVariant, character)));
        QCOMPARE(inputMethod->property("committedText").toString(), QString::fromUtf8("你"));
        const QStringList backendPredictions = call(QStringLiteral("predictionList"),
                                                    {QString::fromUtf8("你"), 15}).toStringList();
        QVERIFY(!backendPredictions.isEmpty());
        QStringList expectedCandidates = backendPredictions;
        expectedCandidates.append(QStringLiteral(" "));
        QCOMPARE(candidateTexts(decoder), expectedCandidates);
        QVERIFY(QMetaObject::invokeMethod(handler, "accept", Q_ARG(QVariant, 0)));
        QCOMPARE(inputMethod->property("committedText").toString(),
                 QString::fromUtf8("你") + backendPredictions.first());
        setContext(root.data(), QString::fromUtf8("你好"), 2);
        QTRY_VERIFY(candidateTexts(decoder).contains(QString::fromUtf8("啊")));

        const QString groupContinuation = call(QStringLiteral("predictionList"),
                                               {QString::fromUtf8("我们"), 15}).toStringList().value(0);
        const QString greetingContinuation = call(QStringLiteral("predictionList"),
                                                  {QString::fromUtf8("你好"), 15}).toStringList().value(0);
        QVERIFY(!groupContinuation.isEmpty());
        QVERIFY(!greetingContinuation.isEmpty());

        setContext(root.data(), QString::fromUtf8("你好，我们"), 5);
        QTRY_COMPARE(candidateTexts(decoder).value(0), groupContinuation);
        inputMethod->setProperty("cursorPosition", 2);
        QTRY_COMPARE(candidateTexts(decoder).value(0), greetingContinuation);
        inputMethod->setProperty("surroundingText", QString::fromUtf8("我们，你好"));
        QTRY_COMPARE(candidateTexts(decoder).value(0), groupContinuation);
        const int pair = candidateTexts(decoder).indexOf(groupContinuation);
        QVERIFY(QMetaObject::invokeMethod(handler, "accept", Q_ARG(QVariant, pair)));
        QCOMPARE(inputMethod->property("surroundingText").toString(),
                 QString::fromUtf8("我们") + groupContinuation + QString::fromUtf8("，你好"));

        setContext(root.data(), QString(), 0);
        inputMethod->setProperty("contextEchoEnabled", false);
        click(root.data(), QStringLiteral("nihao"), Qt::Key_unknown);
        click(root.data(), QString(), Qt::Key_Space);
        QVERIFY(candidateTexts(decoder).contains(QString::fromUtf8("啊")));
        QTest::qWait(100);
        QVERIFY(candidateTexts(decoder).contains(QString::fromUtf8("啊")));
        inputMethod->setProperty("surroundingText", inputMethod->property("pendingSurroundingText"));
        QTest::qWait(100);
        QVERIFY(candidateTexts(decoder).contains(QString::fromUtf8("啊")));
        const int exclamation = candidateTexts(decoder).indexOf(QString::fromUtf8("啊"));
        QVERIFY(QMetaObject::invokeMethod(handler, "accept", Q_ARG(QVariant, exclamation)));
        QCOMPARE(inputMethod->property("committedText").toString(), QString::fromUtf8("你好啊"));
        QVERIFY(QMetaObject::invokeMethod(inputMethod, "flushContext"));
        inputMethod->setProperty("contextEchoEnabled", true);
        QTest::qWait(100);
        QCOMPARE(inputMethod->property("surroundingText").toString(), QString::fromUtf8("你好啊"));
        QVERIFY(!candidateTexts(decoder).isEmpty());

        inputMethod->setProperty("surroundingTextValid", false);
        setContext(root.data(), QString::fromUtf8("我们"), 2);
        QTRY_COMPARE(candidateTexts(decoder), initialCandidates);
        inputMethod->setProperty("surroundingTextValid", true);
        QTRY_COMPARE(candidateTexts(decoder).value(0), groupContinuation);

        click(root.data(), QStringLiteral("ceshi"), Qt::Key_unknown);
        const QStringList composition = candidateTexts(decoder);
        QVERIFY(composition.contains(QString::fromUtf8("测试")));
        inputMethod->setProperty("surroundingText", QString::fromUtf8("你好"));
        QTest::qWait(100);
        QCOMPARE(candidateTexts(decoder), composition);
        click(root.data(), QString(), Qt::Key_Space);

        handler->setProperty("pinyinMode", false);
        QTRY_VERIFY(candidateTexts(decoder).isEmpty());
        handler->setProperty("pinyinMode", true);
        QTRY_VERIFY(!candidateTexts(decoder).isEmpty());
        root->setProperty("symbolMode", true);
        QTRY_VERIFY(candidateTexts(decoder).isEmpty());
        root->setProperty("symbolMode", false);
        QTRY_VERIFY(!candidateTexts(decoder).isEmpty());
        inputMethod->setProperty("hiddenText", true);
        QTRY_VERIFY(candidateTexts(decoder).isEmpty());
        inputMethod->setProperty("hiddenText", false);
        QTRY_VERIFY(!candidateTexts(decoder).isEmpty());
        const QStringList chinesePredictions = candidateTexts(decoder);
        inputMethod->setProperty("predictionEnabled", true);
        QTest::qWait(100);
        QCOMPARE(candidateTexts(decoder), chinesePredictions);
        inputMethod->setProperty("predictionEnabled", false);
        QTest::qWait(100);
        QCOMPARE(candidateTexts(decoder), chinesePredictions);
        handler->setProperty("active", false);
        QTRY_VERIFY(candidateTexts(decoder).isEmpty());
        handler->setProperty("active", true);
        QTRY_VERIFY(!candidateTexts(decoder).isEmpty());
        setContext(root.data(), QString(), 0);
        QTRY_COMPARE(candidateTexts(decoder), initialCandidates);
        click(root.data(), QStringLiteral("n"), Qt::Key_unknown);
        click(root.data(), QString(), Qt::Key_Backspace);
        QTRY_COMPARE(candidateTexts(decoder), initialCandidates);
        QVERIFY(QMetaObject::invokeMethod(handler, "_reset"));
        QTRY_COMPARE(candidateTexts(decoder), initialCandidates);
        setContext(root.data(), QString::fromUtf8("我们好"), 3);
        click(root.data(), QString(), Qt::Key_Backspace);
        QTRY_COMPARE(candidateTexts(decoder).value(0), groupContinuation);

        // Maliit getters read the latest host state, including state changed by
        // a commit before the cached property notifications are emitted.
        QTest::qWait(100);
        {
            QSignalBlocker blocker(inputMethod);
            inputMethod->setProperty("surroundingText", QString::fromUtf8("你好"));
            inputMethod->setProperty("cursorPosition", 2);
        }
        QVERIFY(QMetaObject::invokeMethod(inputMethod, "editorStateUpdate"));
        QTRY_COMPARE(candidateTexts(decoder).value(0), greetingContinuation);

        setContext(root.data(), QString::fromUtf8("我们"), 2);
        QTRY_COMPARE(candidateTexts(decoder).value(0), groupContinuation);
        {
            QSignalBlocker blocker(inputMethod);
            inputMethod->setProperty("surroundingText", QString::fromUtf8("你好"));
        }
        QVERIFY(QMetaObject::invokeMethod(inputMethod, "cursorPositionChanged"));
        QTRY_COMPARE(candidateTexts(decoder).value(0), greetingContinuation);

        // The input handler stays selected while the keyboard is hidden.
        inputMethod->setProperty("active", false);
        {
            QSignalBlocker blocker(inputMethod);
            inputMethod->setProperty("surroundingText", QString());
            inputMethod->setProperty("cursorPosition", 0);
        }
        inputMethod->setProperty("active", true);
        QTRY_COMPARE(candidateTexts(decoder), initialCandidates);

        inputMethod->setProperty("contextEchoEnabled", false);
        QVERIFY(QMetaObject::invokeMethod(handler, "accept", Q_ARG(QVariant, 0)));
        QVERIFY(handler->property("awaitingCommitContext").toBool());
        QVERIFY(QMetaObject::invokeMethod(inputMethod, "focusTargetChanged", Q_ARG(bool, true)));
        QTRY_COMPARE(candidateTexts(decoder), initialCandidates);
        inputMethod->setProperty("contextPending", false);

        QVERIFY(QMetaObject::invokeMethod(handler, "accept", Q_ARG(QVariant, 0)));
        QVERIFY(handler->property("awaitingCommitContext").toBool());
        QVERIFY(QMetaObject::invokeMethod(inputMethod, "inputMethodReset"));
        QTRY_COMPARE(candidateTexts(decoder), initialCandidates);
        inputMethod->setProperty("contextPending", false);

        // A completed update reports the client's cursor position, even when
        // it equals the insertion point from before the commit.
        setContext(root.data(), QString::fromUtf8("你好"), 2);
        QTRY_COMPARE(candidateTexts(decoder).value(0), greetingContinuation);
        QVERIFY(QMetaObject::invokeMethod(handler, "accept", Q_ARG(QVariant, 0)));
        {
            QSignalBlocker blocker(inputMethod);
            inputMethod->setProperty("surroundingText", inputMethod->property("pendingSurroundingText"));
        }
        QVERIFY(QMetaObject::invokeMethod(inputMethod, "editorStateUpdate"));
        QTRY_COMPARE(candidateTexts(decoder).value(0), greetingContinuation);
        QVERIFY(!handler->property("awaitingCommitContext").toBool());
    }

    void installedQmlKeyboard_data()
    {
        installedQmlPredictions_data();
    }

    void installedQmlKeyboard()
    {
        QFETCH(QString, handlerPath);
        QQmlEngine engine;
        const QVariantMap model = {{QStringLiteral("type"), QStringLiteral("china_pinyin")},
                                   {QStringLiteral("handler"), QStringLiteral("MoyuPinyinHandler.qml")},
                                   {QStringLiteral("languageCode"), QString::fromUtf8("墨羽拼音")},
                                   {QStringLiteral("index"), 0}};
        engine.rootContext()->setContextProperty(QStringLiteral("model"), model);
        engine.rootContext()->setContextProperty(QStringLiteral("testHandlerUrl"),
                    QUrl::fromLocalFile(handlerPath));
        engine.rootContext()->setContextProperty(QStringLiteral("testLayoutUrl"),
                    QUrl::fromLocalFile(QStringLiteral("/usr/share/maliit/plugins/com/jolla/layouts/zh_cn_moyu.qml")));
        const QString harness = QString::fromLocal8Bit(qgetenv("MOYU_RIME_TEST_HARNESS"));
        QVERIFY(!harness.isEmpty());
        QQmlComponent component(&engine, QUrl::fromLocalFile(harness));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QQuickRenderControl renderControl;
        QQuickWindow window(&renderControl);
        window.setGeometry(0, 0, 1080, 2340);
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        QQuickItem *rootItem = qobject_cast<QQuickItem *>(root.data());
        QVERIFY(rootItem);
        rootItem->setParentItem(window.contentItem());
        QObject *inputMethod = root->property("inputMethod").value<QObject *>();
        QObject *clipboard = root->property("clipboard").value<QObject *>();
        QVERIFY(inputMethod);
        QVERIFY(clipboard);
        const QString savedClipboardText = clipboard->property("text").toString();
        QObject clipboardRestore;
        connect(&clipboardRestore, &QObject::destroyed, clipboard, [clipboard, savedClipboardText]() {
            clipboard->setProperty("text", savedClipboardText);
        });
        engine.rootContext()->setContextProperty(QStringLiteral("MInputMethodQuick"), inputMethod);
        engine.rootContext()->setContextProperty(QStringLiteral("Clipboard"), clipboard);
        root->setProperty("ready", true);
        QTRY_VERIFY(root->property("loadedLayout").value<QObject *>());
        QObject *layout = root->property("loadedLayout").value<QObject *>();
        const auto snapshot = [&]() {
            QCoreApplication::processEvents();
            QMetaObject::invokeMethod(layout, "updateSizes");
            renderControl.polishItems();
            return invoke(root.data(), "snapshot").toString();
        };
        QObject *handler = root->property("handler").value<QObject *>();
        QVERIFY(handler);
        QObject *decoder = nullptr;
        for (QObject *child : handler->findChildren<QObject *>()) {
            if (child->property("moreCandidates").isValid()) {
                decoder = child;
                break;
            }
        }
        QVERIFY(decoder);
        QVERIFY(decoder->property("inProcess").toBool());
        QTRY_VERIFY(!candidateTexts(decoder).isEmpty());
        QTRY_VERIFY(!layout->property("customRowsActive").toBool());
        const QString portrait = snapshot();
        const QJsonArray portraitRows = QJsonDocument::fromJson(portrait.toUtf8()).array();
        QCOMPARE(portraitRows.size(), 4);
        const QList<int> keyCounts = {10, 9, 9, 5};
        double previousBottom = 0;
        for (int index = 0; index < portraitRows.size(); ++index) {
            const QJsonObject row = portraitRows[index].toObject();
            QCOMPARE(row.value(QStringLiteral("width")).toDouble(), 1080.0);
            QCOMPARE(row.value(QStringLiteral("height")).toDouble(), 180.0);
            QVERIFY(row.value(QStringLiteral("y")).toDouble() >= previousBottom);
            previousBottom = row.value(QStringLiteral("y")).toDouble() + row.value(QStringLiteral("height")).toDouble();
            const QJsonArray keys = row.value(QStringLiteral("keys")).toArray();
            QCOMPARE(keys.size(), keyCounts[index]);
            for (const QJsonValue &value : keys) {
                const QJsonObject key = value.toObject();
                QVERIFY(key.value(QStringLiteral("width")).toDouble() > 0);
                QCOMPARE(key.value(QStringLiteral("height")).toDouble(), 180.0);
            }
        }
        QCOMPARE(portraitRows[0].toObject().value(QStringLiteral("keys")).toArray()[0]
                 .toObject().value(QStringLiteral("caption")).toString(), QStringLiteral("q"));
        const QString defaultDefinition = invoke(root.data(), "defaultDefinition").toString();
        root->setProperty("layoutDefinition", defaultDefinition);
        QTRY_VERIFY(layout->property("customRowsActive").toBool());
        QCOMPARE(snapshot(), portrait);

        // Exercise the native clipboard header when its system component exists,
        // and verify its absence leaves the candidate list usable otherwise.
        const auto pasteAvailable = [&engine](const QString &name) {
            QQmlComponent moduleComponent(&engine);
            moduleComponent.setData((QStringLiteral("import QtQuick 2.0\nimport com.jolla.keyboard 1.0\nComponent { ")
                                     + name.left(name.size() - 4) + QStringLiteral(" {} }")).toUtf8(), QUrl());
            if (moduleComponent.isReady())
                return true;
            const QString directory = QStringLiteral("/usr/share/maliit/plugins/com/jolla/");
            return QFileInfo::exists(directory + QStringLiteral("handlers/") + name)
                    || QFileInfo::exists(directory + name);
        };
        QQuickItem *horizontalList = nullptr;
        for (QQuickItem *item : root->findChildren<QQuickItem *>()) {
            if (item->property("model").value<QObject *>() == decoder->property("candidates").value<QObject *>()
                    && item->property("orientation").toInt() == Qt::Horizontal) {
                horizontalList = item;
                break;
            }
        }
        QVERIFY(horizontalList);
        QTRY_VERIFY(horizontalList->property("headerItem").value<QQuickItem *>());
        QQuickItem *horizontalPaste = horizontalList->property("headerItem").value<QQuickItem *>();
        const QString clipboardText = QString::fromUtf8("剪贴板汉字");
        clipboard->setProperty("text", clipboardText);
        snapshot();
        if (pasteAvailable(QStringLiteral("PasteButton.qml"))) {
            QTRY_VERIFY(horizontalPaste->property("item").value<QQuickItem *>());
            QQuickItem *button = horizontalPaste->property("item").value<QQuickItem *>();
            QTRY_VERIFY(horizontalPaste->isVisible());
            QTRY_VERIFY(horizontalPaste->width() > 0);
            QTRY_COMPARE(horizontalPaste->height(), horizontalList->height());
            QTRY_VERIFY(button->isVisible());
            setContext(root.data(), QString(), 0);
            root->setProperty("expandedPaste", true);
            QVERIFY(QMetaObject::invokeMethod(root.data(), "clickPaste",
                                              Q_ARG(QVariant, QVariant::fromValue(static_cast<QObject *>(button)))));
            QCOMPARE(inputMethod->property("committedText").toString(), clipboardText);
            QVERIFY(!root->property("expandedPaste").toBool());
            setContext(root.data(), QString(), 0);
            click(root.data(), QStringLiteral("nihao"), Qt::Key_unknown);
            QVERIFY(candidateTexts(decoder).contains(QString::fromUtf8("你好")));
            QVERIFY(QMetaObject::invokeMethod(root.data(), "clickPaste",
                                              Q_ARG(QVariant, QVariant::fromValue(static_cast<QObject *>(button)))));
            QCOMPARE(inputMethod->property("committedText").toString(), QStringLiteral("nihao") + clipboardText);
            QVERIFY(handler->property("preedit").toString().isEmpty());
        } else {
            QVERIFY(!horizontalPaste->property("item").value<QQuickItem *>());
            QVERIFY(!horizontalPaste->isVisible());
            QCOMPARE(horizontalPaste->width(), 0.0);
        }
        clipboard->setProperty("text", QString());
        QTRY_VERIFY(!horizontalPaste->isVisible());

        inputMethod->setProperty("committedText", QString());
        click(root.data(), QStringLiteral("shi"), Qt::Key_unknown);
        const int expandedIndex = decoder->property("pageSize").toInt() + 5;
        const QString expandedCandidate = call(QStringLiteral("candidateAt"), {expandedIndex}).toString();
        QVERIFY(!expandedCandidate.isEmpty());
        QVERIFY(QMetaObject::invokeMethod(decoder, "getMoreCandidates"));
        QVERIFY(QMetaObject::invokeMethod(handler, "selectPhrase", Q_ARG(QVariant, QString()),
                                          Q_ARG(QVariant, 5), Q_ARG(QVariant, true)));
        QCOMPARE(inputMethod->property("committedText").toString(), expandedCandidate);

        root->setProperty("portrait", false);
        root->setProperty("split", true);
        rootItem->setWidth(2340);
        rootItem->setHeight(1080);
        window.setGeometry(0, 0, 2340, 1080);
        QTRY_VERIFY(layout->property("splitActive").toBool());
        QTRY_VERIFY(root->property("verticalCandidates").value<QQuickItem *>());
        QQuickItem *verticalContainer = root->property("verticalCandidates").value<QQuickItem *>();
        QQuickItem *verticalList = nullptr;
        for (QQuickItem *item : verticalContainer->findChildren<QQuickItem *>()) {
            if (item->property("model").value<QObject *>() == decoder->property("candidates").value<QObject *>()
                    && item->property("orientation").toInt() == Qt::Vertical) {
                verticalList = item;
                break;
            }
        }
        QVERIFY(verticalList);
        QTRY_VERIFY(verticalList->isVisible() && verticalList->width() > 0 && verticalList->height() > 0);
        QTRY_VERIFY(verticalList->property("headerItem").value<QQuickItem *>());
        QQuickItem *verticalPaste = verticalList->property("headerItem").value<QQuickItem *>();
        clipboard->setProperty("text", clipboardText);
        snapshot();
        if (pasteAvailable(QStringLiteral("PasteButtonVertical.qml"))) {
            QTRY_VERIFY(verticalPaste->property("item").value<QQuickItem *>());
            QQuickItem *button = verticalPaste->property("item").value<QQuickItem *>();
            QTRY_VERIFY(verticalPaste->isVisible() && verticalPaste->width() > 0 && verticalPaste->height() > 0);
            QCOMPARE(button->width(), verticalList->width());
            QCOMPARE(button->property("popupParent").value<QObject *>(), static_cast<QObject *>(verticalContainer));
            QCOMPARE(button->property("popupAnchor").toInt(), 2);
            setContext(root.data(), QString(), 0);
            QVERIFY(QMetaObject::invokeMethod(root.data(), "clickPaste",
                                              Q_ARG(QVariant, QVariant::fromValue(static_cast<QObject *>(button)))));
            QCOMPARE(inputMethod->property("committedText").toString(), clipboardText);
            setContext(root.data(), QString(), 0);
            click(root.data(), QStringLiteral("nihao"), Qt::Key_unknown);
            QVERIFY(candidateTexts(decoder).contains(QString::fromUtf8("你好")));
            QVERIFY(QMetaObject::invokeMethod(root.data(), "clickPaste",
                                              Q_ARG(QVariant, QVariant::fromValue(static_cast<QObject *>(button)))));
            QCOMPARE(inputMethod->property("committedText").toString(), QStringLiteral("nihao") + clipboardText);
            QVERIFY(handler->property("preedit").toString().isEmpty());
        } else {
            QVERIFY(!verticalPaste->property("item").value<QQuickItem *>());
            QVERIFY(!verticalPaste->isVisible());
            QCOMPARE(verticalPaste->height(), 0.0);
        }
        clipboard->setProperty("text", QString());
        QTRY_VERIFY(!verticalPaste->isVisible());
        const QString landscape = snapshot();
        QCOMPARE(QJsonDocument::fromJson(landscape.toUtf8()).array().size(), 4);
        root->setProperty("layoutDefinition", QString());
        QTRY_VERIFY(!layout->property("customRowsActive").toBool());
        QCOMPARE(snapshot(), landscape);

        root->setProperty("portrait", true);
        root->setProperty("split", false);
        rootItem->setWidth(1080);
        rootItem->setHeight(2340);
        window.setGeometry(0, 0, 1080, 2340);
        const QString compact = QStringLiteral(
                    "{\"version\":1,\"rows\":[{\"keys\":[{\"type\":\"character\",\"caption\":\"ni\"},"
                    "{\"type\":\"character\",\"caption\":\"hao\"}]},{\"keys\":[{\"type\":\"space\"},"
                    "{\"type\":\"backspace\"},{\"type\":\"enter\"}]}]}");
        root->setProperty("layoutDefinition", compact);
        QTRY_VERIFY(layout->property("customRowsActive").toBool());
        QTRY_VERIFY(snapshot().contains(QStringLiteral("\"caption\":\"ni\"")));

        inputMethod->setProperty("committedText", QString());
        call(QStringLiteral("resetSearch"));
        for (const QString &caption : {QStringLiteral("ni"), QStringLiteral("hao")}) {
            QVariant handled;
            QVERIFY(QMetaObject::invokeMethod(root.data(), "clickCharacter", Q_RETURN_ARG(QVariant, handled),
                                              Q_ARG(QVariant, caption)));
            QVERIFY(handled.toBool());
        }
        click(root.data(), QString(), Qt::Key_Space);
        QCOMPARE(inputMethod->property("committedText").toString(), QString::fromUtf8("你好"));

        inputMethod->setProperty("committedText", QString());
        click(root.data(), QStringLiteral("nihao"), Qt::Key_unknown);
        click(root.data(), QString::fromUtf8("，"), Qt::Key_unknown);
        QCOMPARE(inputMethod->property("committedText").toString(), QString::fromUtf8("你好，"));

        inputMethod->setProperty("committedText", QString());
        click(root.data(), QStringLiteral("hello"), Qt::Key_unknown);
        click(root.data(), QString(), Qt::Key_Return);
        QCOMPARE(inputMethod->property("committedText").toString(), QStringLiteral("hello"));

        inputMethod->setProperty("committedText", QString());
        handler->setProperty("pinyinMode", false);
        const int resetsBefore = root->property("resetShiftCount").toInt();
        click(root.data(), QStringLiteral("Hello"), Qt::Key_unknown);
        QCOMPARE(inputMethod->property("committedText").toString(), QStringLiteral("Hello"));
        QCOMPARE(root->property("resetShiftCount").toInt(), resetsBefore + 1);
        QVERIFY(call(QStringLiteral("pinyinString"), {false}).toString().isEmpty());
        handler->setProperty("pinyinMode", true);

        inputMethod->setProperty("committedText", QString());
        root->setProperty("symbolMode", true);
        click(root.data(), QStringLiteral("@"), Qt::Key_unknown);
        QCOMPARE(inputMethod->property("committedText").toString(), QStringLiteral("@"));
        root->setProperty("symbolMode", false);

        root->setProperty("layoutDefinition", QStringLiteral("{invalid"));
        QTRY_VERIFY(!layout->property("customRowsActive").toBool());
        QCOMPARE(snapshot(), portrait);
    }

    void keyboardEngineOwnership()
    {
        QCOMPARE(QDBusConnection::sessionBus().interface()->servicePid(serviceName).value(),
                 uint(QCoreApplication::applicationPid()));
        QProcess duplicate;
        duplicate.start(QStringLiteral("/usr/bin/harbour-moyu-rime"));
        QVERIFY(duplicate.waitForStarted());
        QTRY_VERIFY(duplicate.state() == QProcess::NotRunning);
        QCOMPARE(duplicate.exitCode(), 1);
        QCOMPARE(QDBusConnection::sessionBus().interface()->servicePid(serviceName).value(),
                 uint(QCoreApplication::applicationPid()));

        const QString phrase = QString::fromUtf8("墨羽直连");
        const QString pinyin = QStringLiteral("mo yu zhi lian");
        const int count = call(QStringLiteral("userDictionaryEntryCount")).toInt();
        call(QStringLiteral("resetSearch"));
        QVERIFY(call(QStringLiteral("search"), {QStringLiteral("nihao")}).toInt() > 0);
        QProcess client;
        client.start(QStringLiteral("/usr/bin/dbus-send"), {
            QStringLiteral("--session"), QStringLiteral("--print-reply"),
            QStringLiteral("--dest=") + serviceName, QStringLiteral("/Rime"),
            serviceName + QStringLiteral(".addUserPhrase"),
            QStringLiteral("string:") + phrase, QStringLiteral("string:") + pinyin
        });
        QVERIFY(client.waitForStarted());
        QTRY_VERIFY(client.state() == QProcess::NotRunning);
        QCOMPARE(client.exitCode(), 0);
        QCOMPARE(call(QStringLiteral("userDictionaryEntryCount")).toInt(), count + 1);
        QVERIFY(call(QStringLiteral("userDictionaryEntryList")).toStringList().contains(phrase + '\t' + pinyin));
        QCOMPARE(call(QStringLiteral("pinyinString"), {false}).toString(), QStringLiteral("nihao"));
        QCOMPARE(call(QStringLiteral("removeUserPhrase"), {phrase, pinyin}).toString(), QString());
        QCOMPARE(call(QStringLiteral("userDictionaryEntryCount")).toInt(), count);
        call(QStringLiteral("resetSearch"));
    }

    void keyboardEngineRestart()
    {
        const int count = call(QStringLiteral("userDictionaryEntryCount")).toInt();
        QVERIFY(call(QStringLiteral("releaseForKeyboard")).toBool());
        QTRY_VERIFY(!QDBusConnection::sessionBus().interface()->isServiceRegistered(serviceName).value());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick 2.0\nimport xyz.birdzhang.moyu.ime 1.0\nMoyuPinyin { keyboardMode: true }", QUrl());
        QScopedPointer<QObject> decoder(component.create());
        QVERIFY2(decoder, qPrintable(component.errorString()));
        bool initialized = false;
        QVERIFY(QMetaObject::invokeMethod(decoder.data(), "init", Q_RETURN_ARG(bool, initialized)));
        QVERIFY(initialized);
        QVERIFY(decoder->property("inProcess").toBool());
        QCOMPARE(QDBusConnection::sessionBus().interface()->servicePid(serviceName).value(),
                 uint(QCoreApplication::applicationPid()));
        QCOMPARE(call(QStringLiteral("userDictionaryEntryCount")).toInt(), count);
        QVERIFY(call(QStringLiteral("search"), {QStringLiteral("nihao")}).toInt() > 0);
        QCOMPARE(call(QStringLiteral("candidateAt"), {0}).toString(), QString::fromUtf8("你好"));
        call(QStringLiteral("resetSearch"));
    }

    void installedSettingsPages()
    {
        QQmlEngine engine;
        QQmlComponent windowComponent(&engine);
        windowComponent.setData(
                    "import QtQuick 2.0\n"
                    "import Sailfish.Silica 1.0\n"
                    "ApplicationWindow {\n"
                    "    visible: false\n"
                    "    cover: null\n"
                    "    property Item openedPage: null\n"
                    "    function supported() { return typeof Clipboard.text === 'string' }\n"
                    "    function openPage(url) { openedPage = pageStack.push(url, {}, PageStackAction.Immediate) }\n"
                    "}\n", QUrl::fromLocalFile(QStringLiteral("/usr/share/harbour-moyu/qml/settings-test.qml")));
        QVERIFY2(windowComponent.isReady(), qPrintable(windowComponent.errorString()));
        QScopedPointer<QObject> applicationWindow(windowComponent.create());
        QVERIFY2(applicationWindow, qPrintable(windowComponent.errorString()));
        QVERIFY(invoke(applicationWindow.data(), "supported").toBool());

        const QStringList pages = {QStringLiteral("FirstPage.qml"), QStringLiteral("AboutPage.qml"),
                                    QStringLiteral("UserDictionaryPage.qml"), QStringLiteral("KeyboardLayoutPage.qml"),
                                    QStringLiteral("DictionaryFilesPage.qml")};
        for (const QString &name : pages) {
            const QString url = QUrl::fromLocalFile(
                    QStringLiteral("/usr/share/harbour-moyu/qml/pages/") + name).toString();
            QVERIFY(QMetaObject::invokeMethod(applicationWindow.data(), "openPage", Q_ARG(QVariant, url)));
            QTRY_VERIFY(applicationWindow->property("openedPage").value<QObject *>());
            QObject *page = applicationWindow->property("openedPage").value<QObject *>();
            QCoreApplication::processEvents();
            if (name == QStringLiteral("KeyboardLayoutPage.qml")) {
                QVERIFY(QMetaObject::invokeMethod(page, "generateLetterLayout"));
                QVERIFY(QMetaObject::invokeMethod(page, "saveLayout"));
                QVERIFY(page->property("statusMessage").toString().contains(QString::fromUtf8("已保存")));
                QVERIFY(QMetaObject::invokeMethod(page, "restoreDefault"));
            }
            if (name == QStringLiteral("DictionaryFilesPage.qml")) {
                QTRY_VERIFY(page->property("initialized").toBool());
                QVERIFY(page->property("builtInVersion").toString().contains(QStringLiteral("2026-11-11")));
            }
        }
    }

    void modernDictionaryVocabulary()
    {
        for (const auto &entry : QList<QPair<QString, QString>>{
             {QString::fromUtf8("生成式人工智能"), QStringLiteral("shengchengshirengongzhineng")},
             {QString::fromUtf8("大语言模型"), QStringLiteral("dayuyanmoxing")}}) {
            call(QStringLiteral("resetSearch"));
            QVERIFY(call(QStringLiteral("search"), {entry.second}).toInt() > 0);
            QVERIFY2(candidateIndex(entry.first) >= 0, qPrintable(entry.first));
        }
    }

    void dictionaryFileManagement()
    {
        const QString inputPath = configDirectory + QStringLiteral("/extension.txt");
        QFile input(inputPath);
        const QByteArray contents = QString::fromUtf8("墨羽词典管理\tmo yu ci dian guan li\t1000000\n").toUtf8();
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write(contents), qint64(contents.size()));
        input.close();
        const auto operation = [&](const QString &method, const QVariantList &arguments = QVariantList()) {
            return QJsonDocument::fromJson(call(method, arguments).toString().toUtf8()).object();
        };
        const QString name = QStringLiteral("extension.dict.yaml");
        const QString phrase = QString::fromUtf8("墨羽词典管理");
        const QString spelling = QStringLiteral("moyucidianguanli");
        const auto containsPhrase = [&]() {
            call(QStringLiteral("resetSearch"));
            call(QStringLiteral("search"), {spelling});
            return candidateIndex(phrase) >= 0;
        };
        QVERIFY(!containsPhrase());
        const QJsonObject imported = operation(QStringLiteral("importDictionaryFile"), {QUrl::fromLocalFile(inputPath).toString()});
        QVERIFY2(imported.value(QStringLiteral("error")).toString().isEmpty(),
                 qPrintable(imported.value(QStringLiteral("error")).toString()));
        QVERIFY(containsPhrase());
        QVERIFY(call(QStringLiteral("predictionList"), {QString::fromUtf8("墨羽词典"), 100}).toStringList()
                .contains(QString::fromUtf8("管理")));
        QVERIFY(!operation(QStringLiteral("importDictionaryFile"), {inputPath})
                .value(QStringLiteral("error")).toString().isEmpty());
        QVERIFY(operation(QStringLiteral("setDictionaryFileEnabled"), {name, false})
                .value(QStringLiteral("error")).toString().isEmpty());
        QVERIFY(!containsPhrase());
        QVERIFY(!call(QStringLiteral("predictionList"), {QString::fromUtf8("墨羽词典"), 100}).toStringList()
                .contains(QString::fromUtf8("管理")));
        QVERIFY(operation(QStringLiteral("setDictionaryFileEnabled"), {name, true})
                .value(QStringLiteral("error")).toString().isEmpty());
        QVERIFY(containsPhrase());
        QVERIFY(call(QStringLiteral("releaseForKeyboard")).toBool());
        QTRY_VERIFY(!QDBusConnection::sessionBus().interface()->isServiceRegistered(serviceName).value());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        startServer();
        QVERIFY(call(QStringLiteral("init")).toBool());
        QVERIFY(containsPhrase());
        for (int repetition = 0; repetition < 2; ++repetition) {
            const QJsonObject exported = operation(QStringLiteral("exportDictionaryFile"), {name});
            QVERIFY2(exported.value(QStringLiteral("error")).toString().isEmpty(),
                     qPrintable(exported.value(QStringLiteral("error")).toString()));
            const QString path = exported.value(QStringLiteral("path")).toString();
            QVERIFY2(path.startsWith(configDirectory + QStringLiteral("/exports/")), qPrintable(path));
            QFile exportedFile(path);
            QVERIFY(exportedFile.open(QIODevice::ReadOnly));
            QVERIFY(exportedFile.readAll().contains(contents));
        }
        const QString userExport = operation(QStringLiteral("exportUserDictionary"))
                .value(QStringLiteral("path")).toString();
        QVERIFY(QFile::exists(userExport));
        QVERIFY(operation(QStringLiteral("removeDictionaryFile"), {name})
                .value(QStringLiteral("error")).toString().isEmpty());
        QVERIFY(!containsPhrase());
        const QJsonObject info = QJsonDocument::fromJson(call(QStringLiteral("dictionaryFiles")).toString().toUtf8()).object();
        QVERIFY(info.value(QStringLiteral("files")).toArray().isEmpty());

        QVERIFY(input.open(QIODevice::WriteOnly | QIODevice::Truncate));
        input.write(contents + "broken line\n");
        input.close();
        QVERIFY(!operation(QStringLiteral("importDictionaryFile"), {inputPath})
                .value(QStringLiteral("error")).toString().isEmpty());
        QVERIFY(!containsPhrase());
        QVERIFY(!operation(QStringLiteral("removeDictionaryFile"), {QStringLiteral("../default.yaml")})
                .value(QStringLiteral("error")).toString().isEmpty());
    }

private:
    QStringList candidateTexts(QObject *decoder)
    {
        QAbstractItemModel *model = qobject_cast<QAbstractItemModel *>(
                    decoder->property("candidates").value<QObject *>());
        QStringList result;
        if (!model)
            return result;
        const int textRole = model->roleNames().key(QByteArrayLiteral("text"), -1);
        for (int row = 0; row < model->rowCount(); ++row)
            result.append(model->data(model->index(row, 0), textRole).toString());
        return result;
    }

    void setContext(QObject *root, const QString &text, int cursor)
    {
        QMetaObject::invokeMethod(root, "setContext", Q_ARG(QVariant, text), Q_ARG(QVariant, cursor));
    }

    QVariant invoke(QObject *object, const char *method)
    {
        QVariant result;
        QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QVariant, result));
        return result;
    }

    void click(QObject *root, const QString &text, int key)
    {
        QMetaObject::invokeMethod(root, "click", Q_ARG(QVariant, text), Q_ARG(QVariant, key));
    }

    void startServer(const QString &overrideDataDirectory = QString())
    {
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("MOYU_RIME_USER_DIR"),
                           configDirectory + QStringLiteral("/harbour-moyu/rime"));
        environment.insert(QStringLiteral("MOYU_RIME_DATA_DIR"),
                           overrideDataDirectory.isEmpty() ? dataDirectory : overrideDataDirectory);
        server.setProcessEnvironment(environment);
        QString executable = QString::fromLocal8Bit(qgetenv("MOYU_RIME_TEST_BACKEND"));
        if (executable.isEmpty())
            executable = QStringLiteral("/usr/bin/harbour-moyu-rime");
        server.start(executable);
        QVERIFY(server.waitForStarted());
        QTRY_VERIFY(QDBusConnection::sessionBus().interface()->isServiceRegistered(serviceName).value());
    }

    QVariant call(const QString &method, const QVariantList &arguments = {})
    {
        QDBusMessage request = QDBusMessage::createMethodCall(serviceName, QStringLiteral("/Rime"), serviceName, method);
        request.setArguments(arguments);
        const QDBusMessage reply = QDBusConnection::sessionBus().call(request, QDBus::Block, 30000);
        lastError = reply.errorMessage();
        return reply.arguments().value(0);
    }

    int candidateIndex(const QString &text)
    {
        for (int index = 0; index < 100; ++index) {
            const QString candidate = call(QStringLiteral("candidateAt"), {index}).toString();
            if (candidate == text)
                return index;
            if (candidate.isEmpty())
                break;
        }
        return -1;
    }

    QString configDirectory;
    QString dataDirectory;
    QString legacyPath;
    QStringList spellings;
    QByteArray legacyBytes;
    QString lastError;
    QProcess server;
    QProcess configurationServer;
};

class AppDictionarySmoke : public QObject
{
    Q_OBJECT

private slots:
    void dictionaryService()
    {
        const QDBusMessage initRequest = QDBusMessage::createMethodCall(
                    serviceName, QStringLiteral("/Rime"), serviceName, QStringLiteral("init"));
        const QDBusMessage initReply = QDBusConnection::sessionBus().call(initRequest);
        QVERIFY2(initReply.type() == QDBusMessage::ReplyMessage, qPrintable(initReply.errorMessage()));
        QVERIFY(initReply.arguments().value(0).toBool());

        QDBusMessage pageRequest = QDBusMessage::createMethodCall(
                    serviceName, QStringLiteral("/Rime"), serviceName,
                    QStringLiteral("userDictionaryEntryPage"));
        pageRequest.setArguments({0, 100});
        const QDBusMessage pageReply = QDBusConnection::sessionBus().call(pageRequest);
        QVERIFY2(pageReply.type() == QDBusMessage::ReplyMessage, qPrintable(pageReply.errorMessage()));
        const QStringList rows = pageReply.arguments().value(0).toStringList();
        QVERIFY(!rows.isEmpty());
        QVERIFY(rows.first().startsWith(QStringLiteral("@next:")));
    }

    void installedDictionaryPage()
    {
        QStringList pageWarnings;
        QQmlEngine engine;
        QObject::connect(&engine, &QQmlEngine::warnings,
                         [&pageWarnings](const QList<QQmlError> &warnings) {
            for (const QQmlError &warning : warnings)
                pageWarnings.append(warning.toString());
        });
        QQmlComponent windowComponent(&engine);
        windowComponent.setData(
                    "import QtQuick 2.0\n"
                    "import Sailfish.Silica 1.0\n"
                    "ApplicationWindow {\n"
                    "    visible: false\n"
                    "    cover: null\n"
                    "    property Item openedPage: null\n"
                    "    function openPage() {\n"
                    "        openedPage = pageStack.push(Qt.resolvedUrl('pages/UserDictionaryPage.qml'),\n"
                    "                                    {}, PageStackAction.Immediate)\n"
                    "    }\n"
                    "}\n", QUrl::fromLocalFile(QStringLiteral("/usr/share/harbour-moyu/qml/app-bus-test.qml")));
        QVERIFY2(windowComponent.isReady(), qPrintable(windowComponent.errorString()));
        QScopedPointer<QObject> applicationWindow(windowComponent.create());
        QVERIFY2(applicationWindow, qPrintable(windowComponent.errorString()));
        QVERIFY(QMetaObject::invokeMethod(applicationWindow.data(), "openPage"));
        QTRY_VERIFY(applicationWindow->property("openedPage").value<QObject *>());
        QObject *page = applicationWindow->property("openedPage").value<QObject *>();
        QTRY_VERIFY2(page->property("initialized").toBool(),
                     qPrintable(page->property("statusMessage").toString()));
        QVERIFY(!page->property("loading").toBool());
        const QString status = page->property("statusMessage").toString();
        QVERIFY2(!status.contains(QString::fromUtf8("失败")), qPrintable(status));
        QVERIFY2(pageWarnings.isEmpty(), qPrintable(pageWarnings.join(QLatin1Char('\n'))));
    }
};

int main(int argc, char **argv)
{
    if (argc == 2 && QByteArray(argv[1]) == QByteArrayLiteral("--app-bus")) {
        const QByteArray busAddress = qgetenv("DBUS_SESSION_BUS_ADDRESS");
        if (!busAddress.startsWith("unix:path=/proc/") ||
                !busAddress.endsWith("/root/run/firejail/mnt/dbus/user")) {
            qWarning("Run the app-bus checks with tools/test-sailjail.sh");
            return 1;
        }
        QGuiApplication application(argc, argv);
        AppDictionarySmoke tests;
        char *testArguments[] = {argv[0], nullptr};
        return QTest::qExec(&tests, 1, testArguments);
    }

    const QString testRoot = QString::fromLocal8Bit(qgetenv("MOYU_RIME_TEST_ROOT"));
    if (!testRoot.startsWith(QStringLiteral("/tmp/moyu-rime-test-")) || !QDir(testRoot).exists()) {
        qWarning("Run this binary with tools/test-rime.sh");
        return 1;
    }
    const QString configDirectory = testRoot + QStringLiteral("/config");
    qputenv("XDG_CONFIG_HOME", configDirectory.toUtf8());
    QGuiApplication application(argc, argv);
    qmlRegisterUncreatableType<SimulatedMaliit>("com.meego.maliitquick", 1, 0, "Maliit",
                                               QStringLiteral("Maliit key-event enum"));
    RimeSmoke tests(configDirectory);
    return QTest::qExec(&tests, argc, argv);
}

#include "rime-smoke.moc"
