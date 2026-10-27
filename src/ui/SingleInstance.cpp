// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/SingleInstance.h"

#include "core/Database.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QWindow>

#include <utility>

namespace {

const QString Service = QStringLiteral("de.bauenundbooten.BuB3Player"); // = desktop ID
const QString ObjectPath = QStringLiteral("/de/bauenundbooten/BuB3Player");
const QString AppInterface = QStringLiteral("org.freedesktop.Application");
const QString InfoInterface = QStringLiteral("de.bauenundbooten.BuB3Player");

// What a launcher hands to a started program for focus: Wayland and X11.
QVariantMap platformData()
{
    QVariantMap d;
    const QString wayland = qEnvironmentVariable("XDG_ACTIVATION_TOKEN");
    if (!wayland.isEmpty())
        d.insert(QStringLiteral("activation-token"), wayland);
    const QString x11 = qEnvironmentVariable("DESKTOP_STARTUP_ID");
    if (!x11.isEmpty())
        d.insert(QStringLiteral("desktop-startup-id"), x11);
    return d;
}

} // namespace

void raiseWindow(QWindow *window, const QString &token)
{
    if (!window)
        return;
    // Qt's Wayland backend takes the token from here for the next activation.
    if (!token.isEmpty())
        qputenv("XDG_ACTIVATION_TOKEN", token.toUtf8());
    // keeps maximised and full screen as they were
    if (window->windowStates() & Qt::WindowMinimized)
        window->setWindowStates(window->windowStates() & ~Qt::WindowMinimized);
    if (!window->isVisible())
        window->show();
    window->raise();
    window->requestActivate();
}

bool SingleInstance::forward(const QStringList &paths)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.interface() || !bus.interface()->isServiceRegistered(Service))
        return false;
    // Only a player with the same database is "the" player.
    QDBusMessage get = QDBusMessage::createMethodCall(Service, ObjectPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                      QStringLiteral("Get"));
    get << InfoInterface << QStringLiteral("Database");
    const QDBusMessage reply = bus.call(get, QDBus::Block, 3000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()
        || reply.arguments().first().value<QDBusVariant>().variant().toString() != Database::path())
        return false;

    QStringList uris;
    for (const QString &p : paths) {
        const QUrl url = p.startsWith(QLatin1String("file:")) ? QUrl(p) : QUrl::fromLocalFile(QFileInfo(p).absoluteFilePath());
        if (url.isLocalFile())
            uris << url.toString(QUrl::FullyEncoded);
    }
    QDBusMessage call = uris.isEmpty() ? QDBusMessage::createMethodCall(Service, ObjectPath, AppInterface, QStringLiteral("Activate"))
                                       : QDBusMessage::createMethodCall(Service, ObjectPath, AppInterface, QStringLiteral("Open"));
    if (!uris.isEmpty())
        call << uris;
    call << platformData();
    return bus.call(call, QDBus::Block, 3000).type() == QDBusMessage::ReplyMessage;
}

SingleInstance::SingleInstance(const QStringList &files, QObject *parent) : QObject(parent), m_waiting(files)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;
    auto *holder = new QObject(this);
    new ApplicationAdaptor(holder, this);
    new PlayerInfoAdaptor(holder);
    // Taken: another player with another database runs. Then this one is simply on its own.
    if (bus.registerObject(ObjectPath, holder) && !bus.registerService(Service))
        bus.unregisterObject(ObjectPath);
}

void SingleInstance::setWindow(QWindow *window)
{
    m_window = window;
    if (window && !m_waiting.isEmpty())
        emit openRequested(std::exchange(m_waiting, {}));
}

void SingleInstance::activate(const QVariantMap &platformData)
{
    raiseWindow(m_window, platformData.value(QStringLiteral("activation-token")).toString());
}

void SingleInstance::open(const QStringList &uris, const QVariantMap &platformData)
{
    QStringList paths;
    for (const QString &u : uris) {
        const QUrl url(u);
        if (url.isLocalFile())
            paths << url.toLocalFile();
    }
    activate(platformData);
    if (paths.isEmpty())
        return;
    if (m_window)
        emit openRequested(paths);
    else
        m_waiting += paths; // still starting: when the window is there

}

ApplicationAdaptor::ApplicationAdaptor(QObject *holder, SingleInstance *instance)
    : QDBusAbstractAdaptor(holder), m_instance(instance)
{
}

PlayerInfoAdaptor::PlayerInfoAdaptor(QObject *holder) : QDBusAbstractAdaptor(holder)
{
}

QString PlayerInfoAdaptor::database() const
{
    return Database::path();
}
