// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QVariantMap>

class Library;
class Player;
class QDBusServiceWatcher;
class QWindow;

// D-Bus types of the tray protocols (registered in Tray's constructor).
struct TrayPixmap
{
    int width = 0, height = 0;
    QByteArray argb; // ARGB32, network byte order
};
using TrayPixmaps = QList<TrayPixmap>;
struct TrayToolTip
{
    QString iconName;
    TrayPixmaps pixmaps;
    QString title, text;
};
struct TrayMenuLayout // (ia{sv}av)
{
    int id = 0;
    QVariantMap props;
    QVariantList children; // each a TrayMenuLayout
};
struct TrayMenuItem // (ia{sv})
{
    int id = 0;
    QVariantMap props;
};
struct TrayMenuEvent // (isvu)
{
    int id = 0;
    QString eventId;
    QDBusVariant data;
    uint timestamp = 0;
};
Q_DECLARE_METATYPE(TrayPixmap)
Q_DECLARE_METATYPE(TrayToolTip)
Q_DECLARE_METATYPE(TrayMenuLayout)
Q_DECLARE_METATYPE(TrayMenuItem)
Q_DECLARE_METATYPE(TrayMenuEvent)

// The symbol in the panel's tray and a notification on each new track
// (QML singleton "Tray"), both straight over D-Bus: StatusNotifierItem with a
// com.canonical.dbusmenu menu (KDE, Xfce, LXQt, Cinnamon, GNOME with the
// AppIndicator extension …) and org.freedesktop.Notifications. No widgets, no
// extra library. Where the desktop has no tray the symbol just does not appear.
class Tray : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY settingsChanged)       // symbol in the tray
    Q_PROPERTY(bool closeToTray READ closeToTray WRITE setCloseToTray NOTIFY settingsChanged) // closing the window keeps playing
    Q_PROPERTY(bool notify READ notify WRITE setNotify NOTIFY settingsChanged)          // notification on a new track
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)                    // a tray took the symbol

public:
    Tray(Player *player, Library *library, QObject *parent = nullptr);
    ~Tray() override;

    void setWindow(QWindow *window);
    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    bool closeToTray() const { return m_closeToTray; }
    void setCloseToTray(bool on);
    bool notify() const { return m_notify; }
    void setNotify(bool on);
    bool available() const { return m_available; }

    // called over D-Bus (adaptors)
    void activate();          // click: show or hide the window
    void setActivationToken(const QString &token) { m_activationToken = token; } // Wayland: for the next activate()
    void secondaryActivate(); // middle click: play/pause
    void scroll(int delta, const QString &orientation);
    TrayMenuLayout layout() const;
    void menuEvent(int id, const QString &eventId);
    QString title() const;
    TrayToolTip toolTip() const;
    TrayPixmaps icon() const { return m_icon; }
    QString status() const;
    uint revision() const { return m_revision; }

signals:
    void settingsChanged();
    void availableChanged();
    // adaptor signals
    void newToolTip();
    void newTitle();
    void newStatus(const QString &status);
    void layoutUpdated(uint revision, int parent);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override; // closing the window with closeToTray

private:
    void start();
    void stop();
    void registerWithWatcher();
    void onTrackChanged();
    void stateChanged();
    void maybeNotify();
    void showNotification(qint64 id, const QString &coverFile);

    Player *m_player;
    Library *m_library;
    QPointer<QWindow> m_window;
    QString m_activationToken;
    bool m_enabled = true;
    bool m_closeToTray = false;
    bool m_notify = true;
    bool m_available = false;
    bool m_running = false;
    bool m_quitting = false;
    QString m_service;
    QObject *m_itemHolder = nullptr;
    QObject *m_menuHolder = nullptr;
    QDBusServiceWatcher *m_watcher = nullptr;
    TrayPixmaps m_icon;
    uint m_revision = 1;
    uint m_notificationId = 0;
    qint64 m_lastNotified = 0;
    QElapsedTimer m_deactivated; // when the window lost the focus last
};

// org.kde.StatusNotifierItem
class TrayItemAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierItem")
    Q_PROPERTY(QString Category READ category)
    Q_PROPERTY(QString Id READ id)
    Q_PROPERTY(QString Title READ title)
    Q_PROPERTY(QString Status READ status)
    Q_PROPERTY(int WindowId READ windowId)
    Q_PROPERTY(QString IconName READ iconName)
    Q_PROPERTY(TrayPixmaps IconPixmap READ iconPixmap)
    Q_PROPERTY(QString OverlayIconName READ empty)
    Q_PROPERTY(TrayPixmaps OverlayIconPixmap READ noPixmaps)
    Q_PROPERTY(QString AttentionIconName READ empty)
    Q_PROPERTY(TrayPixmaps AttentionIconPixmap READ noPixmaps)
    Q_PROPERTY(QString AttentionMovieName READ empty)
    Q_PROPERTY(TrayToolTip ToolTip READ toolTip)
    Q_PROPERTY(bool ItemIsMenu READ itemIsMenu)
    Q_PROPERTY(QDBusObjectPath Menu READ menu)
    Q_PROPERTY(QString IconThemePath READ empty)

public:
    TrayItemAdaptor(QObject *holder, Tray *tray);
    QString category() const { return QStringLiteral("ApplicationStatus"); }
    QString id() const { return QStringLiteral("de.bauenundbooten.BuB3Player"); }
    QString title() const { return m_tray->title(); }
    QString status() const { return m_tray->status(); }
    int windowId() const { return 0; }
    QString iconName() const { return {}; } // the theme may not have it (AppImage): pixmaps instead
    TrayPixmaps iconPixmap() const { return m_tray->icon(); }
    TrayPixmaps noPixmaps() const { return {}; }
    QString empty() const { return {}; }
    TrayToolTip toolTip() const { return m_tray->toolTip(); }
    bool itemIsMenu() const { return false; }
    QDBusObjectPath menu() const { return QDBusObjectPath(QStringLiteral("/MenuBar")); }

public slots:
    void Activate(int x, int y) { m_tray->activate(); }
    void SecondaryActivate(int x, int y) { m_tray->secondaryActivate(); }
    void ContextMenu(int x, int y) {}
    void ProvideXdgActivationToken(const QString &token) { m_tray->setActivationToken(token); } // KDE, before Activate
    void Scroll(int delta, const QString &orientation) { m_tray->scroll(delta, orientation); }

signals:
    void NewTitle();
    void NewIcon();
    void NewToolTip();
    void NewStatus(const QString &status);

private:
    Tray *m_tray;
};

// com.canonical.dbusmenu
class TrayMenuAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.canonical.dbusmenu")
    Q_PROPERTY(uint Version READ version)
    Q_PROPERTY(QString TextDirection READ textDirection)
    Q_PROPERTY(QString Status READ status)
    Q_PROPERTY(QStringList IconThemePath READ iconThemePath)

public:
    TrayMenuAdaptor(QObject *holder, Tray *tray);
    uint version() const { return 3; }
    QString textDirection() const { return QStringLiteral("ltr"); }
    QString status() const { return QStringLiteral("normal"); }
    QStringList iconThemePath() const { return {}; }

public slots:
    uint GetLayout(int parentId, int recursionDepth, const QStringList &propertyNames, TrayMenuLayout &layout);
    QList<TrayMenuItem> GetGroupProperties(const QList<int> &ids, const QStringList &propertyNames);
    QDBusVariant GetProperty(int id, const QString &name);
    void Event(int id, const QString &eventId, const QDBusVariant &data, uint timestamp);
    QList<int> EventGroup(const QList<TrayMenuEvent> &events);
    bool AboutToShow(int id) { return false; }
    QList<int> AboutToShowGroup(const QList<int> &ids, QList<int> &idErrors);

signals:
    void LayoutUpdated(uint revision, int parent);

private:
    Tray *m_tray;
};
