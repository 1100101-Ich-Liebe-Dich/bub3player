#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>

class QQuickWindow;

// Keeps the memory footprint small on machines with little RAM:
//  - caps glibc malloc arenas (threads otherwise each get their own heap),
//  - after a theme switch or when the window goes to the background it lets
//    the scene graph drop unused caches and returns freed heap to the system,
//  - reports the current usage (for Settings → Leistung).
class Memory : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int residentMb READ residentMb NOTIFY updated)   // everything incl. shared libraries
    Q_PROPERTY(int privateMb READ privateMb NOTIFY updated)     // the player's own memory
    Q_PROPERTY(bool watching READ watching WRITE setWatching NOTIFY watchingChanged)

public:
    explicit Memory(QObject *parent = nullptr);

    // Call before anything allocates much (start of main).
    static void configureAllocator();

    void setWindow(QQuickWindow *w);
    int residentMb() const { return m_rss; }
    int privateMb() const { return m_private; }
    bool watching() const { return m_poll.isActive(); }
    void setWatching(bool on);

    // Release caches and return free heap to the OS. Cheap; safe to call often.
    Q_INVOKABLE void trim();
    // Trim a moment later (after QML has destroyed the old objects).
    Q_INVOKABLE void trimSoon(int ms = 1500);

signals:
    void updated();
    void watchingChanged();

private:
    void sample();

    QPointer<QQuickWindow> m_window;
    QTimer m_poll, m_soon, m_periodic;
    int m_rss = 0, m_private = 0;
};
