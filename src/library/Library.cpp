#include "library/Library.h"

#include "core/Database.h"
#include "core/FolderWatcher.h"
#include "core/Scanner.h"
#include "library/FolderModel.h"
#include "library/GroupModel.h"
#include "library/PlaylistStore.h"
#include "library/QueryBuilder.h"
#include "library/TrackCache.h"
#include "library/TrackListModel.h"
#include "library/ViewRegistry.h"
#include "player/PlayQueue.h"
#include "player/Player.h"

#include <QDateTime>
#include <QSet>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QLocale>

Library *Library::s_instance = nullptr;

Library::Library(Player *player, PlayQueue *queue, QObject *parent)
    : QObject(parent), m_scanner(new Scanner(this)), m_player(player), m_queue(queue), m_watcher(new FolderWatcher(this))
{
    s_instance = this;
    m_roots = m_settings.value(QStringLiteral("library/roots")).toStringList();
    connect(m_scanner, &Scanner::libraryChanged, this, [this] {
        TrackCache::instance().invalidate();
        refreshStats();
        emit changed();
    });
    connect(m_scanner, &Scanner::finished, this, &Library::scanFinished);

    connect(m_watcher, &FolderWatcher::foldersChanged, this, &Library::foldersChanged);
    connect(m_watcher, &FolderWatcher::everythingChanged, this, [this] {
        m_checkAll = true;
        m_dirtyTimer.start(1500);
    });
    connect(m_watcher, &FolderWatcher::statusChanged, this, &Library::watchStatusChanged);
    m_dirtyTimer.setSingleShot(true);
    connect(&m_dirtyTimer, &QTimer::timeout, this, &Library::scanChangedFolders);
    m_periodic.setInterval(15 * 60 * 1000);
    connect(&m_periodic, &QTimer::timeout, this, &Library::periodicCheck);
    m_mounts.setInterval(30 * 1000);
    connect(&m_mounts, &QTimer::timeout, this, &Library::checkMounts);
    ViewRegistry::writeExampleIfMissing();
    reloadViews();
    refreshStats();
}

Library::~Library()
{
    s_instance = nullptr;
}

Library *Library::instance()
{
    return s_instance;
}

QString Library::viewsFile() const
{
    return ViewRegistry::userFile();
}

bool Library::scanOnStart() const
{
    return m_settings.value(QStringLiteral("library/scanOnStart"), true).toBool();
}

void Library::setScanOnStart(bool on)
{
    m_settings.setValue(QStringLiteral("library/scanOnStart"), on);
    emit settingsChanged();
}

int Library::scanThreads() const
{
    return m_settings.value(QStringLiteral("library/scanThreads"), 0).toInt();
}

void Library::setScanThreads(int n)
{
    m_settings.setValue(QStringLiteral("library/scanThreads"), std::clamp(n, 0, 32));
    emit settingsChanged();
}

void Library::startupScan(bool scan)
{
    if (!scan) {
        startWatching();
        return;
    }
    bool ftsStale = false;
    try {
        ftsStale = Database::ftsDirty(TrackCache::instance().db());
    } catch (...) {
    }
    // A previous import was interrupted: the scan also rebuilds the search index.
    if (!m_roots.isEmpty() && (scanOnStart() || ftsStale))
        rescan(); // watching starts when it is done
    else
        startWatching();
}

// ---------- folder watching ----------

bool Library::watchFolders() const
{
    return m_settings.value(QStringLiteral("library/watch"), true).toBool();
}

void Library::setWatchFolders(bool on)
{
    if (on == watchFolders())
        return;
    m_settings.setValue(QStringLiteral("library/watch"), on);
    emit settingsChanged();
    if (on) {
        // changes while it was off: one look at everything, then watch
        if (!m_scanner->running() && !m_roots.isEmpty())
            m_scanner->startSubset(m_roots, scanThreads());
        startWatching();
    } else {
        m_watcher->stop();
        m_watchRoots.clear();
        m_periodic.stop();
        m_mounts.stop();
        m_dirtyTimer.stop();
        m_dirtyFolders.clear();
    }
    emit watchStatusChanged();
}

QStringList Library::localRoots() const
{
    QStringList out;
    const QStringList net = m_watcher->networkRoots();
    for (const QString &r : m_roots)
        if (!net.contains(r))
            out << r;
    return out;
}

void Library::startWatching()
{
    if (!watchFolders() || m_roots.isEmpty()) {
        m_watcher->stop();
        m_watchRoots.clear();
        m_periodic.stop();
        m_mounts.stop();
        emit watchStatusChanged();
        return;
    }
    if (m_watchRoots != m_roots || !m_watcher->active()) {
        m_watcher->start(m_roots);
        m_watchRoots = m_roots;
    } else {
        m_watcher->resync(); // new folders from the scan, retry after a limit
    }
    m_periodic.start();
    m_mounts.start();
    emit watchStatusChanged();
}

void Library::foldersChanged(const QStringList &paths)
{
    for (const QString &p : paths)
        if (!m_dirtyFolders.contains(p))
            m_dirtyFolders << p;
    if (!m_firstDirty.isValid())
        m_firstDirty.start();
    // Wait until the copying is done, but not forever (a long copy job).
    m_dirtyTimer.start(m_firstDirty.elapsed() > 10000 ? 0 : 1500);
}

void Library::scanChangedFolders()
{
    if (m_scanner->running())
        return; // scanFinished comes back here
    if (m_checkAll || m_dirtyFolders.size() > 300) {
        m_checkAll = false;
        m_dirtyFolders.clear();
        m_firstDirty.invalidate();
        const QStringList local = localRoots();
        if (!local.isEmpty())
            m_scanner->startSubset(local, scanThreads());
        return;
    }
    if (!m_subsetQueue.isEmpty()) {
        const QStringList roots = m_subsetQueue;
        m_subsetQueue.clear();
        m_scanner->startSubset(roots, scanThreads());
        return;
    }
    if (m_dirtyFolders.isEmpty())
        return;
    const QStringList folders = m_dirtyFolders;
    m_dirtyFolders.clear();
    m_firstDirty.invalidate();
    m_scanner->startFolders(m_roots, folders);
}

void Library::periodicCheck()
{
    // What inotify cannot see: network shares, and folders over the watch limit.
    QStringList roots = m_watcher->networkRoots();
    if (m_watcher->limited())
        roots += localRoots();
    for (const QString &r : std::as_const(roots))
        if (!m_subsetQueue.contains(r) && !m_missing.contains(r))
            m_subsetQueue << r;
    if (!m_subsetQueue.isEmpty())
        m_dirtyTimer.start(0);
}

void Library::checkMounts()
{
    // A drive that comes back (USB disk plugged in, share mounted) gets a look
    // of its own; one that goes away keeps everything (see Scanner).
    for (const QString &r : std::as_const(m_roots)) {
        const QFileInfo fi(r);
        const bool there = fi.isDir() && fi.isReadable() && fi.isExecutable();
        if (!there) {
            m_missing.insert(r);
        } else if (m_missing.remove(r)) {
            if (!m_subsetQueue.contains(r))
                m_subsetQueue << r;
            m_dirtyTimer.start(0);
        }
    }
}

QString Library::watchStatus() const
{
    const QLocale de(QLocale::German);
    if (!watchFolders())
        return tr("Aus. Änderungen findet der Scan beim Start oder „Jetzt scannen“.");
    if (m_roots.isEmpty())
        return {};
    QStringList parts;
    if (m_watcher->active() && !localRoots().isEmpty()) {
        if (m_watcher->limited())
            parts << tr("Überwacht %1 von %2 Ordnern, mehr erlaubt das System gerade nicht. Der Rest wird alle 15 Minuten geprüft.")
                         .arg(de.toString(m_watcher->watched()), de.toString(std::max(m_watcher->total(), m_watcher->watched())));
        else
            parts << tr("Überwacht %1 Ordner: Neue, geänderte und gelöschte Dateien sind nach wenigen Sekunden in der Bibliothek.")
                         .arg(de.toString(m_watcher->watched()));
    }
    const QStringList net = m_watcher->networkRoots();
    if (!net.isEmpty())
        parts << tr("Netzlaufwerke werden alle 15 Minuten geprüft (%1).").arg(net.join(QStringLiteral(", ")));
    if (!m_missing.isEmpty())
        parts << tr("Sobald %1 wieder da ist, wird es eingelesen.").arg(QStringList(m_missing.values()).join(QStringLiteral(", ")));
    return parts.join(QLatin1Char(' '));
}

QString Library::watchHint() const
{
    if (!watchFolders() || !m_watcher->limited())
        return {};
    const qint64 want = std::max<qint64>(524288, (m_watcher->total() * 3 / 2 + 65535) / 65536 * 65536);
    return QStringLiteral("echo fs.inotify.max_user_watches=%1 | sudo tee /etc/sysctl.d/90-bub3player.conf && sudo sysctl --system").arg(want);
}

void Library::scanFinished()
{
    const Scanner::Stats &st = m_scanner->lastStats();
    if (st.mode == Scanner::Full) {
        emit message(m_scanner->summary());
        m_missing = QSet<QString>(st.missingRoots.begin(), st.missingRoots.end());
        if (!st.cancelled)
            startWatching();
    } else {
        // Small automatic scans: say what changed, if anything did.
        const QLocale de(QLocale::German);
        QStringList parts;
        if (st.added > 0)
            parts << (st.added == 1 ? tr("1 Titel neu") : tr("%1 Titel neu").arg(de.toString(st.added)));
        if (st.removed > 0)
            parts << (st.removed == 1 ? tr("1 Titel entfernt") : tr("%1 Titel entfernt").arg(de.toString(st.removed)));
        if (!parts.isEmpty())
            emit message(tr("Bibliothek aktualisiert: %1").arg(parts.join(QStringLiteral(", "))));
        if (watchFolders())
            m_watcher->addTrees(st.scanned);
    }
    if (auto *p = PlaylistStore::instance())
        p->relink(); // tracks that came back (drive plugged in, file moved)
    if (!m_dirtyFolders.isEmpty() || !m_subsetQueue.isEmpty() || m_checkAll)
        m_dirtyTimer.start(500);
    emit watchStatusChanged();
}

void Library::addRoot(const QVariant &pathOrUrl)
{
    QString path = pathOrUrl.typeId() == QMetaType::QUrl ? pathOrUrl.toUrl().toLocalFile() : pathOrUrl.toString();
    if (path.startsWith(QLatin1String("file://")))
        path = QUrl(path).toLocalFile();
    path = QDir::cleanPath(path);
    if (path.isEmpty() || !QFileInfo(path).isDir()) {
        emit message(tr("Ordner nicht gefunden: %1").arg(path));
        return;
    }
    for (const QString &r : std::as_const(m_roots)) {
        if (path == r || path.startsWith(r + QLatin1Char('/'))) {
            emit message(tr("Ist bereits Teil der Bibliothek: %1").arg(r));
            return;
        }
    }
    m_roots << path;
    m_settings.setValue(QStringLiteral("library/roots"), m_roots);
    emit rootsChanged();
    rescan();
}

void Library::removeRoot(const QString &path)
{
    if (!m_roots.removeOne(path))
        return;
    m_settings.setValue(QStringLiteral("library/roots"), m_roots);
    emit rootsChanged();
    // The next scan prunes folders that no longer belong to any root.
    rescan();
}

void Library::rescan()
{
    if (m_scanner->running())
        return;
    m_scanner->start(m_roots, scanThreads());
}

void Library::reloadViews()
{
    m_viewsError.clear();
    m_views = ViewRegistry::builtIn() + ViewRegistry::userViews(&m_viewsError);
    if (auto *p = PlaylistStore::instance())
        m_views += p->views();
    emit viewsChanged();
}

void Library::openViewsFolder()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(ViewRegistry::userFile()).absolutePath()));
}

QVariantMap Library::viewById(const QString &id) const
{
    for (const QVariant &v : m_views) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("id")).toString() == id)
            return m;
    }
    return {};
}

void Library::notifyExternalChange()
{
    TrackCache::instance().invalidate();
    refreshStats();
    emit changed();
    if (auto *p = PlaylistStore::instance())
        p->relink();
}

void Library::refreshStats()
{
    try {
        DbConnection &db = TrackCache::instance().db();
        m_trackCount = db.scalar(QueryBuilder::hideDuplicates() ? "SELECT COUNT(*) FROM tracks WHERE dup = 0"
                                                                : "SELECT COUNT(*) FROM tracks");
        m_folderCount = db.scalar("SELECT COUNT(*) FROM folders");
    } catch (const std::exception &e) {
        qWarning() << e.what();
    }
    emit statsChanged();
}

QVector<qint64> Library::idsForSpec(const QVariantMap &spec)
{
    QVector<qint64> ids;
    const QueryBuilder::Built q = QueryBuilder::trackIds(spec);
    if (!q.ok)
        return ids;
    try {
        Stmt s(TrackCache::instance().db(), q.sql);
        for (int i = 0; i < q.binds.size(); ++i)
            s.bind(i + 1, q.binds[i]);
        while (s.step())
            ids << s.i64(0);
    } catch (const std::exception &e) {
        qWarning() << e.what();
    }
    return ids;
}

void Library::playIds(QVector<qint64> ids, int start, bool shuffle)
{
    if (ids.isEmpty())
        return;
    // start < 0 with shuffle: begin with a random track.
    m_queue->setTracks(std::move(ids), start, shuffle);
    m_player->playCurrent();
}

void Library::playList(TrackListModel *model, int row, bool shuffle)
{
    if (!model)
        return;
    playIds(model->ids(), shuffle ? -1 : std::max(0, row), shuffle);
}

void Library::enqueueList(TrackListModel *model, int row, bool next)
{
    if (!model || row < 0 || row >= model->count())
        return;
    const QVector<qint64> ids = model->actionIds(row); // the selection, if the row is part of it
    next ? m_queue->insertNext(ids) : m_queue->append(ids);
    if (ids.size() == 1)
        emit message(next ? tr("Wird als Nächstes gespielt") : tr("Zur Warteschlange hinzugefügt"));
    else
        emit message(next ? tr("%1 Titel werden als Nächstes gespielt").arg(QLocale(QLocale::German).toString(ids.size()))
                          : tr("%1 Titel zur Warteschlange hinzugefügt").arg(QLocale(QLocale::German).toString(ids.size())));
}

void Library::enqueueIds(const QVariantList &list, bool next)
{
    QVector<qint64> ids;
    for (const QVariant &v : list)
        if (v.toLongLong() > 0)
            ids << v.toLongLong();
    if (ids.isEmpty())
        return;
    next ? m_queue->insertNext(ids) : m_queue->append(ids);
    emit message(next ? tr("Wird als Nächstes gespielt") : tr("Zur Warteschlange hinzugefügt"));
}

void Library::playSelection(TrackListModel *model, int row)
{
    if (model)
        playIds(model->actionIds(row), 0, false);
}

void Library::playFolder(FolderModel *model, int row)
{
    if (!model)
        return;
    playIds(model->trackIds(), model->trackIndexOfRow(row), false);
}

void Library::playFolderTree(const QString &path, bool shuffle)
{
    QVariantMap spec{{QStringLiteral("filters"), QVariantList{QVariantMap{{QStringLiteral("field"), QStringLiteral("folder")},
                                                                           {QStringLiteral("op"), QStringLiteral("under")},
                                                                           {QStringLiteral("value"), path}}}},
                     {QStringLiteral("sort"), QStringLiteral("path")}};
    playIds(idsForSpec(spec), shuffle ? -1 : 0, shuffle);
}

void Library::playGroup(GroupModel *model, int row, bool shuffle)
{
    if (!model)
        return;
    playIds(idsForSpec(model->trackSpec(row)), shuffle ? -1 : 0, shuffle);
}

void Library::enqueueGroup(GroupModel *model, int row, bool next)
{
    if (!model)
        return;
    const QVector<qint64> ids = idsForSpec(model->trackSpec(row));
    next ? m_queue->insertNext(ids) : m_queue->append(ids);
    emit message(tr("%1 Titel zur Warteschlange hinzugefügt").arg(QLocale(QLocale::German).toString(ids.size())));
}

void Library::playSpec(const QVariantMap &spec, bool shuffle)
{
    playIds(idsForSpec(spec), shuffle ? -1 : 0, shuffle);
}

QVariant Library::setting(const QString &key, const QVariant &fallback) const
{
    return m_settings.value(QStringLiteral("ui/") + key, fallback);
}

void Library::setSetting(const QString &key, const QVariant &value)
{
    m_settings.setValue(QStringLiteral("ui/") + key, value);
}

QString Library::formatCount(qint64 n) const
{
    return QLocale(QLocale::German).toString(n);
}

QString Library::formatDuration(qint64 ms, bool longForm) const
{
    if (!longForm)
        return ::formatDuration(ms);
    const qint64 mins = ms / 60000;
    if (mins < 60)
        return tr("%1 Min.").arg(mins);
    const qint64 hours = mins / 60;
    if (hours < 48)
        return tr("%1 Std. %2 Min.").arg(hours).arg(mins % 60);
    return tr("%1 Tage %2 Std.").arg(QLocale(QLocale::German).toString(hours / 24)).arg(hours % 24);
}
