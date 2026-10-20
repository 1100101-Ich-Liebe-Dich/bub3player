// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/SystemDialogs.h"

#include "core/Paths.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
#include <QWindow>

Q_LOGGING_CATEGORY(lcDialogs, "halcyon.dialogs", QtWarningMsg)

// Portal file filter: a(sa(us)), each rule (0 = glob pattern, 1 = MIME type).
struct PortalRule
{
    uint type = 0;
    QString pattern;
};
struct PortalFilter
{
    QString name;
    QList<PortalRule> rules;
};
Q_DECLARE_METATYPE(PortalRule)
Q_DECLARE_METATYPE(PortalFilter)

QDBusArgument &operator<<(QDBusArgument &a, const PortalRule &r)
{
    a.beginStructure();
    a << r.type << r.pattern;
    a.endStructure();
    return a;
}
const QDBusArgument &operator>>(const QDBusArgument &a, PortalRule &r)
{
    a.beginStructure();
    a >> r.type >> r.pattern;
    a.endStructure();
    return a;
}
QDBusArgument &operator<<(QDBusArgument &a, const PortalFilter &f)
{
    a.beginStructure();
    a << f.name << f.rules;
    a.endStructure();
    return a;
}
const QDBusArgument &operator>>(const QDBusArgument &a, PortalFilter &f)
{
    a.beginStructure();
    a >> f.name >> f.rules;
    a.endStructure();
    return a;
}

namespace {

const QString PortalService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString PortalPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString FileChooser = QStringLiteral("org.freedesktop.portal.FileChooser");

QString startFolder(const QVariant &pathOrUrl)
{
    const QString wanted = Paths::localPath(pathOrUrl);
    if (!wanted.isEmpty() && QFileInfo(wanted).isDir())
        return wanted;
    const QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    return QFileInfo(music).isDir() ? music : QDir::homePath();
}

// Our window's X11 id, so the dialog sits on top of it (0 on Wayland: that would need
// the surface exported first; the dialog still opens, just not tied to the window).
WId x11Window()
{
    if (QGuiApplication::platformName() != QLatin1String("xcb"))
        return 0;
    QWindow *w = QGuiApplication::focusWindow();
    if (!w)
        for (QWindow *t : QGuiApplication::topLevelWindows())
            if (t->isVisible()) {
                w = t;
                break;
            }
    return w ? w->winId() : 0;
}

// The extensions of a filter ("*.m3u8" -> "m3u8"); none for "*".
QStringList extensions(const QVariant &filter)
{
    QStringList out;
    for (const QString &p : filter.toMap().value(QStringLiteral("patterns")).toStringList())
        if (p.startsWith(QLatin1String("*.")) && p.size() > 2)
            out << p.mid(2).toLower();
    return out;
}

QList<PortalFilter> portalFilters(const QVariantList &filters)
{
    QList<PortalFilter> out;
    for (const QVariant &v : filters) {
        const QVariantMap m = v.toMap();
        PortalFilter f;
        f.name = m.value(QStringLiteral("name")).toString();
        for (const QString &p : m.value(QStringLiteral("patterns")).toStringList())
            f.rules.append({0, p});
        out.append(f);
    }
    return out;
}

// The external helpers are programs of the system: what the AppImage sets for itself
// (GStreamer, GIO modules, Qt plugin and QML paths) must not reach them. The
// listener's own settings (input method, scaling, Qt theme) stay.
QProcessEnvironment toolEnvironment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (env.contains(QStringLiteral("APPDIR"))) {
        for (const QString &key : env.keys()) {
            if (key.startsWith(QLatin1String("GST_")) || key == QLatin1String("GIO_MODULE_DIR") || key == QLatin1String("QT_PLUGIN_PATH")
                || key == QLatin1String("QML_IMPORT_PATH") || key == QLatin1String("QML2_IMPORT_PATH")
                || key == QLatin1String("LD_LIBRARY_PATH"))
                env.remove(key);
        }
    }
    return env;
}

// kdialog on KDE, zenity elsewhere; the other one if the first is missing.
QStringList toolOrder()
{
    const bool kde = qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(QLatin1String("KDE"), Qt::CaseInsensitive);
    QStringList order = kde ? QStringList{QStringLiteral("kdialog"), QStringLiteral("zenity")}
                            : QStringList{QStringLiteral("zenity"), QStringLiteral("kdialog")};
    QStringList found;
    for (const QString &t : order)
        if (!QStandardPaths::findExecutable(t).isEmpty())
            found << t;
    return found;
}

} // namespace

SystemDialogs::SystemDialogs(QObject *parent)
    : QObject(parent)
{
    qDBusRegisterMetaType<PortalRule>();
    qDBusRegisterMetaType<QList<PortalRule>>();
    qDBusRegisterMetaType<PortalFilter>();
    qDBusRegisterMetaType<QList<PortalFilter>>();
}

bool SystemDialogs::busy()
{
    // A second click while a dialog is still coming (or open, on Wayland it is not modal)
    // opens no second one. Two minutes at most: a portal that crashed must not block us.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_busySince > 0 && now - m_busySince < 120000)
        return true;
    m_busySince = now;
    return false;
}

void SystemDialogs::chooseFolder(const QString &title, const QVariant &folder, const QJSValue &done)
{
    if (!busy())
        viaPortal({Folder, title, startFolder(folder), {}, {}, done});
}

void SystemDialogs::openFile(const QString &title, const QVariant &folder, const QVariantList &filters, const QJSValue &done)
{
    if (!busy())
        viaPortal({Open, title, startFolder(folder), {}, filters, done});
}

void SystemDialogs::saveFile(const QString &title, const QVariant &folder, const QString &name, const QVariantList &filters,
                             const QJSValue &done)
{
    if (!busy())
        viaPortal({Save, title, startFolder(folder), name, filters, done});
}

void SystemDialogs::viaPortal(const Request &r)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        viaTool(r, 0);
        return;
    }
    // Folders need version 3 of the file chooser (2019); older ones would offer files.
    QDBusMessage get = QDBusMessage::createMethodCall(PortalService, PortalPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                      QStringLiteral("Get"));
    get << FileChooser << QStringLiteral("version");
    auto *watch = new QDBusPendingCallWatcher(bus.asyncCall(get, 5000), this);
    connect(watch, &QDBusPendingCallWatcher::finished, this, [this, r, bus](QDBusPendingCallWatcher *w) mutable {
        w->deleteLater();
        QDBusPendingReply<QDBusVariant> version = *w;
        if (version.isError() || (r.kind == Folder && version.value().variant().toUInt() < 3)) {
            qCDebug(lcDialogs) << "no file chooser portal:" << version.error().message();
            viaTool(r, 0);
            return;
        }
        // The answer comes as a signal on a request object whose path we can know
        // beforehand: subscribe first, so it cannot be missed.
        const QString token = QStringLiteral("halcyon%1").arg(++m_counter);
        QString sender = bus.baseService().mid(1);
        sender.replace(QLatin1Char('.'), QLatin1Char('_'));
        const QString path = PortalPath + QStringLiteral("/request/") + sender + QLatin1Char('/') + token;
        bus.connect(PortalService, path, QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"), this,
                    SLOT(portalResponse(uint, QVariantMap)));
        m_waiting.insert(path, r);

        QVariantMap options;
        options.insert(QStringLiteral("handle_token"), token);
        options.insert(QStringLiteral("modal"), true);
        options.insert(QStringLiteral("current_folder"), QVariant::fromValue(QFile::encodeName(r.folder) + '\0'));
        if (r.kind == Folder)
            options.insert(QStringLiteral("directory"), true);
        if (r.kind == Save && !r.name.isEmpty())
            options.insert(QStringLiteral("current_name"), r.name);
        if (!r.filters.isEmpty())
            options.insert(QStringLiteral("filters"), QVariant::fromValue(portalFilters(r.filters)));
        QDBusMessage call = QDBusMessage::createMethodCall(PortalService, PortalPath, FileChooser,
                                                           r.kind == Save ? QStringLiteral("SaveFile") : QStringLiteral("OpenFile"));
        const WId win = x11Window();
        call << (win ? QStringLiteral("x11:") + QString::number(win, 16) : QString()) << r.title << options;
        m_waiting[path].asked = QDateTime::currentMSecsSinceEpoch();
        auto *open = new QDBusPendingCallWatcher(bus.asyncCall(call, 10000), this);
        connect(open, &QDBusPendingCallWatcher::finished, this, [this, path, bus](QDBusPendingCallWatcher *w) mutable {
            w->deleteLater();
            QDBusPendingReply<QDBusObjectPath> handle = *w;
            if (handle.isError()) {
                qCDebug(lcDialogs) << "portal refused:" << handle.error().message();
                bus.disconnect(PortalService, path, QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"), this,
                               SLOT(portalResponse(uint, QVariantMap)));
                if (m_waiting.contains(path))
                    viaTool(m_waiting.take(path), 0);
                return;
            }
            // very old portals choose the path themselves
            const QString actual = handle.value().path();
            if (actual != path && m_waiting.contains(path)) {
                bus.disconnect(PortalService, path, QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"), this,
                               SLOT(portalResponse(uint, QVariantMap)));
                m_waiting.insert(actual, m_waiting.take(path));
                bus.connect(PortalService, actual, QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"), this,
                            SLOT(portalResponse(uint, QVariantMap)));
            }
        });
    });
}

void SystemDialogs::portalResponse(uint response, const QVariantMap &results)
{
    const QString path = message().path();
    QDBusConnection::sessionBus().disconnect(PortalService, path, QStringLiteral("org.freedesktop.portal.Request"),
                                             QStringLiteral("Response"), this, SLOT(portalResponse(uint, QVariantMap)));
    if (!m_waiting.contains(path))
        return;
    Request r = m_waiting.take(path);
    // 2 = "ended in some other way": closing the window, but also a backend that cannot
    // show anything (wrong desktop). An answer within a moment means no dialog was there.
    if (response == 2 && QDateTime::currentMSecsSinceEpoch() - r.asked < 700) {
        qCWarning(lcDialogs) << "the file chooser portal showed no dialog, trying kdialog/zenity";
        viaTool(r, 0);
        return;
    }
    const QStringList uris = results.value(QStringLiteral("uris")).toStringList();
    if (response != 0 || uris.isEmpty()) {
        finish(r, QString());
        return;
    }
    const QUrl url(uris.first());
    if (!url.isLocalFile())
        qCWarning(lcDialogs) << "not a local file, ignored:" << url.toString();
    if (r.kind == Save) { // the filter chosen in the dialog decides the extension
        const PortalFilter current = qdbus_cast<PortalFilter>(results.value(QStringLiteral("current_filter")));
        for (int i = 0; i < r.filters.size(); ++i)
            if (r.filters.at(i).toMap().value(QStringLiteral("name")).toString() == current.name)
                r.chosenFilter = i;
    }
    finish(r, url.isLocalFile() ? url.toLocalFile() : QString());
}

void SystemDialogs::viaTool(const Request &r, int toolIndex)
{
    const QStringList tools = toolOrder();
    if (toolIndex >= tools.size()) {
        finish(r, QVariant()); // none: the caller shows Qt's own dialog
        return;
    }
    const QString tool = tools.at(toolIndex);
    const WId win = x11Window();
    const QString winId = win ? QString::number(win) : QString();

    QStringList args;
    const QString start = r.kind == Save ? r.folder + QLatin1Char('/') + r.name : r.folder + QLatin1Char('/');
    if (tool == QLatin1String("kdialog")) {
        if (!winId.isEmpty())
            args << QStringLiteral("--attach") << winId;
        args << QStringLiteral("--title") << r.title;
        if (r.kind == Folder) {
            args << QStringLiteral("--getexistingdirectory") << r.folder;
        } else {
            QStringList filter; // "*.m3u *.m3u8|Playlists\n*|Alle Dateien"
            for (const QVariant &v : r.filters) {
                const QVariantMap m = v.toMap();
                filter << m.value(QStringLiteral("patterns")).toStringList().join(QLatin1Char(' ')) + QLatin1Char('|')
                              + m.value(QStringLiteral("name")).toString();
            }
            args << (r.kind == Save ? QStringLiteral("--getsavefilename") : QStringLiteral("--getopenfilename")) << start;
            if (!filter.isEmpty())
                args << filter.join(QLatin1Char('\n'));
        }
    } else {
        if (!winId.isEmpty())
            args << QStringLiteral("--attach=") + winId;
        args << QStringLiteral("--file-selection") << QStringLiteral("--title=") + r.title << QStringLiteral("--filename=") + start;
        if (r.kind == Folder)
            args << QStringLiteral("--directory");
        if (r.kind == Save)
            args << QStringLiteral("--save") << QStringLiteral("--confirm-overwrite");
        for (const QVariant &v : r.filters) {
            const QVariantMap m = v.toMap();
            args << QStringLiteral("--file-filter=") + m.value(QStringLiteral("name")).toString() + QStringLiteral(" | ")
                        + m.value(QStringLiteral("patterns")).toStringList().join(QLatin1Char(' '));
        }
    }

    auto *p = new QProcess(this);
    p->setProcessEnvironment(toolEnvironment());
    connect(p, &QProcess::errorOccurred, this, [this, p, r, toolIndex](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        p->deleteLater();
        viaTool(r, toolIndex + 1);
    });
    const qint64 started = QDateTime::currentMSecsSinceEpoch();
    connect(p, &QProcess::finished, this, [this, p, r, toolIndex, started](int code, QProcess::ExitStatus status) {
        p->deleteLater();
        QString out = QString::fromLocal8Bit(p->readAllStandardOutput());
        if (out.endsWith(QLatin1Char('\n'))) // only the line end: names may start or end with blanks
            out.chop(1);
        // 1 is "cancelled", but also zenity without a display, right away
        const bool quick = QDateTime::currentMSecsSinceEpoch() - started < 500;
        if (status == QProcess::NormalExit && code == 0 && !out.isEmpty())
            finish(r, out);
        else if (status == QProcess::NormalExit && code == 1 && !quick)
            finish(r, QString());
        else
            viaTool(r, toolIndex + 1); // broken: the next one
    });
    p->start(tool, args);
}

void SystemDialogs::finish(Request r, const QVariant &result)
{
    m_busySince = 0;
    if (!r.done.isCallable())
        return;
    QString path = result.toString();
    // Saving: the dialogs of GNOME and zenity add no extension. Add the one of the chosen
    // filter; the dialog then asked about another name, so the caller has to ask again
    // before overwriting ("confirmed" = false).
    bool confirmed = true;
    if (r.kind == Save && !path.isEmpty() && !r.filters.isEmpty()) {
        QStringList all;
        for (const QVariant &f : std::as_const(r.filters))
            all << extensions(f);
        const QString suffix = QFileInfo(path).suffix().toLower();
        const QStringList chosen = extensions(r.filters.value(std::max(0, r.chosenFilter)));
        if (!all.contains(suffix) && !chosen.isEmpty()) {
            path += QLatin1Char('.') + chosen.first();
            confirmed = !QFileInfo::exists(path);
        }
    }
    const QJSValue arg = result.isNull() ? QJSValue(QJSValue::NullValue) : QJSValue(path);
    const QJSValue ret = r.done.call({arg, QJSValue(confirmed)});
    if (ret.isError())
        qCWarning(lcDialogs) << "callback failed:" << ret.toString();
}
