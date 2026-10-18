#pragma once

#include "library/TrackCache.h"
#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QVariantMap>
#include <QVector>

// Virtualized track list for any view spec. Holds only ids; rows are pulled in
// pages of 128 when the ListView asks for them. Id queries run off the UI thread.
class TrackListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap spec READ spec WRITE setSpec NOTIFY specChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(qint64 totalDurationMs READ totalDurationMs NOTIFY countChanged)
    Q_PROPERTY(QString error READ error NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1, TitleRole, ArtistRole, AlbumRole, AlbumArtistRole, YearRole, DurationRole,
        DurationMsRole, FormatRole, TrackNoRole, DiscNoRole, GenreRole, AddedRole, MtimeRole, PlayCountRole,
        LastPlayedRole, FileNameRole, ValidRole
    };

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

    Q_INVOKABLE void reload();
    Q_INVOKABLE qint64 idAt(int row) const { return row >= 0 && row < m_ids.size() ? m_ids[row] : 0; }
    Q_INVOKABLE int rowOf(qint64 id) const { return int(m_ids.indexOf(id)); }
    Q_INVOKABLE QVariantMap get(int row) const;

signals:
    void specChanged();
    void countChanged();
    void loadingChanged();
    void reloaded();

private:
    const TrackRow &rowAt(int row) const;
    void apply(int generation, QVector<qint64> ids, qint64 totalMs, QString error);

    QVariantMap m_spec;
    QVector<qint64> m_ids;
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
