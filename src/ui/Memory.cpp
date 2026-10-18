#include "ui/Memory.h"

#include <QFile>
#include <QJSEngine>
#include <QQmlEngine>
#include <QQuickWindow>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

Memory::Memory(QObject *parent) : QObject(parent)
{
    m_poll.setInterval(2000);
    connect(&m_poll, &QTimer::timeout, this, &Memory::sample);
    m_soon.setSingleShot(true);
    connect(&m_soon, &QTimer::timeout, this, &Memory::trim);
    // A gentle housekeeping pass every few minutes.
    m_periodic.setInterval(3 * 60 * 1000);
    connect(&m_periodic, &QTimer::timeout, this, &Memory::trim);
    m_periodic.start();
    sample();
}

void Memory::configureAllocator()
{
#if defined(__GLIBC__)
    // Every thread (scanner, cover loader, GStreamer, render thread …) would
    // otherwise get its own 64 MB malloc arena whose freed pages are rarely
    // returned. Two arenas are plenty for this workload.
    mallopt(M_ARENA_MAX, 2);
    // Return free memory at the top of the heap sooner.
    mallopt(M_TRIM_THRESHOLD, 8 * 1024 * 1024);
#endif
}

void Memory::setWindow(QQuickWindow *w)
{
    m_window = w;
    if (!w)
        return;
    // Going to the background or minimising is a good moment to tidy up.
    connect(w, &QWindow::activeChanged, this, [this] {
        if (m_window && !m_window->isActive())
            trimSoon(4000);
    });
    connect(w, &QWindow::visibilityChanged, this, [this](QWindow::Visibility v) {
        if (v == QWindow::Minimized || v == QWindow::Hidden)
            trimSoon(1000);
    });
}

void Memory::setWatching(bool on)
{
    if (on == m_poll.isActive())
        return;
    if (on) {
        sample();
        m_poll.start();
    } else {
        m_poll.stop();
    }
    emit watchingChanged();
}

void Memory::trimSoon(int ms)
{
    // Do not keep postponing while the user switches themes in quick succession.
    if (!m_soon.isActive())
        m_soon.start(ms);
}

void Memory::trim()
{
    if (m_window) {
        if (QQmlEngine *e = qmlEngine(m_window))
            e->collectGarbage();
        // Drops unused scene graph resources: glyph caches, atlas textures, pipelines.
        m_window->releaseResources();
    }
#if defined(__GLIBC__)
    malloc_trim(0);
#endif
    sample();
}

void Memory::sample()
{
    // /proc/self/smaps_rollup: Rss (all) and anonymous memory (ours: heap, images, textures in RAM).
    QFile f(QStringLiteral("/proc/self/smaps_rollup"));
    int rss = 0, anon = 0;
    if (f.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> lines = f.readAll().split('\n');
        for (const QByteArray &l : lines) {
            if (l.startsWith("Rss:"))
                rss = int(l.mid(4).trimmed().split(' ').value(0).toLongLong() / 1024);
            else if (l.startsWith("Anonymous:"))
                anon = int(l.mid(10).trimmed().split(' ').value(0).toLongLong() / 1024);
        }
    }
    if (rss != m_rss || anon != m_private) {
        m_rss = rss;
        m_private = anon;
        emit updated();
    }
}
