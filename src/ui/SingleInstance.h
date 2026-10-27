// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDBusAbstractAdaptor>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantMap>

class QWindow;

// Brings a window to the front (from the tray, minimised or behind others).
// `token`: the activation token of the starter (Wayland), if one came along.
void raiseWindow(QWindow *window, const QString &token = {});

// One player per database: a second start ("Öffnen mit" in the file manager, a
// click on the starter) hands its files to the running player and ends. Over
// D-Bus as org.freedesktop.Application at the desktop ID, so file managers and
// launchers know the interface. A player with another database (tests, --db)
// leaves the running one alone.
class SingleInstance : public QObject
{
    Q_OBJECT

public:
    // Another player with this database runs: it got `paths` (or comes to the
    // front without), this one should end. Paths may be relative or file URLs.
    enum Forward { NotRunning, Forwarded, NoAnswer };
    static Forward forward(const QStringList &paths);
    // For a start from the desktop, where nobody reads the terminal: a notification too.
    static void tell(const QString &text);

    // `files`: from the command line, opened as soon as the window is there
    explicit SingleInstance(const QStringList &files, QObject *parent = nullptr);
    void setWindow(QWindow *window);

    // called over D-Bus
    void activate(const QVariantMap &platformData);
    void open(const QStringList &uris, const QVariantMap &platformData);

signals:
    // local paths of files and folders to play (from a second start or the command line)
    void openRequested(const QStringList &paths);

private:
    QPointer<QWindow> m_window;
    QStringList m_waiting; // files that came before the window
};

// org.freedesktop.Application
class ApplicationAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Application")

public:
    ApplicationAdaptor(QObject *holder, SingleInstance *instance);

public slots:
    void Activate(const QVariantMap &platformData) { m_instance->activate(platformData); }
    void Open(const QStringList &uris, const QVariantMap &platformData) { m_instance->open(uris, platformData); }
    void ActivateAction(const QString &, const QVariantList &, const QVariantMap &platformData) { m_instance->activate(platformData); }

private:
    SingleInstance *m_instance;
};

// de.bauenundbooten.BuB3Player: which database the running player uses
class PlayerInfoAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "de.bauenundbooten.BuB3Player")
    Q_PROPERTY(QString Database READ database)

public:
    explicit PlayerInfoAdaptor(QObject *holder);
    QString database() const;
};
