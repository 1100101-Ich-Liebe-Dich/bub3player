// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/SingleInstance.h"

#include "core/Database.h"
#include "core/Http.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QWindow>

#include <cstdio>
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

SingleInstance::Forward SingleInstance::forward(const QStringList &paths)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.interface() || !bus.interface()->isServiceRegistered(Service))
        return NotRunning;
    // No answer at all: it hangs or is still busy starting. A second player on the
    // same database must not start then (two scans would undo each other).
    auto noAnswer = [](const QDBusMessage &m) {
        return m.type() == QDBusMessage::ErrorMessage
               && (m.errorName() == QLatin1String("org.freedesktop.DBus.Error.NoReply")
                   || m.errorName() == QLatin1String("org.freedesktop.DBus.Error.Timeout"));
    };
    // Only a player with the same database is "the" player.
    QDBusMessage get = QDBusMessage::createMethodCall(Service, ObjectPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                      QStringLiteral("Get"));
    get << InfoInterface << QStringLiteral("Database");
    // a player that is still starting answers once it is up (old computers: seconds)
    const QDBusMessage reply = bus.call(get, QDBus::Block, 30000);
    if (noAnswer(reply))
        return NoAnswer;
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()
        || reply.arguments().first().value<QDBusVariant>().variant().toString() != Database::path())
        return NotRunning;

    QStringList uris;
    for (const QString &p : paths) {
        // an internet address is a radio station (or its list), anything else a file
        const QUrl url = Http::isWeb(QUrl(p)) || p.startsWith(QLatin1String("file:")) ? QUrl(p) : QUrl::fromLocalFile(QFileInfo(p).absoluteFilePath());
        if (url.isLocalFile() || Http::isWeb(url))
            uris << url.toString(QUrl::FullyEncoded);
    }
    QDBusMessage call = uris.isEmpty() ? QDBusMessage::createMethodCall(Service, ObjectPath, AppInterface, QStringLiteral("Activate"))
                                       : QDBusMessage::createMethodCall(Service, ObjectPath, AppInterface, QStringLiteral("Open"));
    if (!uris.isEmpty())
        call << uris;
    call << platformData();
    const QDBusMessage answer = bus.call(call, QDBus::Block, 30000);
    return answer.type() == QDBusMessage::ReplyMessage ? Forwarded : noAnswer(answer) ? NoAnswer : NotRunning;
}

void SingleInstance::tell(const QString &text)
{
    std::fprintf(stderr, "%s\n", text.toLocal8Bit().constData());
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Notifications"),
                                                       QStringLiteral("/org/freedesktop/Notifications"),
                                                       QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Notify"));
    call << QStringLiteral("BuB3Player") << uint(0) << Service << QStringLiteral("BuB3Player") << text << QStringList()
         << QVariantMap{{QStringLiteral("desktop-entry"), Service}} << int(8000);
    QDBusConnection::sessionBus().call(call, QDBus::Block, 2000);
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
        else if (Http::isWeb(url))
            paths << u; // a radio station
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
