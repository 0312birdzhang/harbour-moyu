// SPDX-License-Identifier: GPL-3.0-or-later
#include "rimebackend.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTextCodec>
#include <QUrl>
#include <QRegExp>
#include <rime/dict/db.h>
#include <yaml-cpp/yaml.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include <unistd.h>

namespace {

QString result(const QString &error = QString(), const QString &path = QString())
{
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("error"), error}, {QStringLiteral("path"), path}
    }).toJson(QJsonDocument::Compact));
}

QString fileRoot(const QByteArray &userDirectory)
{
    return QString::fromUtf8(userDirectory) + QStringLiteral("/dictionary-files");
}

bool readManifest(const QString &root, QJsonArray *files)
{
    const QFileInfo current(root + QStringLiteral("/current"));
    if (!current.isSymLink())
        return !current.exists();
    const QFileInfo generation(current.symLinkTarget());
    if (generation.absolutePath() != root || !generation.fileName().startsWith(QStringLiteral("generation-")) ||
            generation.isSymLink() || !generation.isDir())
        return false;
    QFile manifest(root + QStringLiteral("/current/files.json"));
    if (!manifest.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(manifest.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray())
        return false;
    *files = document.array();
    for (const QJsonValue &value : *files) {
        const QString name = value.toObject().value(QStringLiteral("name")).toString();
        if (name.isEmpty() || name != QFileInfo(name).fileName() || name.contains(QLatin1Char('\\')))
            return false;
    }
    return true;
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.flush();
}

QString exportData(const QString &name, const QByteArray &data)
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
            + QString::fromUtf8("/墨羽词典");
    if (!QDir().mkpath(directory))
        return result(QString::fromUtf8("无法创建词典导出目录"));
    QTemporaryFile temporary(directory + QStringLiteral("/.export-XXXXXX"));
    if (!temporary.open() || temporary.write(data) != data.size() || !temporary.flush())
        return result(QString::fromUtf8("词典导出写入失败"));
    QString path = directory + QLatin1Char('/') + name;
    for (int suffix = 1; QFileInfo::exists(path); ++suffix)
        path = directory + QLatin1Char('/') + QString::number(suffix) + QLatin1Char('-') + name;
    // A hard link publishes the complete file and refuses to replace an existing export.
    if (::link(QFile::encodeName(temporary.fileName()).constData(), QFile::encodeName(path).constData()))
        return result(QString::fromUtf8("无法保存导出的词典文件"));
    return result(QString(), path);
}

} // namespace

bool RimeBackend::prepareDictionaryFiles()
{
    const QString root = fileRoot(userDataDirectory);
    QJsonArray files;
    if (!readManifest(root, &files))
        return false;
    const bool active = QFileInfo(root + QStringLiteral("/current")).isSymLink();
    const QDir user(QString::fromUtf8(userDataDirectory));
    if (!QDir().mkpath(user.filePath(QStringLiteral("build"))))
        return false;
    const QStringList names = {QStringLiteral("moyu_extra.dict.yaml"),
                              QStringLiteral("moyu_extra.schema.yaml"),
                              QStringLiteral("build/moyu_extra.table.bin"),
                              QStringLiteral("build/moyu_extra.prism.bin"),
                              QStringLiteral("build/moyu_extra.reverse.bin")};
    for (const QString &name : names) {
        const QString target = root + QStringLiteral("/current/") + name;
        const QString link = user.filePath(name);
        const QFileInfo info(link);
        if (info.isSymLink() && info.symLinkTarget() == target) {
            if (!active && !QFile::remove(link))
                return false;
            continue;
        }
        if (!active)
            continue;
        // Only compiled Rime output can be replaced. Preserve an existing source override.
        if (info.exists() || info.isSymLink()) {
            if (!name.startsWith(QStringLiteral("build/")) || !QFile::remove(link))
                return false;
        }
        if (!QFile::link(target, link))
            return false;
    }
    return true;
}

QString RimeBackend::dictionaryFiles()
{
    QJsonArray files;
    const bool valid = readManifest(fileRoot(userDataDirectory), &files);
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("error"), valid ? QString() : QString::fromUtf8("词典文件清单损坏，请检查文件目录")},
        {QStringLiteral("files"), files},
        {QStringLiteral("directory"), fileRoot(userDataDirectory) + QStringLiteral("/current/files")},
        {QStringLiteral("builtInVersion"), QStringLiteral("2026-11-11 · 927e1cf1")}
    }).toJson(QJsonDocument::Compact));
}

QString RimeBackend::importDictionaryFile(const QString &path)
{
    if (!init())
        return result(QString::fromUtf8("词典初始化失败"));
    const QUrl url(path);
    const QString localPath = url.isLocalFile() ? url.toLocalFile() : path;
    if ((!url.scheme().isEmpty() && !url.isLocalFile()) || !QDir::isAbsolutePath(localPath))
        return result(QString::fromUtf8("请选择本机的词典文件"));
    QFile input(localPath);
    if (!input.open(QIODevice::ReadOnly) || input.size() > 32 * 1024 * 1024)
        return result(QString::fromUtf8("无法读取文件，单个词典最大为 32 MiB"));
    const QByteArray bytes = input.readAll();
    QTextCodec::ConverterState utf8;
    QString text = QTextCodec::codecForName("UTF-8")->toUnicode(bytes.constData(), bytes.size(), &utf8);
    if (utf8.invalidChars)
        return result(QString::fromUtf8("词典必须使用 UTF-8 编码"));
    if (text.startsWith(QChar(0xfeff)))
        text.remove(0, 1);
    QString name = QFileInfo(localPath).fileName();
    if (name.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive) ||
            name.endsWith(QStringLiteral(".tsv"), Qt::CaseInsensitive))
        name = name.left(name.length() - 4) + QStringLiteral(".dict.yaml");
    if (!name.endsWith(QStringLiteral(".dict.yaml")) || name.length() > 128 ||
            name.contains(QRegExp(QStringLiteral("[\\\\\t\r\n]"))))
        return result(QString::fromUtf8("支持 .dict.yaml、.txt 和 .tsv 词典文件"));
    QJsonArray files;
    if (!readManifest(fileRoot(userDataDirectory), &files))
        return result(QString::fromUtf8("无法读取词典文件清单"));
    for (const QJsonValue &value : files) {
        if (value.toObject().value(QStringLiteral("name")).toString() == name)
            return result(QString::fromUtf8("同名词典已存在，请先删除或更改导入文件名"));
    }

    QStringList lines = text.split(QLatin1Char('\n'));
    int firstEntry = 0;
    for (int index = 0; index < lines.size(); ++index) {
        if (lines[index].trimmed() != QStringLiteral("---"))
            continue;
        int end = index + 1;
        while (end < lines.size() && lines[end].trimmed() != QStringLiteral("..."))
            ++end;
        if (end == lines.size())
            return result(QString::fromUtf8("Rime 词典头部缺少 ... 结束标记"));
        try {
            const YAML::Node header = YAML::Load(lines.mid(index + 1, end - index - 1).join(QLatin1Char('\n')).toStdString());
            if (header["import_tables"] && header["import_tables"].size())
                return result(QString::fromUtf8("请选择直接包含词条的子词典，主词典的 import_tables 暂不支持导入"));
            if (header["columns"]) {
                const auto columns = header["columns"].as<std::vector<std::string>>();
                if (columns != std::vector<std::string>{"text", "code", "weight"} &&
                        columns != std::vector<std::string>{"text", "code"})
                    return result(QString::fromUtf8("词典列顺序应为 text、code、可选 weight"));
            }
        } catch (const YAML::Exception &) {
            return result(QString::fromUtf8("Rime 词典头部格式错误"));
        }
        firstEntry = end + 1;
        break;
    }
    QByteArray entries;
    int count = 0;
    for (int index = firstEntry; index < lines.size(); ++index) {
        const QString line = lines[index].trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const QStringList fields = line.split(QLatin1Char('\t'));
        const QString phrase = fields.value(0).trimmed();
        const QString code = fields.value(1).trimmed();
        const int syllableCount = code.split(QRegExp(QStringLiteral("['\\s]+")), QString::SkipEmptyParts).size();
        const QString spelling = normalizePinyin(code, syllableCount);
        bool validWeight = true;
        const double weight = fields.size() > 2 ? fields[2].toDouble(&validWeight) : 100;
        if (fields.size() < 2 || fields.size() > 3 || phrase.isEmpty() || phrase.size() > 128 ||
                spelling.isEmpty() || !validWeight || !std::isfinite(weight) || weight < 0 ||
                phrase.contains(QRegExp(QStringLiteral("[\\x00-\\x1f]"))))
            return result(QString::fromUtf8("第 %1 行格式错误，应为词条、完整拼音、可选非负词频，用 Tab 分隔").arg(index + 1));
        entries += phrase.toUtf8() + '\t' + spelling.toUtf8() + '\t' + QByteArray::number(weight, 'g', 16) + '\n';
        ++count;
    }
    if (!count)
        return result(QString::fromUtf8("文件中没有可导入的词条"));
    const QByteArray data = QByteArray("---\nname: imported\nversion: '1.0'\nsort: by_weight\nuse_preset_vocabulary: false\n...\n") + entries;
    files.append(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("enabled"), true},
                             {QStringLiteral("entries"), count}, {QStringLiteral("bytes"), data.size()}});
    return applyDictionaryFiles(files, name, data);
}

QString RimeBackend::setDictionaryFileEnabled(const QString &name, bool enabled)
{
    QJsonArray files;
    if (!readManifest(fileRoot(userDataDirectory), &files))
        return result(QString::fromUtf8("无法读取词典文件清单"));
    for (int index = 0; index < files.size(); ++index) {
        QJsonObject file = files[index].toObject();
        if (file.value(QStringLiteral("name")).toString() != name)
            continue;
        if (file.value(QStringLiteral("enabled")).toBool() == enabled)
            return result();
        file.insert(QStringLiteral("enabled"), enabled);
        files[index] = file;
        return applyDictionaryFiles(files);
    }
    return result(QString::fromUtf8("词典文件已不存在"));
}

QString RimeBackend::removeDictionaryFile(const QString &name)
{
    QJsonArray files;
    if (!readManifest(fileRoot(userDataDirectory), &files))
        return result(QString::fromUtf8("无法读取词典文件清单"));
    for (int index = 0; index < files.size(); ++index) {
        if (files[index].toObject().value(QStringLiteral("name")).toString() == name) {
            files.removeAt(index);
            return applyDictionaryFiles(files);
        }
    }
    return result(QString::fromUtf8("词典文件已不存在"));
}

QString RimeBackend::applyDictionaryFiles(const QJsonArray &files, const QString &importName,
                                        const QByteArray &importData)
{
    if (!init())
        return result(QString::fromUtf8("词典初始化失败"));
    const QString root = fileRoot(userDataDirectory);
    if (!QDir().mkpath(root))
        return result(QString::fromUtf8("无法创建词典文件目录"));
    QTemporaryDir generation(root + QStringLiteral("/generation-XXXXXX"));
    if (!generation.isValid() || !QDir().mkpath(generation.path() + QStringLiteral("/files")))
        return result(QString::fromUtf8("无法创建词典编译目录"));
    QByteArray merged("墨羽\tmo yu\t1\n");
    for (const QJsonValue &value : files) {
        const QJsonObject file = value.toObject();
        const QString name = file.value(QStringLiteral("name")).toString();
        QByteArray data = importData;
        if (name != importName) {
            QFile source(root + QStringLiteral("/current/files/") + name);
            if (!source.open(QIODevice::ReadOnly))
                return result(QString::fromUtf8("无法读取已有词典文件"));
            data = source.readAll();
        }
        if (!writeFile(generation.path() + QStringLiteral("/files/") + name, data))
            return result(QString::fromUtf8("保存词典文件失败"));
        if (file.value(QStringLiteral("enabled")).toBool())
            merged += data.mid(data.indexOf("...\n") + 4);
    }
    const QByteArray digest = QCryptographicHash::hash(merged, QCryptographicHash::Sha256).toHex();
    const QByteArray dictionary = QByteArray("---\nname: moyu_extra\nversion: '") + digest +
            "'\nsort: by_weight\nuse_preset_vocabulary: false\n...\n" + merged;
    if (!writeFile(generation.path() + QStringLiteral("/moyu_extra.dict.yaml"), dictionary) ||
            !writeFile(generation.path() + QStringLiteral("/files.json"), QJsonDocument(files).toJson()) ||
            !writeFile(generation.path() + QStringLiteral("/default.yaml"), "config_version: '1.0'\nschema_list:\n  - schema: moyu_extra\n") ||
            !QFile::copy(QString::fromUtf8(sharedDataDirectory) + QStringLiteral("/moyu_extra.schema.yaml"),
                        generation.path() + QStringLiteral("/moyu_extra.schema.yaml")))
        return result(QString::fromUtf8("准备词典编译文件失败"));

    QProcess compiler;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("MOYU_RIME_DATA_DIR"), generation.path());
    environment.insert(QStringLiteral("MOYU_RIME_USER_DIR"), generation.path());
    compiler.setProcessEnvironment(environment);
    compiler.start(QStringLiteral("/usr/bin/harbour-moyu-rime"), {QStringLiteral("--deploy")});
    if (!compiler.waitForStarted() || !compiler.waitForFinished(180000) || compiler.exitCode() != 0 ||
            compiler.exitStatus() != QProcess::NormalExit) {
        compiler.kill();
        compiler.waitForFinished();
        return result(QString::fromUtf8("词典编译失败，已保留当前启用的词典"));
    }
    for (const QString &suffix : {QStringLiteral("table.bin"), QStringLiteral("prism.bin"), QStringLiteral("reverse.bin")}) {
        if (!QFileInfo::exists(generation.path() + QStringLiteral("/build/moyu_extra.") + suffix))
            return result(QString::fromUtf8("词典编译结果不完整"));
    }

    const QString current = root + QStringLiteral("/current");
    const QString previous = QFileInfo(current).symLinkTarget();
    const QString pending = generation.path() + QStringLiteral("/activation");
    if (!QFile::link(generation.path(), pending))
        return result(QString::fromUtf8("无法准备词典切换"));
    const bool learningEnabled = !userDatabase->disabled();
    destroySession();
    if (::rename(QFile::encodeName(pending).constData(), QFile::encodeName(current).constData())) {
        init();
        setUserDictionary(learningEnabled);
        return result(QString::fromUtf8("词典切换失败"));
    }
    generation.setAutoRemove(false);
    if (!init()) {
        destroySession();
        bool restored = false;
        if (previous.isEmpty()) {
            restored = QFile::remove(current);
        } else {
            restored = QFile::link(previous, pending) &&
                    ::rename(QFile::encodeName(pending).constData(), QFile::encodeName(current).constData()) == 0;
        }
        if (!restored)
            return result(QString::fromUtf8("新词典加载及恢复失败，已保留文件组，请检查目录权限"));
        generation.setAutoRemove(true);
        init();
        setUserDictionary(learningEnabled);
        return result(QString::fromUtf8("新词典加载失败，已恢复原词典"));
    }
    setUserDictionary(learningEnabled);
    // The preceding generation is an app-created immutable snapshot.
    if (!previous.isEmpty() && QFileInfo(previous).absolutePath() == root &&
            QFileInfo(previous).fileName().startsWith(QStringLiteral("generation-")))
        QDir(previous).removeRecursively();
    return result();
}

QString RimeBackend::exportDictionaryFile(const QString &name)
{
    QJsonArray files;
    if (!readManifest(fileRoot(userDataDirectory), &files))
        return result(QString::fromUtf8("无法读取词典文件清单"));
    for (const QJsonValue &value : files) {
        if (value.toObject().value(QStringLiteral("name")).toString() != name)
            continue;
        QFile file(fileRoot(userDataDirectory) + QStringLiteral("/current/files/") + name);
        if (!file.open(QIODevice::ReadOnly))
            return result(QString::fromUtf8("无法读取词典文件"));
        return exportData(name, file.readAll());
    }
    return result(QString::fromUtf8("词典文件已不存在"));
}

QString RimeBackend::exportUserDictionary()
{
    if (!init())
        return result(QString::fromUtf8("词典初始化失败"));
    QByteArray data("---\nname: moyu_user_export\nversion: '1.0'\nsort: by_weight\nuse_preset_vocabulary: false\n...\n");
    for (const QString &entry : userDictionaryEntryList())
        data += entry.toUtf8() + "\t100\n";
    return exportData(QStringLiteral("moyu-user.dict.yaml"), data);
}
