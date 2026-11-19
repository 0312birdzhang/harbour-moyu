// SPDX-License-Identifier: GPL-3.0-or-later
#include "rimebackend.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QDebug>
#include <QTimer>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("harbour-moyu-rime"));
    RimeBackend backend;
    if (application.arguments().contains(QStringLiteral("--deploy")))
        return backend.deploy() ? 0 : 1;

    QDBusConnection connection = QDBusConnection::sessionBus();
    if (!connection.interface()) {
        qWarning() << "Could not connect to the session bus";
        return 1;
    }
    const auto registration = connection.interface()->registerService(QStringLiteral("xyz.birdzhang.moyu.Rime"),
                QDBusConnectionInterface::DontQueueService, QDBusConnectionInterface::AllowReplacement);
    if (!registration.isValid() || registration.value() != QDBusConnectionInterface::ServiceRegistered) {
        qWarning() << "Could not register the Rime service";
        return 1;
    }
    if (!connection.registerObject(QStringLiteral("/Rime"), &backend,
                                   QDBusConnection::ExportAllSlots)) {
        qWarning() << "Could not export the Rime backend";
        return 1;
    }
    QObject::connect(&backend, &RimeBackend::ownershipReleased, &application, [&application]() {
        QTimer::singleShot(0, &application, &QCoreApplication::quit);
    });
    return application.exec();
}
