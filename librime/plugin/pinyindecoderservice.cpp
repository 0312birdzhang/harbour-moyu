// SPDX-License-Identifier: GPL-3.0-or-later
#include "pinyindecoderservice.h"
#include "rimebackend.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaMethod>

namespace {

QPointer<RimeBackend> keyboardBackend;

RimeBackend *acquireKeyboardBackend(QString *error)
{
    if (keyboardBackend)
        return keyboardBackend;
    QDBusConnection connection = QDBusConnection::sessionBus();
    auto *bus = connection.interface();
    if (!bus) {
        *error = connection.lastError().message();
        return nullptr;
    }
    const QString service = QStringLiteral("xyz.birdzhang.moyu.Rime");
    if (bus->isServiceRegistered(service).value()) {
        auto request = QDBusMessage::createMethodCall(service, QStringLiteral("/Rime"), service,
                                                     QStringLiteral("releaseForKeyboard"));
        const auto reply = connection.call(request, QDBus::Block, 60000);
        if ((reply.type() != QDBusMessage::ReplyMessage || !reply.arguments().value(0).toBool()) &&
                bus->isServiceRegistered(service).value()) {
            *error = QStringLiteral("Could not release the existing Rime engine: ") + reply.errorMessage();
            return nullptr;
        }
    }
    auto *backend = new RimeBackend(QCoreApplication::instance());
    const auto registration = bus->registerService(service,
                QDBusConnectionInterface::ReplaceExistingService, QDBusConnectionInterface::DontAllowReplacement);
    if (!registration.isValid() || registration.value() != QDBusConnectionInterface::ServiceRegistered) {
        *error = QStringLiteral("Could not acquire the Rime service name");
        delete backend;
        return nullptr;
    }
    if (!connection.registerObject(QStringLiteral("/Rime"), backend, QDBusConnection::ExportAllSlots)) {
        *error = QStringLiteral("Could not export the keyboard Rime engine");
        connection.unregisterService(service);
        delete backend;
        return nullptr;
    }
    QObject::connect(backend, &RimeBackend::ownershipReleased, backend, [connection, service, backend]() mutable {
        connection.unregisterObject(QStringLiteral("/Rime"));
        connection.unregisterService(service);
        backend->deleteLater();
    });
    keyboardBackend = backend;
    return backend;
}

} // namespace

MoyuPinyinDecoderService::MoyuPinyinDecoderService(QObject *parent) : QObject(parent)
{
}

bool MoyuPinyinDecoderService::inProcess() const
{
    return !localBackend.isNull();
}

QVariant MoyuPinyinDecoderService::localCall(const QString &method, const QVariantList &arguments)
{
    if (arguments.size() > 4) {
        lastError = QStringLiteral("Too many local Rime arguments");
        return QVariant();
    }
    QByteArray signature = method.toLatin1() + '(';
    QGenericArgument parameters[4];
    for (int index = 0; index < arguments.size(); ++index) {
        if (index)
            signature += ',';
        signature += arguments[index].typeName();
        parameters[index] = QGenericArgument(arguments[index].typeName(), arguments[index].constData());
    }
    signature += ')';
    const int index = localBackend->metaObject()->indexOfSlot(QMetaObject::normalizedSignature(signature.constData()));
    if (index < 0) {
        lastError = QStringLiteral("Unknown local Rime method: ") + method;
        qWarning().noquote() << lastError;
        return QVariant();
    }
    const QMetaMethod target = localBackend->metaObject()->method(index);
    QVariant result;
    QGenericReturnArgument returnArgument;
    if (target.returnType() != QMetaType::Void) {
        result = QVariant(target.returnType(), nullptr);
        returnArgument = QGenericReturnArgument(target.typeName(), result.data());
    }
    if (!target.invoke(localBackend, Qt::DirectConnection, returnArgument,
                       parameters[0], parameters[1], parameters[2], parameters[3])) {
        lastError = QStringLiteral("Could not invoke local Rime method: ") + method;
        qWarning().noquote() << lastError;
        return QVariant();
    }
    return result;
}

QVariant MoyuPinyinDecoderService::call(const QString &method, const QVariantList &arguments)
{
    lastError.clear();
    if (keyboardMode) {
        if (!directAttempted) {
            directAttempted = true;
            localBackend = acquireKeyboardBackend(&lastError);
            if (localBackend) {
                connect(localBackend, &QObject::destroyed, this, &MoyuPinyinDecoderService::inProcessChanged);
                emit inProcessChanged();
            } else {
                qWarning().noquote() << "Moyu keyboard could not acquire its local engine:" << lastError;
            }
        }
        if (localBackend)
            return localCall(method, arguments);
    }
    QDBusMessage request = QDBusMessage::createMethodCall(
                QStringLiteral("xyz.birdzhang.moyu.Rime"),
                QStringLiteral("/Rime"), QStringLiteral("xyz.birdzhang.moyu.Rime"), method);
    request.setArguments(arguments);
    const QDBusMessage reply = QDBusConnection::sessionBus().call(request, QDBus::Block, 60000);
    lastError.clear();
    if (reply.type() == QDBusMessage::ErrorMessage) {
        lastError = QString::fromUtf8("Rime 服务调用失败（%1）：%2")
                .arg(method, reply.errorMessage());
        qWarning().noquote() << lastError;
        return QVariant();
    }
    return reply.arguments().value(0);
}

QString MoyuPinyinDecoderService::dictionaryCall(const QString &method, const QVariantList &arguments)
{
    const QVariant result = call(method, arguments);
    return lastError.isEmpty() ? result.toString() : lastError;
}

bool MoyuPinyinDecoderService::init()
{
    return call(QStringLiteral("init")).toBool();
}

void MoyuPinyinDecoderService::setUserDictionary(bool enabled)
{
    call(QStringLiteral("setUserDictionary"), {enabled});
}

int MoyuPinyinDecoderService::search(const QString &spelling)
{
    return call(QStringLiteral("search"), {spelling}).toInt();
}

void MoyuPinyinDecoderService::resetSearch()
{
    call(QStringLiteral("resetSearch"));
}

QString MoyuPinyinDecoderService::candidateAt(int index)
{
    return call(QStringLiteral("candidateAt"), {index}).toString();
}

int MoyuPinyinDecoderService::chooceCandidate(int index)
{
    return call(QStringLiteral("chooceCandidate"), {index}).toInt();
}

int MoyuPinyinDecoderService::backspace()
{
    return call(QStringLiteral("backspace")).toInt();
}

QVariantMap MoyuPinyinDecoderService::compositionCall(const QString &method, const QVariantList &arguments)
{
    return QJsonDocument::fromJson(call(method, arguments).toString().toUtf8()).object().toVariantMap();
}

QVariantMap MoyuPinyinDecoderService::searchComposition(const QString &spelling, int pageSize)
{
    return compositionCall(QStringLiteral("searchComposition"), {spelling, pageSize});
}

QVariantMap MoyuPinyinDecoderService::backspaceComposition(int pageSize)
{
    return compositionCall(QStringLiteral("backspaceComposition"), {pageSize});
}

QVariantMap MoyuPinyinDecoderService::selectComposition(int index, int pageSize, int revision)
{
    return compositionCall(QStringLiteral("selectComposition"), {index, pageSize, revision});
}

QVariantMap MoyuPinyinDecoderService::candidatePage(int offset, int limit, int revision)
{
    return compositionCall(QStringLiteral("candidatePage"), {offset, limit, revision});
}

QString MoyuPinyinDecoderService::takeCommit()
{
    return call(QStringLiteral("takeCommit")).toString();
}

QString MoyuPinyinDecoderService::commitComposition(bool raw)
{
    return call(QStringLiteral("commitComposition"), {raw}).toString();
}

QString MoyuPinyinDecoderService::preeditText()
{
    return call(QStringLiteral("preeditText")).toString();
}

QString MoyuPinyinDecoderService::pinyinString(bool decoded)
{
    return call(QStringLiteral("pinyinString"), {decoded}).toString();
}

QStringList MoyuPinyinDecoderService::spellingStartPositions()
{
    return call(QStringLiteral("spellingStartPositions")).toStringList();
}

int MoyuPinyinDecoderService::getSplStart()
{
    return call(QStringLiteral("getSplStart")).toInt();
}

int MoyuPinyinDecoderService::fixedLength()
{
    return call(QStringLiteral("fixedLength")).toInt();
}

void MoyuPinyinDecoderService::flushCache()
{
    call(QStringLiteral("flushCache"));
}

QStringList MoyuPinyinDecoderService::predictionList(const QString &history, int fetchSize)
{
    return call(QStringLiteral("predictionList"), {history, fetchSize}).toStringList();
}

void MoyuPinyinDecoderService::recordPrediction(const QString &text)
{
    call(QStringLiteral("recordPrediction"), {text});
}

QString MoyuPinyinDecoderService::addUserPhrase(const QString &phrase, const QString &pinyin)
{
    return dictionaryCall(QStringLiteral("addUserPhrase"), {phrase, pinyin});
}

int MoyuPinyinDecoderService::userDictionaryEntryCount()
{
    return call(QStringLiteral("userDictionaryEntryCount")).toInt();
}

QVariantList MoyuPinyinDecoderService::userDictionaryEntries()
{
    QVariantList entries;
    const QStringList rows = userDictionaryEntryList();
    for (const QString &row : rows) {
        const int separator = row.indexOf(QLatin1Char('\t'));
        if (separator < 0)
            continue;
        QVariantMap entry;
        entry.insert(QStringLiteral("phrase"), row.left(separator));
        entry.insert(QStringLiteral("pinyin"), row.mid(separator + 1));
        entries.append(entry);
    }
    return entries;
}

QStringList MoyuPinyinDecoderService::userDictionaryEntryList()
{
    return call(QStringLiteral("userDictionaryEntryList")).toStringList();
}

QStringList MoyuPinyinDecoderService::userDictionaryEntryPage(int offset, int limit)
{
    return call(QStringLiteral("userDictionaryEntryPage"), {offset, limit}).toStringList();
}

QString MoyuPinyinDecoderService::removeUserPhrase(const QString &phrase, const QString &pinyin)
{
    return dictionaryCall(QStringLiteral("removeUserPhrase"), {phrase, pinyin});
}

QString MoyuPinyinDecoderService::updateUserPhrase(const QString &oldPhrase, const QString &oldPinyin,
                                              const QString &newPhrase, const QString &newPinyin)
{
    return dictionaryCall(QStringLiteral("updateUserPhrase"),
                          {oldPhrase, oldPinyin, newPhrase, newPinyin});
}

QString MoyuPinyinDecoderService::userDictionaryPath()
{
    return call(QStringLiteral("userDictionaryPath")).toString();
}

bool MoyuPinyinDecoderService::resetUserDictionary()
{
    return call(QStringLiteral("resetUserDictionary")).toBool();
}

bool MoyuPinyinDecoderService::reloadUserDictionaryIfChanged()
{
    return call(QStringLiteral("reloadUserDictionaryIfChanged")).toBool();
}

QString MoyuPinyinDecoderService::migrationStatus()
{
    return call(QStringLiteral("migrationStatus")).toString();
}

QString MoyuPinyinDecoderService::engineVersion()
{
    return call(QStringLiteral("engineVersion")).toString();
}

QString MoyuPinyinDecoderService::dictionaryFiles()
{
    return call(QStringLiteral("dictionaryFiles")).toString();
}

void MoyuPinyinDecoderService::manageDictionaryFile(const QString &operation, const QString &value, bool enabled)
{
    const QStringList methods = {QStringLiteral("importDictionaryFile"),
                                QStringLiteral("setDictionaryFileEnabled"),
                                QStringLiteral("removeDictionaryFile"),
                                QStringLiteral("exportDictionaryFile"),
                                QStringLiteral("exportUserDictionary")};
    if (!methods.contains(operation))
        return;
    QDBusMessage request = QDBusMessage::createMethodCall(
                QStringLiteral("xyz.birdzhang.moyu.Rime"), QStringLiteral("/Rime"),
                QStringLiteral("xyz.birdzhang.moyu.Rime"), operation);
    QVariantList arguments;
    if (operation != QStringLiteral("exportUserDictionary"))
        arguments.append(value);
    if (operation == QStringLiteral("setDictionaryFileEnabled"))
        arguments.append(enabled);
    request.setArguments(arguments);
    auto *watcher = new QDBusPendingCallWatcher(
                QDBusConnection::sessionBus().asyncCall(request, 240000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, operation](QDBusPendingCallWatcher *completed) {
        const QDBusPendingReply<QString> reply = *completed;
        QString response = reply.value();
        if (reply.isError()) {
            response = QString::fromUtf8(QJsonDocument(QJsonObject{
                {QStringLiteral("error"), QString::fromUtf8("Rime 服务调用失败。") + reply.error().message()}
            }).toJson(QJsonDocument::Compact));
        }
        emit dictionaryOperationFinished(operation, response);
        completed->deleteLater();
    });
}
