// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/Tray.h"

#include "core/CoverProvider.h"
#include "library/Library.h"
#include "player/Player.h"
#include "ui/SingleInstance.h"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QEvent>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSettings>
#include <QSvgRenderer>
#include <QWindow>
#include <QtConcurrent>
#include <QtEndian>

#include <utility>

// ---------------------------------------------------------------- D-Bus types

QDBusArgument &operator<<(QDBusArgument &a, const TrayPixmap &p)
{
    a.beginStructure();
    a << p.width << p.height << p.argb;
    a.endStructure();
    return a;
}
const QDBusArgument &operator>>(const QDBusArgument &a, TrayPixmap &p)
{
    a.beginStructure();
    a >> p.width >> p.height >> p.argb;
    a.endStructure();
    return a;
}
QDBusArgument &operator<<(QDBusArgument &a, const TrayToolTip &t)
{
    a.beginStructure();
    a << t.iconName << t.pixmaps << t.title << t.text;
    a.endStructure();
    return a;
}
const QDBusArgument &operator>>(const QDBusArgument &a, TrayToolTip &t)
{
    a.beginStructure();
    a >> t.iconName >> t.pixmaps >> t.title >> t.text;
    a.endStructure();
    return a;
}
QDBusArgument &operator<<(QDBusArgument &a, const TrayMenuLayout &l)
{
    a.beginStructure();
    a << l.id << l.props;
    a.beginArray(qMetaTypeId<QDBusVariant>());
    for (const QVariant &c : l.children)
        a << QDBusVariant(c);
    a.endArray();
    a.endStructure();
    return a;
}
const QDBusArgument &operator>>(const QDBusArgument &a, TrayMenuLayout &l)
{
    a.beginStructure();
    a >> l.id >> l.props;
    a.beginArray();
    l.children.clear();
    while (!a.atEnd()) {
        QDBusVariant v;
        a >> v;
        l.children << v.variant();
    }
    a.endArray();
    a.endStructure();
    return a;
}
QDBusArgument &operator<<(QDBusArgument &a, const TrayMenuItem &i)
{
    a.beginStructure();
    a << i.id << i.props;
    a.endStructure();
    return a;
}
const QDBusArgument &operator>>(const QDBusArgument &a, TrayMenuItem &i)
{
    a.beginStructure();
    a >> i.id >> i.props;
    a.endStructure();
    return a;
}
QDBusArgument &operator<<(QDBusArgument &a, const TrayMenuEvent &e)
{
    a.beginStructure();
    a << e.id << e.eventId << e.data << e.timestamp;
    a.endStructure();
    return a;
}
const QDBusArgument &operator>>(const QDBusArgument &a, TrayMenuEvent &e)
{
    a.beginStructure();
    a >> e.id >> e.eventId >> e.data >> e.timestamp;
    a.endStructure();
    return a;
}

namespace {

const QString WatcherService = QStringLiteral("org.kde.StatusNotifierWatcher");
const QString ItemPath = QStringLiteral("/StatusNotifierItem");
const QString MenuPath = QStringLiteral("/MenuBar");

enum MenuId { Root = 0, NowPlaying, Sep1, PlayPause, Next, Previous, Sep2, Favorite, Sep3, ShowWindow, Quit };

// The app symbol at the sizes panels ask for, ARGB32 in network byte order.
TrayPixmaps renderIcon()
{
    TrayPixmaps out;
    QSvgRenderer svg(QStringLiteral(":/icons/de.bauenundbooten.BuB3Player.svg"));
    for (int size : {16, 22, 24, 32, 48, 64}) {
        QImage img(size, size, QImage::Format_ARGB32);
        img.fill(Qt::transparent);
        QPainter p(&img);
        svg.render(&p);
        p.end();
        TrayPixmap px;
        px.width = size;
        px.height = size;
        px.argb.resize(size * size * 4);
        auto *dst = reinterpret_cast<quint32 *>(px.argb.data());
        for (int y = 0; y < size; ++y) {
            const auto *line = reinterpret_cast<const quint32 *>(img.constScanLine(y));
            for (int x = 0; x < size; ++x)
                dst[y * size + x] = qToBigEndian(line[x]);
        }
        out << px;
    }
    return out;
}

QVariantMap item(const QString &label, bool enabled = true)
{
    return {{QStringLiteral("label"), label}, {QStringLiteral("enabled"), enabled}, {QStringLiteral("visible"), true}};
}

QVariantMap separator()
{
    return {{QStringLiteral("type"), QStringLiteral("separator")}, {QStringLiteral("visible"), true}};
}

QString escapeMarkup(QString s)
{
    s.replace(QLatin1Char('&'), QLatin1String("&amp;"));
    s.replace(QLatin1Char('<'), QLatin1String("&lt;"));
    s.replace(QLatin1Char('>'), QLatin1String("&gt;"));
    return s;
}

} // namespace

// ---------------------------------------------------------------- Tray

Tray::Tray(Player *player, Library *library, QObject *parent)
    : QObject(parent), m_player(player), m_library(library)
{
    qDBusRegisterMetaType<TrayPixmap>();
    qDBusRegisterMetaType<TrayPixmaps>();
    qDBusRegisterMetaType<TrayToolTip>();
    qDBusRegisterMetaType<TrayMenuLayout>();
    qDBusRegisterMetaType<TrayMenuItem>();
    qDBusRegisterMetaType<QList<TrayMenuItem>>();
    qDBusRegisterMetaType<TrayMenuEvent>();
    qDBusRegisterMetaType<QList<TrayMenuEvent>>();
    qDBusRegisterMetaType<QList<int>>();

    QSettings s;
    m_enabled = s.value(QStringLiteral("tray/enabled"), true).toBool();
    m_closeToTray = s.value(QStringLiteral("tray/closeToTray"), false).toBool();
    m_notify = s.value(QStringLiteral("tray/notify"), true).toBool();

    connect(player, &Player::trackChanged, this, &Tray::onTrackChanged);
    connect(player, &Player::stateChanged, this, &Tray::stateChanged);
    connect(library, &Library::currentFavoriteChanged, this, [this] { emit layoutUpdated(++m_revision, 0); });
    // Quitting (menu, Ctrl+Q) first closes all windows: that close must not end in the tray.
    qApp->installEventFilter(this);
    if (m_enabled)
        start();
}

Tray::~Tray()
{
    stop();
}

void Tray::setWindow(QWindow *window)
{
    m_window = window;
    if (!window)
        return;
    window->installEventFilter(this);
    connect(window, &QWindow::activeChanged, this, [this] {
        if (m_window && !m_window->isActive())
            m_deactivated.start();
    });
}

bool Tray::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Quit && watched == qApp)
        m_quitting = true;
    // Only while the symbol is really in a tray: otherwise the window could not be brought back.
    if (event->type() == QEvent::Close && watched == m_window && m_closeToTray && m_available && !m_quitting) {
        event->ignore();
        m_window->hide();
        emit layoutUpdated(++m_revision, 0);
        return true;
    }
    return QObject::eventFilter(watched, event);
}

void Tray::setEnabled(bool on)
{
    if (m_enabled == on)
        return;
    m_enabled = on;
    QSettings().setValue(QStringLiteral("tray/enabled"), on);
    on ? start() : stop();
    emit settingsChanged();
}

void Tray::setCloseToTray(bool on)
{
    if (m_closeToTray == on)
        return;
    m_closeToTray = on;
    QSettings().setValue(QStringLiteral("tray/closeToTray"), on);
    emit settingsChanged();
}

void Tray::setNotify(bool on)
{
    if (m_notify == on)
        return;
    m_notify = on;
    QSettings().setValue(QStringLiteral("tray/notify"), on);
    emit settingsChanged();
}

void Tray::start()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (m_running || !bus.isConnected())
        return;
    if (m_icon.isEmpty())
        m_icon = renderIcon();
    m_itemHolder = new QObject(this);
    auto *item = new TrayItemAdaptor(m_itemHolder, this);
    m_menuHolder = new QObject(this);
    auto *menu = new TrayMenuAdaptor(m_menuHolder, this);
    connect(this, &Tray::newToolTip, item, &TrayItemAdaptor::NewToolTip);
    connect(this, &Tray::newTitle, item, &TrayItemAdaptor::NewTitle);
    connect(this, &Tray::newStatus, item, &TrayItemAdaptor::NewStatus);
    connect(this, &Tray::layoutUpdated, menu, &TrayMenuAdaptor::LayoutUpdated);
    bus.registerObject(ItemPath, m_itemHolder);
    bus.registerObject(MenuPath, m_menuHolder);
    m_service = QStringLiteral("org.kde.StatusNotifierItem-%1-1").arg(QCoreApplication::applicationPid());
    bus.registerService(m_service);
    m_running = true;

    // The panel may start (or restart) after us.
    m_watcher = new QDBusServiceWatcher(WatcherService, bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(m_watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, [this](const QString &, const QString &, const QString &owner) {
        if (owner.isEmpty()) {
            if (m_available) {
                m_available = false;
                emit availableChanged();
            }
        } else {
            registerWithWatcher();
        }
    });
    registerWithWatcher();
}

void Tray::stop()
{
    if (!m_running)
        return;
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.unregisterService(m_service);
    bus.unregisterObject(ItemPath);
    bus.unregisterObject(MenuPath);
    delete m_watcher;
    m_watcher = nullptr;
    delete m_itemHolder;
    m_itemHolder = nullptr;
    delete m_menuHolder;
    m_menuHolder = nullptr;
    m_running = false;
    if (m_available) {
        m_available = false;
        emit availableChanged();
    }
}

void Tray::registerWithWatcher()
{
    QDBusMessage call = QDBusMessage::createMethodCall(WatcherService, QStringLiteral("/StatusNotifierWatcher"), WatcherService,
                                                       QStringLiteral("RegisterStatusNotifierItem"));
    call << m_service;
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, 5000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const bool ok = !w->isError() && m_running;
        if (ok != m_available) {
            m_available = ok;
            emit availableChanged();
        }
    });
}

QString Tray::title() const
{
    return QStringLiteral("BuB3Player");
}

QString Tray::status() const
{
    return QStringLiteral("Active");
}

TrayToolTip Tray::toolTip() const
{
    TrayToolTip t;
    t.title = QStringLiteral("BuB3Player");
    if (m_player->currentId() > 0)
        t.text = escapeMarkup(m_player->title() + QStringLiteral(" – ") + m_player->artist())
                 + (m_player->playing() ? QString() : tr(" (Pause)"));
    return t;
}

TrayMenuLayout Tray::layout() const
{
    const bool has = m_player->currentId() > 0;
    const bool shown = m_window && m_window->isVisible() && m_window->visibility() != QWindow::Minimized;
    auto node = [](int id, const QVariantMap &props) {
        TrayMenuLayout l;
        l.id = id;
        l.props = props;
        return QVariant::fromValue(l);
    };
    QString now = has ? m_player->title() + QStringLiteral(" – ") + m_player->artist() : tr("Nichts in der Wiedergabe");
    if (now.size() > 60)
        now = now.left(58) + QStringLiteral(" …");
    TrayMenuLayout root;
    root.id = Root;
    root.props = {{QStringLiteral("children-display"), QStringLiteral("submenu")}};
    root.children = {
        node(NowPlaying, item(now, false)),
        node(Sep1, separator()),
        node(PlayPause, item(m_player->playing() ? tr("Pause") : tr("Abspielen"), has)),
        node(Next, item(tr("Nächster Titel"), has)),
        node(Previous, item(tr("Vorheriger Titel"), has)),
        node(Sep2, separator()),
        node(Favorite, item(m_library->currentFavorite() ? tr("Aus den Favoriten entfernen") : tr("Zu den Favoriten"), has)),
        node(Sep3, separator()),
        node(ShowWindow, item(shown ? tr("Fenster verstecken") : tr("Fenster zeigen"))),
        node(Quit, item(tr("Beenden"))),
    };
    return root;
}

void Tray::menuEvent(int id, const QString &eventId)
{
    if (eventId != QLatin1String("clicked"))
        return;
    switch (id) {
    case PlayPause: m_player->toggle(); break;
    case Next: m_player->next(); break;
    case Previous: m_player->previous(); break;
    case Favorite: m_library->toggleCurrentFavorite(); break;
    case ShowWindow: activate(); break;
    case Quit: QCoreApplication::quit(); break;
    default: break;
    }
}

void Tray::activate()
{
    const QString token = std::exchange(m_activationToken, {});
    if (!m_window)
        return;
    // A click into the panel takes the focus from the window first; a window that was
    // active a moment ago counts as in front.
    const bool front = m_window->isActive() || (m_deactivated.isValid() && m_deactivated.elapsed() < 600);
    const bool shown = m_window->isVisible() && m_window->visibility() != QWindow::Minimized;
    if (shown && front)
        m_window->hide(); // back into the tray
    else
        raiseWindow(m_window, token);
    emit layoutUpdated(++m_revision, 0);
}

void Tray::secondaryActivate()
{
    m_player->toggle();
}

void Tray::scroll(int delta, const QString &orientation)
{
    if (orientation.compare(QLatin1String("vertical"), Qt::CaseInsensitive) != 0 || delta == 0)
        return;
    m_player->setVolume(std::clamp(m_player->volume() + (delta > 0 ? 0.05 : -0.05), 0.0, 1.0));
}

void Tray::stateChanged()
{
    if (m_running) {
        emit newToolTip();
        emit layoutUpdated(++m_revision, 0);
    }
    maybeNotify();
}

void Tray::onTrackChanged()
{
    if (m_running) {
        emit newToolTip();
        emit layoutUpdated(++m_revision, 0);
    }
    maybeNotify();
}

void Tray::maybeNotify()
{
    const qint64 id = m_player->currentId();
    // Once per new track and only when it really plays (not for the restored track at
    // start), and only when the listener does not see the window anyway.
    if (!m_notify || id <= 0 || id == m_lastNotified || !m_player->playing())
        return;
    m_lastNotified = id;
    if (QGuiApplication::applicationState() == Qt::ApplicationActive)
        return;
    QPointer<Tray> self(this);
    (void)QtConcurrent::run([self, id] {
        const QString file = CoverProvider::cachedFile(id, 256);
        QMetaObject::invokeMethod(qApp, [self, id, file] {
            if (self && self->m_player->currentId() == id)
                self->showNotification(id, file);
        }, Qt::QueuedConnection);
    });
}

void Tray::showNotification(qint64 id, const QString &coverFile)
{
    QVariantMap hints{{QStringLiteral("desktop-entry"), QStringLiteral("de.bauenundbooten.BuB3Player")},
                      {QStringLiteral("category"), QStringLiteral("x-gnome.music")},
                      {QStringLiteral("transient"), true},
                      {QStringLiteral("urgency"), QVariant::fromValue<uchar>(0)}};
    if (!coverFile.isEmpty())
        hints.insert(QStringLiteral("image-path"), coverFile);
    QString body = escapeMarkup(m_player->artist());
    if (!m_player->album().isEmpty())
        body += QStringLiteral(" – ") + escapeMarkup(m_player->album());
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Notifications"),
                                                       QStringLiteral("/org/freedesktop/Notifications"),
                                                       QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Notify"));
    // replaces the previous one: skipping through an album leaves one notification, not ten
    call << QStringLiteral("BuB3Player") << m_notificationId << QStringLiteral("de.bauenundbooten.BuB3Player")
         << m_player->title() << body << QStringList() << hints << int(5000);
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, 5000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<uint> r = *w;
        if (!r.isError())
            m_notificationId = r.value();
    });
}

// ---------------------------------------------------------------- adaptors

TrayItemAdaptor::TrayItemAdaptor(QObject *holder, Tray *tray) : QDBusAbstractAdaptor(holder), m_tray(tray)
{
    setAutoRelaySignals(false);
}

TrayMenuAdaptor::TrayMenuAdaptor(QObject *holder, Tray *tray) : QDBusAbstractAdaptor(holder), m_tray(tray)
{
    setAutoRelaySignals(false);
}

uint TrayMenuAdaptor::GetLayout(int parentId, int recursionDepth, const QStringList &propertyNames, TrayMenuLayout &layout)
{
    const TrayMenuLayout root = m_tray->layout();
    if (parentId == Root) {
        layout = root;
        if (recursionDepth == 0)
            layout.children.clear();
    } else {
        for (const QVariant &c : root.children)
            if (c.value<TrayMenuLayout>().id == parentId)
                layout = c.value<TrayMenuLayout>();
    }
    return m_tray->revision();
}

QList<TrayMenuItem> TrayMenuAdaptor::GetGroupProperties(const QList<int> &ids, const QStringList &propertyNames)
{
    QList<TrayMenuItem> out;
    const TrayMenuLayout root = m_tray->layout();
    for (const QVariant &c : root.children) {
        const TrayMenuLayout l = c.value<TrayMenuLayout>();
        if (ids.isEmpty() || ids.contains(l.id))
            out << TrayMenuItem{l.id, l.props};
    }
    return out;
}

QDBusVariant TrayMenuAdaptor::GetProperty(int id, const QString &name)
{
    for (const QVariant &c : m_tray->layout().children) {
        const TrayMenuLayout l = c.value<TrayMenuLayout>();
        if (l.id == id)
            return QDBusVariant(l.props.value(name));
    }
    return QDBusVariant(QVariant(QString()));
}

void TrayMenuAdaptor::Event(int id, const QString &eventId, const QDBusVariant &, uint)
{
    // after the D-Bus call has returned: quitting inside it would cut the answer off
    QMetaObject::invokeMethod(m_tray, [tray = m_tray, id, eventId] { tray->menuEvent(id, eventId); }, Qt::QueuedConnection);
}

QList<int> TrayMenuAdaptor::EventGroup(const QList<TrayMenuEvent> &events)
{
    for (const TrayMenuEvent &e : events)
        Event(e.id, e.eventId, e.data, e.timestamp);
    return {};
}

QList<int> TrayMenuAdaptor::AboutToShowGroup(const QList<int> &, QList<int> &idErrors)
{
    idErrors.clear();
    return {};
}
