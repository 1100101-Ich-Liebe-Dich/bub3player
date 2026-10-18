#include "library/TrackListModel.h"

#include "core/Database.h"
#include "library/Library.h"
#include "library/QueryBuilder.h"

#include <QDateTime>
#include <QLocale>
#include <QPointer>
#include <QtConcurrent>

TrackListModel::TrackListModel(QObject *parent) : QAbstractListModel(parent)
{
    if (Library::instance())
        connect(Library::instance(), &Library::changed, this, &TrackListModel::reload);
}

int TrackListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_ids.size());
}

QHash<int, QByteArray> TrackListModel::roleNames() const
{
    return {
        {IdRole, "trackId"}, {TitleRole, "title"}, {ArtistRole, "artist"}, {AlbumRole, "album"},
        {AlbumArtistRole, "albumArtist"}, {YearRole, "year"}, {DurationRole, "duration"},
        {DurationMsRole, "durationMs"}, {FormatRole, "format"}, {TrackNoRole, "trackNo"}, {DiscNoRole, "discNo"},
        {GenreRole, "genre"}, {AddedRole, "added"}, {MtimeRole, "mtime"}, {PlayCountRole, "playCount"},
        {LastPlayedRole, "lastPlayed"}, {FileNameRole, "fileName"}, {ValidRole, "valid"},
    };
}

const TrackRow &TrackListModel::rowAt(int row) const
{
    const int page = row / PageSize;
    auto it = m_pages.find(page);
    if (it == m_pages.end()) {
        const int start = page * PageSize;
        const int end = std::min<int>(start + PageSize, int(m_ids.size()));
        it = m_pages.insert(page, TrackCache::instance().fetch(m_ids.mid(start, end - start)));
        m_lru.append(page);
        while (m_lru.size() > MaxPages)
            m_pages.remove(m_lru.takeFirst());
    }
    const QVector<TrackRow> &rows = *it;
    const int i = row - page * PageSize;
    return i < rows.size() ? rows[i] : m_empty;
}

static QString shortDate(qint64 secs)
{
    if (secs <= 0)
        return QStringLiteral("–");
    return QLocale(QLocale::German).toString(QDateTime::fromSecsSinceEpoch(secs).date(), QStringLiteral("dd.MM.yyyy"));
}

QVariant TrackListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_ids.size())
        return {};
    if (role == IdRole)
        return m_ids[index.row()];
    const TrackRow &r = rowAt(index.row());
    switch (role) {
    case TitleRole: return r.title;
    case ArtistRole: return r.artist;
    case AlbumRole: return r.album;
    case AlbumArtistRole: return r.aa;
    case YearRole: return r.year > 0 ? QString::number(r.year) : QString();
    case DurationRole: return r.durationText();
    case DurationMsRole: return r.durationMs;
    case FormatRole: return r.formatLabel();
    case TrackNoRole: return r.trackNo > 0 ? QString::number(r.trackNo) : QString();
    case DiscNoRole: return r.discNo;
    case GenreRole: return r.genre;
    case AddedRole: return shortDate(r.added);
    case MtimeRole: return shortDate(r.mtime);
    case PlayCountRole: return r.playCount;
    case LastPlayedRole: return r.lastPlayed > 0 ? shortDate(r.lastPlayed) : QString();
    case FileNameRole: return r.filename;
    case ValidRole: return r.valid;
    }
    return {};
}

QVariantMap TrackListModel::get(int row) const
{
    QVariantMap m;
    if (row < 0 || row >= m_ids.size())
        return m;
    const auto roles = roleNames();
    for (auto it = roles.begin(); it != roles.end(); ++it)
        m.insert(QString::fromLatin1(it.value()), data(index(row), it.key()));
    return m;
}

void TrackListModel::setSpec(const QVariantMap &spec)
{
    if (m_specSet && spec == m_spec)
        return;
    m_spec = spec;
    m_specSet = true;
    emit specChanged();
    reload();
}

void TrackListModel::reload()
{
    if (!m_specSet)
        return;
    const int gen = ++m_generation;
    if (m_spec.contains(QStringLiteral("ids"))) {
        // A fixed list (a mix): shown in exactly this order.
        QVector<qint64> ids;
        for (const QVariant &v : m_spec.value(QStringLiteral("ids")).toList())
            ids << v.toLongLong();
        qint64 total = 0;
        for (const TrackRow &r : TrackCache::instance().fetch(ids))
            total += r.durationMs;
        apply(gen, std::move(ids), total, {});
        return;
    }
    if (!m_loading) {
        m_loading = true;
        emit loadingChanged();
    }
    const QueryBuilder::Built q = QueryBuilder::trackIds(m_spec);
    QPointer<TrackListModel> self(this);
    (void)QtConcurrent::run([self, gen, q] {
        QVector<qint64> ids;
        QString error = q.error;
        DbConnection db;
        if (q.ok) {
            try {
                db = Database::open(8 * 1024);
                Stmt s(db, q.sql);
                for (int i = 0; i < q.binds.size(); ++i)
                    s.bind(i + 1, q.binds[i]);
                while (s.step())
                    ids.push_back(s.i64(0));
            } catch (const std::exception &e) {
                error = QString::fromUtf8(e.what());
            }
        }
        const qsizetype n = ids.size();
        // Ids first: the list is usable now; the total duration follows.
        QMetaObject::invokeMethod(qApp, [self, gen, ids = std::move(ids), error]() mutable {
            if (self)
                self->apply(gen, std::move(ids), -1, error);
        }, Qt::QueuedConnection);
        if (!q.ok || n == 0 || !db.isOpen())
            return;
        qint64 total = 0;
        try {
            Stmt s(db, q.sumSql);
            for (int i = 0; i < q.binds.size(); ++i)
                s.bind(i + 1, q.binds[i]);
            if (s.step())
                total = s.i64(0);
        } catch (const std::exception &) {
            return;
        }
        QMetaObject::invokeMethod(qApp, [self, gen, total] {
            if (self && gen == self->m_generation) {
                self->m_totalMs = total;
                emit self->countChanged();
            }
        }, Qt::QueuedConnection);
    });
}

void TrackListModel::apply(int generation, QVector<qint64> ids, qint64 totalMs, QString error)
{
    if (generation != m_generation)
        return; // a newer query superseded this one
    TrackCache::instance().invalidate();
    if (ids == m_ids) {
        // Same rows (e.g. tags updated during a scan): refresh visible data without
        // resetting, so the scroll position stays put.
        m_pages.clear();
        m_lru.clear();
        if (!m_ids.isEmpty())
            emit dataChanged(index(0), index(int(m_ids.size()) - 1));
    } else {
        beginResetModel();
        m_ids = std::move(ids);
        m_pages.clear();
        m_lru.clear();
        endResetModel();
    }
    if (totalMs >= 0 || m_ids.isEmpty())
        m_totalMs = std::max<qint64>(0, totalMs);
    m_error = error;
    m_loading = false;
    emit countChanged();
    emit loadingChanged();
    emit reloaded();
}
