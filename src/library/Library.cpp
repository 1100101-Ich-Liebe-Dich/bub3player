#include "library/Library.h"

#include "core/Database.h"
#include "core/Scanner.h"
#include "library/FolderModel.h"
#include "library/GroupModel.h"
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
    : QObject(parent), m_scanner(new Scanner(this)), m_player(player), m_queue(queue)
{
    s_instance = this;
    // Autoplay: check whenever the queue or playback state changes (queued, so
    // the queue has settled first).
    connect(m_queue, &PlayQueue::upcomingChanged, this, &Library::maybeAutoplay, Qt::QueuedConnection);
    connect(m_player, &Player::stateChanged, this, &Library::maybeAutoplay, Qt::QueuedConnection);
    m_roots = m_settings.value(QStringLiteral("library/roots")).toStringList();
    connect(m_scanner, &Scanner::libraryChanged, this, [this] {
        TrackCache::instance().invalidate();
        refreshStats();
        emit changed();
    });
    connect(m_scanner, &Scanner::finished, this, [this] { emit message(m_scanner->summary()); });
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

void Library::startupScan()
{
    bool ftsStale = false;
    try {
        ftsStale = Database::ftsDirty(TrackCache::instance().db());
    } catch (...) {
    }
    // A previous import was interrupted: the scan also rebuilds the search index.
    if (!m_roots.isEmpty() && (scanOnStart() || ftsStale))
        rescan();
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

void Library::maybeAutoplay()
{
    if (!m_queue->autoplay() || m_queue->repeat() != 0 || m_player->state() != 1)
        return;
    const int count = m_queue->count();
    if (count == 0 || m_queue->currentRow() < count - 2)
        return;
    const qint64 seed = m_queue->lastId();
    if (seed <= 0 || seed == m_autoplaySeed)
        return;
    m_autoplaySeed = seed;
    const QVector<qint64> picks = autoplayPicks(seed, 8);
    if (picks.isEmpty())
        return;
    m_queue->append(picks);
    emit message(tr("Autoplay: %n passende Titel angehängt", nullptr, int(picks.size())));
}

// First, simple version: tracks that fit the last one in the queue (same genre
// and era, a few by the same artist), not played in the last two days and not
// already queued. The recommendation engine (phase 3) will replace this list.
QVector<qint64> Library::autoplayPicks(qint64 seed, int count)
{
    QVector<qint64> out;
    try {
        DbConnection &db = TrackCache::instance().db();
        Stmt row(db, "SELECT aa, genre, year FROM tracks WHERE id = ?");
        row.bind(1, seed);
        if (!row.step())
            return out;
        const QString aa = row.str(0), genre = row.str(1);
        const int year = row.i32(2);
        QSet<qint64> seen(m_queue->ids().cbegin(), m_queue->ids().cend());
        const qint64 cutoff = QDateTime::currentSecsSinceEpoch() - 2 * 24 * 3600;
        auto take = [&](const char *where, int n, auto bindFn) {
            if (int(out.size()) >= count)
                return;
            const QString sql = QStringLiteral("SELECT id FROM tracks WHERE tags_read = 1 AND last_played < ?1 AND (%1) "
                                               "ORDER BY random() LIMIT %2").arg(QLatin1String(where)).arg(n * 3);
            Stmt s(db, sql);
            s.bind(1, cutoff);
            bindFn(s);
            int added = 0;
            while (s.step() && added < n && int(out.size()) < count) {
                const qint64 id = s.i64(0);
                if (seen.contains(id))
                    continue;
                seen.insert(id);
                out << id;
                ++added;
            }
        };
        if (!genre.isEmpty() && year > 0)
            take("genre = ?2 COLLATE NOCASE AND year BETWEEN ?3 AND ?4", 5,
                 [&](Stmt &s) { s.bind(2, genre); s.bind(3, year - 5); s.bind(4, year + 5); });
        if (!aa.isEmpty())
            take("aa = ?2 COLLATE NOCASE", 2, [&](Stmt &s) { s.bind(2, aa); });
        if (!genre.isEmpty())
            take("genre = ?2 COLLATE NOCASE", count, [&](Stmt &s) { s.bind(2, genre); });
        if (year > 0)
            take("year BETWEEN ?2 AND ?3", count, [&](Stmt &s) { s.bind(2, year - 5); s.bind(3, year + 5); });
        take("1", count, [](Stmt &) {});
    } catch (const std::exception &e) {
        qWarning() << "Autoplay:" << e.what();
    }
    // Same-artist picks should not come in a row: spread them.
    if (out.size() > 3)
        std::swap(out[1], out[out.size() - 1]);
    return out;
}

void Library::refreshStats()
{
    try {
        DbConnection &db = TrackCache::instance().db();
        m_trackCount = db.scalar("SELECT COUNT(*) FROM tracks");
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
    const QVector<qint64> one{model->idAt(row)};
    next ? m_queue->insertNext(one) : m_queue->append(one);
    emit message(next ? tr("Wird als Nächstes gespielt") : tr("Zur Warteschlange hinzugefügt"));
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
