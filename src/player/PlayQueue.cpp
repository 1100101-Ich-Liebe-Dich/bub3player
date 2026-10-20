// SPDX-License-Identifier: GPL-3.0-or-later
#include "player/PlayQueue.h"

#include <QSettings>

#include "core/Paths.h"
#include "library/TrackCache.h"

#include <QDataStream>
#include <QFile>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>
#include <numeric>

namespace {
QString queueFile()
{
    return Paths::dataDir() + QStringLiteral("/queue.dat");
}

void shuffleRange(QVector<int> &v, int from)
{
    auto *rng = QRandomGenerator::global();
    for (int i = int(v.size()) - 1; i > from; --i) {
        const int j = from + int(rng->bounded(i - from + 1));
        std::swap(v[i], v[j]);
    }
}
} // namespace

PlayQueue::PlayQueue(QObject *parent) : QAbstractListModel(parent)
{
    m_autoplay = QSettings().value(QStringLiteral("player/autoplay"), false).toBool();
}

void PlayQueue::setAutoplay(bool on)
{
    if (on == m_autoplay)
        return;
    m_autoplay = on;
    QSettings().setValue(QStringLiteral("player/autoplay"), on);
    emit modeChanged();
    emit upcomingChanged();
}

int PlayQueue::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_order.size());
}

QHash<int, QByteArray> PlayQueue::roleNames() const
{
    return {{IdRole, "trackId"}, {TitleRole, "title"}, {ArtistRole, "artist"}, {AlbumRole, "album"},
            {DurationRole, "duration"}, {IsCurrentRole, "isCurrent"}, {IsPastRole, "isPast"}, {IsAutoRole, "isAuto"}};
}

QVariant PlayQueue::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_order.size())
        return {};
    const qint64 id = idAtRow(index.row());
    switch (role) {
    case IdRole: return id;
    case IsCurrentRole: return index.row() == m_pos;
    case IsPastRole: return index.row() < m_pos;
    case IsAutoRole: return m_auto.value(m_order[index.row()]);
    default: break;
    }
    const TrackRow r = TrackCache::instance().get(id);
    switch (role) {
    case TitleRole: return r.title;
    case ArtistRole: return r.artist;
    case AlbumRole: return r.album;
    case DurationRole: return r.durationText();
    }
    return {};
}

qint64 PlayQueue::currentId() const
{
    return m_pos >= 0 && m_pos < m_order.size() ? idAtRow(m_pos) : 0;
}

qint64 PlayQueue::nextId() const
{
    if (m_pos < 0 || m_order.isEmpty())
        return 0;
    if (m_repeat == 2)
        return currentId();
    if (m_pos + 1 < m_order.size())
        return idAtRow(m_pos + 1);
    return m_repeat == 1 ? idAtRow(0) : 0;
}

QString PlayQueue::upNext() const
{
    const qint64 id = nextId();
    if (id <= 0)
        return {};
    const TrackRow r = TrackCache::instance().get(id);
    return r.artist.isEmpty() ? r.title : r.title + QStringLiteral(" – ") + r.artist;
}

void PlayQueue::setPos(int pos)
{
    const int old = m_pos;
    m_pos = pos;
    if (old >= 0 && old < m_order.size())
        emit dataChanged(index(0), index(int(m_order.size()) - 1), {IsCurrentRole, IsPastRole});
    else if (m_pos >= 0 && m_pos < m_order.size())
        emit dataChanged(index(0), index(m_pos), {IsCurrentRole, IsPastRole});
    emit currentChanged();
    emit upcomingChanged();
}

void PlayQueue::setTracks(QVector<qint64> ids, int start, bool shuffle)
{
    beginResetModel();
    m_ids = std::move(ids);
    m_auto.fill(false, m_ids.size());
    m_radio.clear();
    m_order.resize(m_ids.size());
    std::iota(m_order.begin(), m_order.end(), 0);
    if (start < 0 || start >= m_ids.size())
        start = shuffle && !m_ids.isEmpty() ? int(QRandomGenerator::global()->bounded(int(m_ids.size()))) : 0;
    m_shuffle = shuffle;
    if (shuffle) {
        std::swap(m_order[0], m_order[start]);
        shuffleRange(m_order, 1);
        m_pos = m_ids.isEmpty() ? -1 : 0;
    } else {
        m_pos = m_ids.isEmpty() ? -1 : start;
    }
    endResetModel();
    emit countChanged();
    emit modeChanged();
    emit currentChanged();
    emit upcomingChanged();
}

void PlayQueue::append(const QVector<qint64> &ids, bool automatic)
{
    if (ids.isEmpty())
        return;
    const int first = int(m_order.size());
    beginInsertRows({}, first, first + int(ids.size()) - 1);
    for (qint64 id : ids) {
        m_order << int(m_ids.size());
        m_ids << id;
        m_auto << automatic;
    }
    endInsertRows();
    if (m_pos < 0)
        m_pos = 0;
    emit countChanged();
    emit currentChanged();
    emit upcomingChanged();
}

void PlayQueue::insertNext(const QVector<qint64> &ids)
{
    if (ids.isEmpty())
        return;
    const int at = m_pos < 0 ? 0 : m_pos + 1;
    beginInsertRows({}, at, at + int(ids.size()) - 1);
    QVector<int> idx;
    for (qint64 id : ids) {
        idx << int(m_ids.size());
        m_ids << id;
        m_auto << false;
    }
    for (int i = 0; i < idx.size(); ++i)
        m_order.insert(at + i, idx[i]);
    endInsertRows();
    if (m_pos < 0)
        m_pos = 0;
    emit countChanged();
    emit currentChanged();
    emit upcomingChanged();
}

void PlayQueue::setShuffle(bool on)
{
    if (on == m_shuffle)
        return;
    m_shuffle = on;
    if (!m_order.isEmpty()) {
        beginResetModel();
        const int cur = m_pos >= 0 ? m_order[m_pos] : -1;
        if (on) {
            // Keep history, shuffle only what is still to come.
            shuffleRange(m_order, m_pos + 1);
        } else {
            std::iota(m_order.begin(), m_order.end(), 0);
            m_pos = cur;
        }
        endResetModel();
        emit currentChanged();
    }
    emit modeChanged();
    emit upcomingChanged();
}

void PlayQueue::setRepeat(int mode)
{
    mode = std::clamp(mode, 0, 2);
    if (mode == m_repeat)
        return;
    m_repeat = mode;
    emit modeChanged();
    emit upcomingChanged();
}

bool PlayQueue::advance()
{
    if (m_order.isEmpty())
        return false;
    if (m_repeat == 2) {
        emit currentChanged();
        return true;
    }
    if (m_pos + 1 < m_order.size()) {
        setPos(m_pos + 1);
        return true;
    }
    if (m_repeat == 1) {
        setPos(0);
        return true;
    }
    return false;
}

bool PlayQueue::back()
{
    if (m_order.isEmpty())
        return false;
    if (m_pos > 0) {
        setPos(m_pos - 1);
        return true;
    }
    if (m_repeat == 1) {
        setPos(int(m_order.size()) - 1);
        return true;
    }
    return false;
}

void PlayQueue::jumpTo(int row)
{
    if (row >= 0 && row < m_order.size())
        setPos(row);
}

void PlayQueue::removeAt(int row)
{
    if (row < 0 || row >= m_order.size())
        return;
    beginRemoveRows({}, row, row);
    const int k = m_order[row];
    m_order.removeAt(row);
    m_ids.removeAt(k);
    m_auto.removeAt(k);
    for (int &o : m_order)
        if (o > k)
            --o;
    endRemoveRows();
    if (row < m_pos)
        --m_pos;
    if (m_pos >= m_order.size())
        m_pos = int(m_order.size()) - 1;
    emit countChanged();
    emit dataChanged(index(0), index(std::max(0, int(m_order.size()) - 1)), {IsCurrentRole, IsPastRole});
    emit currentChanged();
    emit upcomingChanged();
}

void PlayQueue::move(int from, int to)
{
    if (from < 0 || to < 0 || from >= m_order.size() || to >= m_order.size() || from == to)
        return;
    beginMoveRows({}, from, from, {}, to > from ? to + 1 : to);
    m_order.move(from, to);
    endMoveRows();
    if (m_pos == from)
        m_pos = to;
    else if (from < m_pos && to >= m_pos)
        --m_pos;
    else if (from > m_pos && to <= m_pos)
        ++m_pos;
    emit dataChanged(index(0), index(int(m_order.size()) - 1), {IsCurrentRole, IsPastRole});
    emit upcomingChanged();
}

void PlayQueue::clear()
{
    setTracks({}, 0, m_shuffle);
}

void PlayQueue::setRadio(const QVector<qint64> &ids, const QString &label, int start, bool firstPicked)
{
    setTracks(ids, start, false);
    const int chosen = std::clamp(start, 0, std::max(0, int(m_auto.size()) - 1));
    for (int i = 0; i < m_auto.size(); ++i)
        m_auto[i] = firstPicked || i != chosen;
    m_radio = label;
    if (!m_auto.isEmpty())
        emit dataChanged(index(0), index(int(m_order.size()) - 1), {IsAutoRole});
    emit modeChanged();
    emit upcomingChanged();
}

int PlayQueue::removeUpcomingAuto(const std::function<bool(qint64)> &match)
{
    int removed = 0;
    for (int row = int(m_order.size()) - 1; row > m_pos; --row) {
        if (m_auto.value(m_order[row]) && match(idAtRow(row))) {
            removeAt(row);
            ++removed;
        }
    }
    return removed;
}

void PlayQueue::save() const
{
    QSaveFile f(queueFile());
    if (!f.open(QIODevice::WriteOnly))
        return;
    QDataStream out(&f);
    out << quint32(2) << m_ids << m_order << qint32(m_pos) << m_shuffle << qint32(m_repeat) << m_auto << m_radio;
    f.commit();
}

void PlayQueue::restore()
{
    QFile f(queueFile());
    if (!f.open(QIODevice::ReadOnly))
        return;
    QDataStream in(&f);
    quint32 version = 0;
    QVector<qint64> ids;
    QVector<int> order;
    qint32 pos = -1, repeat = 0;
    bool shuffle = false;
    QVector<bool> autos;
    QString radio;
    in >> version >> ids >> order >> pos >> shuffle >> repeat;
    if (version >= 2)
        in >> autos >> radio;
    if (in.status() != QDataStream::Ok || version < 1 || version > 2 || ids.size() != order.size())
        return;
    if (autos.size() != ids.size())
        autos.fill(false, ids.size());
    for (int o : order)
        if (o < 0 || o >= ids.size())
            return;
    beginResetModel();
    m_ids = ids;
    m_auto = autos;
    m_radio = radio;
    m_order = order;
    m_pos = (pos >= 0 && pos < order.size()) ? pos : (order.isEmpty() ? -1 : 0);
    m_shuffle = shuffle;
    m_repeat = std::clamp(int(repeat), 0, 2);
    endResetModel();
    emit countChanged();
    emit modeChanged();
    emit currentChanged();
    emit upcomingChanged();
}
