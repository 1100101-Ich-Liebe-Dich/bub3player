// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "library/TrackCache.h"
#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QVariantMap>
#include <QVector>

// Virtualized track list for any view spec. Holds only ids; rows are pulled in
// pages of 128 when the ListView asks for them. Id queries run off the UI thread.
// A spec {"ids": [...]} shows exactly these tracks in this order (mixes).
// Selection (Ctrl / Shift click) is a bit set, so "select all" on a million
// rows costs 125 KB.
class TrackListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap spec READ spec WRITE setSpec NOTIFY specChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(qint64 totalDurationMs READ totalDurationMs NOTIFY countChanged)
    Q_PROPERTY(QString error READ error NOTIFY countChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1, TitleRole, ArtistRole, AlbumRole, AlbumArtistRole, YearRole, DurationRole,
        DurationMsRole, FormatRole, TrackNoRole, DiscNoRole, GenreRole, AddedRole, MtimeRole, PlayCountRole,
        LastPlayedRole, FileNameRole, ValidRole, SelectedRole
    };
    enum SelectMode { Replace = 0, Toggle = 1, Range = 2 };
    Q_ENUM(SelectMode)

    explicit TrackListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QVariantMap spec() const { return m_spec; }
    void setSpec(const QVariantMap &spec);
    int count() const { return int(m_ids.size()); }
    bool loading() const { return m_loading; }
    qint64 totalDurationMs() const { return m_totalMs; }
    QString error() const { return m_error; }
    const QVector<qint64> &ids() const { return m_ids; }
    int selectionCount() const { return m_selCount; }
    // Rows an action on `row` applies to: the selection if `row` is part of it, else just `row`.
    QVector<int> actionRows(int row) const;
    QVector<qint64> actionIds(int row) const;

    Q_INVOKABLE void reload();
    Q_INVOKABLE qint64 idAt(int row) const { return row >= 0 && row < m_ids.size() ? m_ids[row] : 0; }
    Q_INVOKABLE int rowOf(qint64 id) const { return int(m_ids.indexOf(id)); }
    Q_INVOKABLE QVariantMap get(int row) const;
    // Playlist entry key (position) of a row, -1 outside playlists.
    Q_INVOKABLE qint64 keyAt(int row) const { return row >= 0 && row < m_keys.size() ? m_keys[row] : -1; }
    bool hasKeys() const { return !m_keys.isEmpty(); }

    Q_INVOKABLE void select(int row, int mode = Replace);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool isSelected(int row) const;
    Q_INVOKABLE int actionCount(int row) const { return int(actionRows(row).size()); }

signals:
    void specChanged();
    void countChanged();
    void loadingChanged();
    void reloaded();
    void selectionChanged();

private:
    const TrackRow &rowAt(int row) const;
    void apply(int generation, QVector<qint64> ids, QVector<qint64> keys, qint64 totalMs, QString error);
    void setBit(int row, bool on);
    void selectionUpdated();

    QVariantMap m_spec;
    QVector<qint64> m_ids;
    QVector<qint64> m_keys;           // playlists only
    std::vector<quint64> m_sel;       // bit per row, empty when nothing is selected
    int m_selCount = 0;
    int m_anchor = -1;
    qint64 m_totalMs = 0;
    QString m_error;
    bool m_loading = false;
    int m_generation = 0;
    bool m_specSet = false;

    static constexpr int PageSize = 128;
    static constexpr int MaxPages = 48;
    mutable QHash<int, QVector<TrackRow>> m_pages;
    mutable QList<int> m_lru;
    TrackRow m_empty;
};
