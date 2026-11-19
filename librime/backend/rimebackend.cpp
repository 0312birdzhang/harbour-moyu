// SPDX-License-Identifier: GPL-3.0-or-later
#include "rimebackend.h"
#include "legacydictionary.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegExp>
#include <QStandardPaths>
#include <QVector>
#include <algorithm>
#include <limits>
#include <rime/context.h>
#include <rime/registry.h>
#include <rime/service.h>
#include <rime/dict/db.h>
#include <rime/dict/dictionary.h>
#include <rime/dict/level_db.h>
#include <rime/dict/reverse_lookup_dictionary.h>
#include <rime/dict/user_dictionary.h>

namespace {

const char *const schemaId = "moyu_pinyin";
const char *const recentPhrasesKey = "/moyu_recent_phrases";
const std::string pendingPhrasePrefix = "\x01/moyu_pending_phrase/";
const int minimumLearnedCommits = 5;
const QStringList defaultPredictions = QStringLiteral(
            "我 你 不 是 这 有 那 可以 还 一 在 好 这个 要 现在 没").split(QLatin1Char(' '));
const QHash<QString, QString> pinyinAliases = {
    {QStringLiteral("lve"), QStringLiteral("lue")},
    {QStringLiteral("nve"), QStringLiteral("nue")}
};

// Pending counts share Rime's transactions, but metadata keys stay out of
// candidate lookups. Manual edits bypass the automatic learning threshold.
class UserDatabase : public rime::UserDbWrapper<rime::LevelDb>
{
public:
    UserDatabase(const rime::path &path, const std::string &name)
        : rime::UserDbWrapper<rime::LevelDb>(path, name) {}

    bool Fetch(const std::string &key, std::string *value) override
    {
        if (key.find('\t') == std::string::npos)
            return rime::LevelDb::Fetch(key, value);
        if (rime::LevelDb::Fetch(pendingPhrasePrefix + key, value))
            return true;
        return rime::LevelDb::Fetch(key, value) && rime::UserDbValue(*value).commits >= 0;
    }

    bool Update(const std::string &key, const std::string &value) override
    {
        if (disabled())
            return false;
        if (key.find('\t') == std::string::npos)
            return rime::LevelDb::Update(key, value);
        rime::UserDbValue usage;
        if (!usage.Unpack(value))
            return false;
        // Rime also updates constituent words with zero commits.
        if (usage.commits == 0)
            return true;
        if (usage.commits > 0 && usage.commits < minimumLearnedCommits)
            return rime::LevelDb::Update(pendingPhrasePrefix + key, value);
        return updatePhrase(key, value);
    }

    bool fetchPhrase(const std::string &key, std::string *value)
    {
        return rime::LevelDb::Fetch(key, value);
    }

    bool updatePhrase(const std::string &key, const std::string &value)
    {
        if (!rime::LevelDb::Update(key, value))
            return false;
        return key.find('\t') == std::string::npos || rime::LevelDb::Erase(pendingPhrasePrefix + key);
    }

    bool stageInfrequentPhrases()
    {
        if (!BeginTransaction())
            return false;
        auto accessor = QueryAll();
        std::string key;
        std::string value;
        while (accessor->GetNextRecord(&key, &value)) {
            rime::UserDbValue usage;
            if (key.find('\t') == std::string::npos || !usage.Unpack(value) ||
                    usage.commits < 0 || usage.commits >= minimumLearnedCommits)
                continue;
            if (!rime::LevelDb::Update(pendingPhrasePrefix + key, value) || !Erase(key)) {
                AbortTransaction();
                return false;
            }
        }
        return CommitTransaction();
    }
};

class UserDatabaseComponent : public rime::UserDbComponent<rime::LevelDb>
{
public:
    explicit UserDatabaseComponent(rime::Db **database) : database(database) {}

    rime::Db *Create(const std::string &name) override
    {
        rime::Db *result = new UserDatabase(DbFilePath(name, extension()), name);
        if (name == schemaId)
            *database = result;
        return result;
    }

private:
    rime::Db **database;
};

QStringList readLines(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QStringList();
    return QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), QString::SkipEmptyParts);
}

bool isChineseCharacter(QChar character)
{
    return character.unicode() >= 0x3400 && character.unicode() <= 0x9fff;
}

bool isChinesePhrase(const QString &text)
{
    return !text.isEmpty() && text.length() <= 64 &&
            std::all_of(text.begin(), text.end(), isChineseCharacter);
}

struct PredictionPhrase {
    QString text;
    quint64 tick = 0;
    int commits = 0;
    double frequency = 0;
};

bool predictionOrder(const PredictionPhrase &left, const PredictionPhrase &right)
{
    if (left.tick != right.tick)
        return left.tick > right.tick;
    if (left.commits != right.commits)
        return left.commits > right.commits;
    if (left.frequency != right.frequency)
        return left.frequency > right.frequency;
    return left.text < right.text;
}

} // namespace

RimeBackend::RimeBackend(QObject *parent) : QObject(parent), api(rime_get_api())
{
    sharedDataDirectory = qgetenv("MOYU_RIME_DATA_DIR");
    if (sharedDataDirectory.isEmpty())
        sharedDataDirectory = "/usr/share/harbour-moyu/rime";
    userDataDirectory = qgetenv("MOYU_RIME_USER_DIR");
    if (userDataDirectory.isEmpty()) {
        userDataDirectory = (QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                             + QStringLiteral("/harbour-moyu/rime")).toUtf8();
    }
    sharedDataDirectory = QDir(QString::fromUtf8(sharedDataDirectory)).absolutePath().toUtf8();
    userDataDirectory = QDir(QString::fromUtf8(userDataDirectory)).absolutePath().toUtf8();
}

RimeBackend::~RimeBackend()
{
    if (!libraryInitialized)
        return;
    destroySession();
    api->join_maintenance_thread();
    api->finalize();
}

void RimeBackend::destroySession()
{
    flushCache();
    userDictionary.reset();
    systemDictionary.reset();
    reverseDictionary.reset();
    if (session)
        api->destroy_session(session);
    session = 0;
    userDatabase = nullptr;
    ready = false;
    candidates.clear();
    candidatesExhausted = true;
    ++compositionRevision;
    pendingCommit.clear();
    committedLength = 0;
    systemPhrases.clear();
    commonPhrases.clear();
}

bool RimeBackend::initializeLibrary()
{
    if (ownershipTransferred)
        return false;
    if (libraryInitialized)
        return true;
    if (!QDir(QString::fromUtf8(sharedDataDirectory)).exists() ||
            !QDir().mkpath(QString::fromUtf8(userDataDirectory))) {
        qWarning() << "Rime data directories are unavailable";
        return false;
    }

    RIME_STRUCT(RimeTraits, traits);
    traits.shared_data_dir = sharedDataDirectory.constData();
    traits.user_data_dir = userDataDirectory.constData();
    traits.distribution_name = "Moyu IME";
    traits.distribution_code_name = "moyuime";
    traits.distribution_version = "0.5.9";
    traits.app_name = "rime.moyuime";
    traits.log_dir = "";
    static const char *modules[] = {"default", "levers", nullptr};
    traits.modules = modules;
    api->setup(&traits);
    api->initialize(&traits);
    api->deployer_initialize(&traits);
    libraryInitialized = true;
    return true;
}

bool RimeBackend::deploy()
{
    return initializeLibrary() && api->deploy();
}

bool RimeBackend::init()
{
    if (ownershipTransferred)
        return false;
    if (ready)
        return true;
    if (!initializeLibrary())
        return false;

    destroySession();
    if (!prepareDictionaryFiles())
        return false;
    if (api->start_maintenance(False))
        api->join_maintenance_thread();
    rime::Registry::instance().Register("userdb", new UserDatabaseComponent(&userDatabase));
    session = api->create_session();
    if (!session || !api->select_schema(session, schemaId)) {
        qWarning() << "Could not start the Moyu Rime schema";
        destroySession();
        return false;
    }
    auto *component = static_cast<rime::UserDictionaryComponent *>(
                rime::UserDictionary::Require("user_dictionary"));
    userDictionary.reset(component->Create(schemaId, "userdb"));
    if (!userDictionary || !userDictionary->Load() || !userDatabase ||
            !static_cast<UserDatabase *>(userDatabase)->stageInfrequentPhrases()) {
        qWarning() << "Could not open the Rime user dictionary";
        destroySession();
        return false;
    }

    auto *dictionaryComponent = static_cast<rime::DictionaryComponent *>(
                rime::Dictionary::Require("dictionary"));
    systemDictionary.reset(dictionaryComponent->Create(schemaId, schemaId, {}));
    auto *reverseComponent = static_cast<rime::ReverseLookupDictionaryComponent *>(
                rime::ReverseLookupDictionary::Require("reverse_lookup_dictionary"));
    reverseDictionary.reset(reverseComponent->Create(schemaId));
    if (!systemDictionary || !reverseDictionary ||
            !systemDictionary->Load() || !reverseDictionary->Load()) {
        qWarning() << "Could not open the Rime pinyin dictionary";
        destroySession();
        return false;
    }
    const QStringList spellings = readLines(QString::fromUtf8(sharedDataDirectory)
                                           + QStringLiteral("/full-pinyin.txt"));
    syllables = QSet<QString>::fromList(spellings);
    ready = !syllables.isEmpty() && loadPredictionDictionary();
    if (ready)
        migrateLegacyDictionary();
    else
        destroySession();
    return ready;
}

bool RimeBackend::releaseForKeyboard()
{
    if (!ownershipTransferred) {
        destroySession();
        if (libraryInitialized) {
            api->join_maintenance_thread();
            api->finalize();
            libraryInitialized = false;
        }
        ownershipTransferred = true;
        emit ownershipReleased();
    }
    return true;
}

void RimeBackend::setUserDictionary(bool enabled)
{
    if (!init())
        return;
    flushCache();
    if (enabled) {
        userDatabase->enable();
        userDictionary->Load();
    } else {
        userDatabase->disable();
    }
}

int RimeBackend::search(const QString &spelling)
{
    searchInput(spelling, -1);
    ensureCandidates(-1);
    return candidates.size();
}

int RimeBackend::searchInput(const QString &spelling, int limit)
{
    if (!init())
        return 0;
    QString input = spelling.trimmed().toLower();
    input.replace(QRegExp(QStringLiteral("\\s+")), QStringLiteral("'"));
    if (!QRegExp(QStringLiteral("^[a-z']*$")).exactMatch(input))
        return candidates.size();

    const QString current = pinyinString(false);
    pendingCommit.clear();
    committedLength = 0;
    if (input.startsWith(current)) {
        for (int index = current.length(); index < input.length(); ++index)
            api->process_key(session, input.at(index).unicode(), 0);
    } else {
        api->clear_composition(session);
        api->set_input(session, input.toLatin1().constData());
    }
    updateCandidates(limit);
    return candidates.size();
}

void RimeBackend::resetSearch()
{
    if (session)
        api->clear_composition(session);
    candidates.clear();
    candidatesExhausted = true;
    ++compositionRevision;
    pendingCommit.clear();
    committedLength = 0;
}

void RimeBackend::updateCandidates(int limit)
{
    candidates.clear();
    candidatesExhausted = false;
    ++compositionRevision;
    ensureCandidates(limit);
}

void RimeBackend::ensureCandidates(int limit)
{
    if (!session || candidatesExhausted || (limit >= 0 && candidates.size() >= limit))
        return;
    RimeCandidateListIterator iterator = {};
    if (!api->candidate_list_from_index(session, &iterator, candidates.size())) {
        candidatesExhausted = true;
        return;
    }
    while (limit < 0 || candidates.size() < limit) {
        if (!api->candidate_list_next(&iterator)) {
            candidatesExhausted = true;
            break;
        }
        candidates.append(QString::fromUtf8(iterator.candidate.text));
    }
    api->candidate_list_end(&iterator);
}

QString RimeBackend::candidateAt(int index)
{
    if (index < 0 || index == std::numeric_limits<int>::max())
        return QString();
    ensureCandidates(index + 1);
    return candidates.value(index);
}

int RimeBackend::chooceCandidate(int index)
{
    return selectInputCandidate(index, -1);
}

int RimeBackend::selectInputCandidate(int index, int limit)
{
    if (!init() || index < 0 || index == std::numeric_limits<int>::max())
        return -1;
    ensureCandidates(index + 1);
    if (index >= candidates.size() ||
            !api->select_candidate(session, size_t(index)))
        return -1;

    RIME_STRUCT(RimeCommit, commit);
    if (api->get_commit(session, &commit)) {
        pendingCommit = QString::fromUtf8(commit.text);
        committedLength = pendingCommit.length();
        api->free_commit(&commit);
        flushCache();
        rememberPhrase(pendingCommit);
    }
    updateCandidates(limit);
    return candidates.size();
}

int RimeBackend::backspace()
{
    return deleteInput(-1);
}

int RimeBackend::deleteInput(int limit)
{
    if (!init())
        return 0;
    api->process_key(session, 0xff08, 0);
    updateCandidates(limit);
    return candidates.size();
}

QString RimeBackend::compositionSnapshot(int pageSize, bool ok)
{
    QJsonObject result;
    result.insert(QStringLiteral("ok"), ok);
    result.insert(QStringLiteral("revision"), compositionRevision);
    result.insert(QStringLiteral("input"), pinyinString(false));
    result.insert(QStringLiteral("preedit"), preeditText());
    result.insert(QStringLiteral("candidates"), QJsonArray::fromStringList(candidates.mid(0, pageSize)));
    result.insert(QStringLiteral("hasMore"), candidates.size() > pageSize || !candidatesExhausted);
    result.insert(QStringLiteral("commit"), ok ? takeCommit() : QString());
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

QString RimeBackend::searchComposition(const QString &spelling, int pageSize)
{
    pageSize = qBound(1, pageSize, std::numeric_limits<int>::max() - 1);
    const bool ok = init();
    if (ok)
        searchInput(spelling, pageSize + 1);
    return compositionSnapshot(pageSize, ok);
}

QString RimeBackend::backspaceComposition(int pageSize)
{
    pageSize = qBound(1, pageSize, std::numeric_limits<int>::max() - 1);
    const bool ok = init();
    if (ok)
        deleteInput(pageSize + 1);
    return compositionSnapshot(pageSize, ok);
}

QString RimeBackend::selectComposition(int index, int pageSize, int revision)
{
    pageSize = qBound(1, pageSize, std::numeric_limits<int>::max() - 1);
    // A different client may have changed the composition since this page.
    const bool ok = revision == compositionRevision && selectInputCandidate(index, pageSize + 1) >= 0;
    return compositionSnapshot(pageSize, ok);
}

QString RimeBackend::candidatePage(int offset, int limit, int revision)
{
    QJsonObject result;
    const bool ok = session && revision == compositionRevision && offset >= 0 && limit >= -1
            && (limit < 0 || offset <= std::numeric_limits<int>::max() - limit - 1);
    result.insert(QStringLiteral("ok"), ok);
    result.insert(QStringLiteral("revision"), compositionRevision);
    if (ok) {
        ensureCandidates(limit < 0 ? -1 : offset + limit + 1);
        result.insert(QStringLiteral("candidates"), QJsonArray::fromStringList(candidates.mid(offset, limit)));
        result.insert(QStringLiteral("hasMore"), limit >= 0 &&
                      (candidates.size() > offset + limit || !candidatesExhausted));
    }
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

QString RimeBackend::takeCommit()
{
    const QString result = pendingCommit;
    pendingCommit.clear();
    return result;
}

QString RimeBackend::commitComposition(bool raw)
{
    if (!init())
        return QString();
    if (raw)
        api->process_key(session, 0xff0d, 0);
    else {
        auto currentSession = rime::Service::instance().GetSession(session);
        auto &composition = currentSession->context()->composition();
        // Rime saves learned entries at confirmed segment boundaries.
        if (!composition.empty())
            composition.back().status = rime::Segment::kConfirmed;
        api->commit_composition(session);
    }
    RIME_STRUCT(RimeCommit, commit);
    QString result;
    if (api->get_commit(session, &commit)) {
        result = QString::fromUtf8(commit.text);
        api->free_commit(&commit);
        flushCache();
        rememberPhrase(result);
    }
    updateCandidates();
    return result;
}

QString RimeBackend::preeditText()
{
    if (!session)
        return QString();
    RIME_STRUCT(RimeContext, context);
    if (!api->get_context(session, &context))
        return QString();
    const QString result = QString::fromUtf8(context.composition.preedit);
    api->free_context(&context);
    return result;
}

QString RimeBackend::pinyinString(bool decoded)
{
    Q_UNUSED(decoded);
    return session ? QString::fromUtf8(api->get_input(session)) : QString();
}

QStringList RimeBackend::spellingStartPositions()
{
    const QString input = pinyinString(false);
    QStringList positions;
    positions.append(QStringLiteral("0"));
    QStringList tokens = preeditText().split(QRegExp(QStringLiteral("[ '\\s]+")), QString::SkipEmptyParts);
    int offset = 0;
    for (const QString &token : tokens) {
        QString spelling = token;
        spelling.replace(QChar(0x00fc), QLatin1Char('v'));
        if (!QRegExp(QStringLiteral("^[a-z]+$")).exactMatch(spelling))
            continue;
        while (offset < input.length() && (input.at(offset) == QLatin1Char('\'') ||
                                          input.at(offset) == QLatin1Char(' ')))
            ++offset;
        offset = qMin(input.length(), offset + spelling.length());
        positions.append(QString::number(offset));
    }
    positions.prepend(QString::number(positions.size() - 1));
    return positions;
}

int RimeBackend::getSplStart()
{
    return spellingStartPositions().value(0).toInt();
}

int RimeBackend::fixedLength()
{
    if (committedLength)
        return committedLength;
    int length = 0;
    const QString text = preeditText();
    while (length < text.length() && text.at(length).unicode() >= 0x3400 &&
           text.at(length).unicode() <= 0x9fff)
        ++length;
    return length;
}

void RimeBackend::flushCache()
{
    if (userDictionary)
        userDictionary->CommitPendingTransaction();
}

bool RimeBackend::loadPredictionDictionary()
{
    // Rime's LookupWords() queries single syllables in the system table.
    // Index the shipped source words to complete multi-syllable text prefixes.
    const QDir directory(QString::fromUtf8(sharedDataDirectory));
    QStringList dictionaries;
    for (const QString &name : directory.entryList({QStringLiteral("*.dict.yaml")}, QDir::Files))
        dictionaries.append(directory.filePath(name));
    const QString extraDictionary = QString::fromUtf8(userDataDirectory)
            + QStringLiteral("/dictionary-files/current/moyu_extra.dict.yaml");
    if (QFile::exists(extraDictionary))
        dictionaries.append(extraDictionary);
    for (const QString &dictionary : dictionaries) {
        QFile file(dictionary);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        while (!file.atEnd()) {
            const QStringList fields = QString::fromUtf8(file.readLine()).trimmed().split(QLatin1Char('\t'));
            if (fields.size() < 2 || !isChinesePhrase(fields[0]))
                continue;
            bool validFrequency = true;
            const double frequency = fields.size() > 2 ? fields[2].toDouble(&validFrequency) : 1.0;
            if (validFrequency && frequency >= 0)
                systemPhrases[fields[0]] = qMax(systemPhrases.value(fields[0]), frequency);
        }
    }
    QVector<PredictionPhrase> phrases;
    phrases.reserve(systemPhrases.size());
    for (auto entry = systemPhrases.constBegin(); entry != systemPhrases.constEnd(); ++entry)
        phrases.append({entry.key(), 0, 0, entry.value()});
    const int count = qMin(100, phrases.size());
    std::partial_sort(phrases.begin(), phrases.begin() + count, phrases.end(), predictionOrder);
    for (int index = 0; index < count; ++index)
        commonPhrases.append(phrases[index].text);
    if (commonPhrases.isEmpty())
        qWarning() << "Could not load prediction words from" << directory.path();
    return !commonPhrases.isEmpty();
}

QStringList RimeBackend::recentPhrases() const
{
    std::string value;
    QStringList result;
    if (!userDatabase->MetaFetch(recentPhrasesKey, &value))
        return result;
    const QJsonArray entries = QJsonDocument::fromJson(QByteArray::fromStdString(value)).array();
    for (const QJsonValue &entry : entries) {
        const QString text = entry.toString();
        if (isChinesePhrase(text) && !result.contains(text))
            result.append(text);
        if (result.size() == 100)
            break;
    }
    return result;
}

bool RimeBackend::writeRecentPhrases(const QStringList &phrases)
{
    const QByteArray value = QJsonDocument(QJsonArray::fromStringList(phrases)).toJson(QJsonDocument::Compact);
    // Administrative removal/reset also clears history while learning is off.
    return static_cast<UserDatabase *>(userDatabase)->updatePhrase(
                std::string("\x01") + recentPhrasesKey, value.toStdString());
}

void RimeBackend::rememberPhrase(const QString &text)
{
    if (userDatabase->disabled() || !isChinesePhrase(text))
        return;
    flushCache();
    QStringList phrases = recentPhrases();
    if (phrases.value(0) == text)
        return;
    phrases.removeAll(text);
    phrases.prepend(text);
    if (!writeRecentPhrases(phrases.mid(0, 100)))
        qWarning() << "Could not save recently selected words";
}

void RimeBackend::recordPrediction(const QString &text)
{
    if (init() && pinyinString(false).isEmpty())
        rememberPhrase(text);
}

QStringList RimeBackend::predictionList(const QString &history, int fetchSize)
{
    QStringList result;
    if (fetchSize <= 0 || !init())
        return result;
    flushCache();
    const int maximum = qMin(fetchSize, 100);
    int start = history.length();
    while (start > 0 && history.length() - start < 8 && isChineseCharacter(history.at(start - 1)))
        --start;
    const QString context = history.mid(start);
    if (context.isEmpty())
        return defaultPredictions.mid(0, maximum);
    QSet<QString> seen;
    const auto append = [&](const QString &text) {
        if (result.size() < maximum && !text.isEmpty() && !seen.contains(text)) {
            seen.insert(text);
            result.append(text);
        }
    };
    QStringList recent;
    QVector<PredictionPhrase> userPhrases;
    if (!userDatabase->disabled()) {
        recent = recentPhrases();
        auto accessor = userDatabase->QueryAll();
        std::string key;
        std::string value;
        while (accessor->GetNextRecord(&key, &value)) {
            const QString record = QString::fromStdString(key);
            const int separator = record.indexOf(QLatin1Char('\t'));
            rime::UserDbValue usage;
            if (separator < 0 || !usage.Unpack(value) || usage.commits < 0)
                continue;
            const QString text = record.mid(separator + 1);
            if (isChinesePhrase(text))
                userPhrases.append({text, usage.tick, usage.commits, systemPhrases.value(text)});
        }
        std::sort(userPhrases.begin(), userPhrases.end(), predictionOrder);
    }

    for (int length = context.length(); length > 0 && result.size() < maximum; --length) {
        const QString prefix = context.right(length);
        for (const QString &text : recent) {
            if (text.startsWith(prefix))
                append(text.mid(length));
        }
        for (const PredictionPhrase &phrase : userPhrases) {
            if (phrase.text.startsWith(prefix))
                append(phrase.text.mid(length));
        }
        QVector<PredictionPhrase> matches;
        const QMap<QString, double> &dictionary = systemPhrases;
        for (auto entry = dictionary.lowerBound(prefix);
             entry != dictionary.constEnd() && entry.key().startsWith(prefix); ++entry) {
            if (entry.key().length() > length)
                matches.append({entry.key(), 0, 0, entry.value()});
        }
        std::sort(matches.begin(), matches.end(), predictionOrder);
        for (const PredictionPhrase &phrase : matches)
            append(phrase.text.mid(length));
    }
    for (const QString &text : recent)
        append(text);
    for (const PredictionPhrase &phrase : userPhrases)
        append(phrase.text);
    for (const QString &text : commonPhrases)
        append(text);
    return result;
}

QString RimeBackend::normalizePinyin(const QString &spelling, int characters) const
{
    QString input = spelling.trimmed().toLower();
    input.replace(QStringLiteral("u:"), QStringLiteral("v"));
    input.replace(QChar(0x00fc), QLatin1Char('v'));
    QStringList explicitSyllables = input.split(QRegExp(QStringLiteral("['\\s]+")),
                                                QString::SkipEmptyParts);
    if (explicitSyllables.size() > 1 || characters == 1) {
        if (explicitSyllables.size() != characters)
            return QString();
        for (QString &syllable : explicitSyllables) {
            syllable = pinyinAliases.value(syllable, syllable);
            if (!syllables.contains(syllable))
                return QString();
        }
        return explicitSyllables.join(QLatin1Char(' '));
    }

    // Support the contiguous spelling accepted by the previous editor.
    QVector<QVector<QStringList>> paths(characters + 1, QVector<QStringList>(input.length() + 1));
    paths[0][0].append(QString());
    for (int count = 0; count < characters; ++count) {
        for (int offset = 0; offset < input.length(); ++offset) {
            if (paths[count][offset].isEmpty())
                continue;
            for (int length = 1; length <= 6 && offset + length <= input.length(); ++length) {
                QString syllable = input.mid(offset, length);
                syllable = pinyinAliases.value(syllable, syllable);
                if (!syllables.contains(syllable))
                    continue;
                QStringList path = paths[count][offset];
                if (count == 0)
                    path.clear();
                path.append(syllable);
                paths[count + 1][offset + length] = path;
            }
        }
    }
    return paths[characters][input.length()].join(QLatin1Char(' '));
}

QString RimeBackend::validatePhrase(const QString &phrase, const QString &spelling,
                                  QString *normalized, int minimumLength) const
{
    if (phrase.length() < minimumLength || phrase.length() > 8)
        return QString::fromUtf8("词组长度需为 %1—8 个汉字").arg(minimumLength);
    for (QChar character : phrase) {
        if (character.unicode() < 0x3400 || character.unicode() > 0x9fff)
            return QString::fromUtf8("词组只能包含汉字");
    }
    *normalized = normalizePinyin(spelling, phrase.length());
    if (normalized->isEmpty())
        return QString::fromUtf8("请输入与汉字数量相同的完整拼音音节");
    return QString();
}

QString RimeBackend::dictionaryKey(const QString &phrase, const QString &spelling) const
{
    return spelling + QStringLiteral(" \t") + phrase;
}

bool RimeBackend::writePhrase(const QString &phrase, const QString &spelling, bool remove)
{
    userDictionary->Load();
    const std::string key = dictionaryKey(phrase, spelling).toStdString();
    std::string existing;
    rime::UserDbValue value;
    if (static_cast<UserDatabase *>(userDatabase)->fetchPhrase(key, &existing))
        value.Unpack(existing);
    value.commits = remove ? -qMax(1, qAbs(value.commits)) : qMax(100, value.commits);
    value.dee = remove ? 0.0 : qMax(1.0, value.dee);
    value.tick = userDictionary->tick();
    return static_cast<UserDatabase *>(userDatabase)->updatePhrase(key, value.Pack());
}

QString RimeBackend::addUserPhrase(const QString &phrase, const QString &pinyin)
{
    if (!init())
        return QString::fromUtf8("词典初始化失败");
    const QString word = phrase.trimmed();
    QString spelling;
    const QString error = validatePhrase(word, pinyin, &spelling);
    if (!error.isEmpty())
        return error;
    flushCache();
    auto *transaction = dynamic_cast<rime::Transactional *>(userDatabase);
    if (!transaction->BeginTransaction())
        return QString::fromUtf8("无法开始词典写入");
    if (!writePhrase(word, spelling, false)) {
        transaction->AbortTransaction();
        return QString::fromUtf8("词典写入失败");
    }
    return transaction->CommitTransaction() ? QString() : QString::fromUtf8("词典保存失败");
}

QStringList RimeBackend::userDictionaryEntryList()
{
    QStringList rows;
    if (!init())
        return rows;
    flushCache();
    auto accessor = userDatabase->QueryAll();
    std::string key;
    std::string value;
    while (accessor->GetNextRecord(&key, &value)) {
        const QString text = QString::fromStdString(key);
        const int separator = text.indexOf(QLatin1Char('\t'));
        if (separator < 0 || rime::UserDbValue(value).commits < 0)
            continue;
        rows.append(text.mid(separator + 1) + QLatin1Char('\t') + text.left(separator).trimmed());
    }
    return rows;
}

int RimeBackend::userDictionaryEntryCount()
{
    return userDictionaryEntryList().size();
}

QStringList RimeBackend::userDictionaryEntryPage(int offset, int limit)
{
    const QStringList rows = userDictionaryEntryList();
    const int begin = qBound(0, offset, rows.size());
    const int end = qMin(rows.size(), begin + qBound(1, limit, 500));
    QStringList page;
    page.append(QStringLiteral("@next:%1:%2").arg(end).arg(rows.size()));
    page.append(rows.mid(begin, end - begin));
    return page;
}

QString RimeBackend::removeUserPhrase(const QString &phrase, const QString &pinyin)
{
    if (!init())
        return QString::fromUtf8("词典初始化失败");
    const QString spelling = normalizePinyin(pinyin, phrase.length());
    std::string value;
    if (spelling.isEmpty() || !static_cast<UserDatabase *>(userDatabase)->fetchPhrase(
                dictionaryKey(phrase, spelling).toStdString(), &value)
            || rime::UserDbValue(value).commits < 0)
        return QString::fromUtf8("词条已不存在");
    flushCache();
    auto *transaction = dynamic_cast<rime::Transactional *>(userDatabase);
    if (!transaction->BeginTransaction())
        return QString::fromUtf8("无法开始词典写入");
    if (!writePhrase(phrase, spelling, true)) {
        transaction->AbortTransaction();
        return QString::fromUtf8("词条删除失败");
    }
    QStringList recent = recentPhrases();
    recent.removeAll(phrase);
    if (!writeRecentPhrases(recent)) {
        transaction->AbortTransaction();
        return QString::fromUtf8("词条删除失败");
    }
    return transaction->CommitTransaction() ? QString() : QString::fromUtf8("词典保存失败");
}

QString RimeBackend::updateUserPhrase(const QString &oldPhrase, const QString &oldPinyin,
                                    const QString &newPhrase, const QString &newPinyin)
{
    if (!init())
        return QString::fromUtf8("词典初始化失败");
    const QString word = newPhrase.trimmed();
    QString spelling;
    const QString error = validatePhrase(word, newPinyin, &spelling, qBound(1, oldPhrase.length(), 2));
    if (!error.isEmpty())
        return error;
    const QString oldSpelling = normalizePinyin(oldPinyin, oldPhrase.length());
    if (oldPhrase == word && oldSpelling == spelling)
        return QString();
    std::string existing;
    if (oldSpelling.isEmpty() ||
            !static_cast<UserDatabase *>(userDatabase)->fetchPhrase(
                dictionaryKey(oldPhrase, oldSpelling).toStdString(), &existing) ||
            rime::UserDbValue(existing).commits < 0)
        return QString::fromUtf8("原词条已不存在");
    flushCache();
    auto *transaction = dynamic_cast<rime::Transactional *>(userDatabase);
    if (!transaction->BeginTransaction())
        return QString::fromUtf8("无法开始词典写入");
    if (!writePhrase(word, spelling, false) || !writePhrase(oldPhrase, oldSpelling, true)) {
        transaction->AbortTransaction();
        return QString::fromUtf8("词条修改失败");
    }
    QStringList recent = recentPhrases();
    const int recentIndex = recent.indexOf(oldPhrase);
    if (recentIndex >= 0)
        recent[recentIndex] = word;
    recent.removeDuplicates();
    if (!writeRecentPhrases(recent)) {
        transaction->AbortTransaction();
        return QString::fromUtf8("词条修改失败");
    }
    return transaction->CommitTransaction() ? QString() : QString::fromUtf8("词典保存失败");
}

QString RimeBackend::userDictionaryPath()
{
    return QString::fromUtf8(userDataDirectory) + QStringLiteral("/moyu_pinyin.userdb");
}

bool RimeBackend::resetUserDictionary()
{
    if (!init())
        return false;
    resetSearch();
    flushCache();
    auto *transaction = dynamic_cast<rime::Transactional *>(userDatabase);
    if (!transaction->BeginTransaction())
        return false;
    auto accessor = userDatabase->Query("");
    std::string key;
    std::string value;
    while (accessor->GetNextRecord(&key, &value)) {
        // Phrase keys include pending counts. Other metadata has no tab.
        if (key.find('\t') != std::string::npos && !userDatabase->Erase(key)) {
            transaction->AbortTransaction();
            return false;
        }
    }
    if (!writeRecentPhrases(QStringList())) {
        transaction->AbortTransaction();
        return false;
    }
    return transaction->CommitTransaction();
}

bool RimeBackend::reloadUserDictionaryIfChanged()
{
    return init() && userDictionary->Load();
}

bool RimeBackend::migrateLegacyDictionary()
{
    const QString configDirectory = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    const QString path = configDirectory + QStringLiteral("/harbour-moyu/pinyin/usr_dict.dat");
    QFile file(path);
    if (!file.exists())
        return true;
    if (!file.open(QIODevice::ReadOnly)) {
        migrationMessage = QString::fromUtf8("无法读取旧词典，原文件已保留");
        return false;
    }
    const QByteArray data = file.readAll();
    const std::string digest = QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex().toStdString();
    std::string previousDigest;
    if (userDatabase->MetaFetch("/moyu_legacy_hash", &previousDigest) && previousDigest == digest)
        return true;
    QStringList entries;
    const QStringList spellings = readLines(QString::fromUtf8(sharedDataDirectory)
                                           + QStringLiteral("/legacy-spellings.txt"));
    if (!readLegacyDictionary(data, spellings, &entries, &migrationMessage)) {
        qWarning().noquote() << migrationMessage << path;
        return false;
    }

    auto *transaction = dynamic_cast<rime::Transactional *>(userDatabase);
    if (!transaction->BeginTransaction())
        return false;
    for (const QString &entry : entries) {
        const int separator = entry.indexOf(QLatin1Char('\t'));
        const QString phrase = entry.left(separator);
        const QString spelling = normalizePinyin(entry.mid(separator + 1), phrase.length());
        if (spelling.isEmpty() || !writePhrase(phrase, spelling, false)) {
            transaction->AbortTransaction();
            migrationMessage = QString::fromUtf8("旧词典导入失败，原文件已保留");
            return false;
        }
    }
    if (!userDatabase->MetaUpdate("/moyu_legacy_hash", digest)) {
        transaction->AbortTransaction();
        return false;
    }
    if (!transaction->CommitTransaction())
        return false;
    migrationMessage = QString::fromUtf8("已从旧词典导入 %1 个词条，原文件已保留").arg(entries.size());
    return true;
}

QString RimeBackend::migrationStatus()
{
    init();
    return migrationMessage;
}

QString RimeBackend::engineVersion()
{
    return QString::fromLatin1(api->get_version());
}
