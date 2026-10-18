#pragma once

#include <QAbstractListModel>
#include <QVector>
#include <functional>

// Play queue. Rows are shown in playback order; shuffle permutes the order
// without losing the original, so turning shuffle off restores it.
class PlayQueue : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int currentRow READ currentRow NOTIFY currentChanged)
    Q_PROPERTY(qint64 currentId READ currentId NOTIFY currentChanged)
    Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY modeChanged)
    Q_PROPERTY(int repeat READ repeat WRITE setRepeat NOTIFY modeChanged) // 0 off, 1 all, 2 one
    Q_PROPERTY(QString upNext READ upNext NOTIFY upcomingChanged)          // "Titel – Künstler" of the next track
    // Autoplay: when the queue is about to run out, the library appends matching
    // tracks (see Library::maybeAutoplay). Later this is fed by recommendations.
    Q_PROPERTY(bool autoplay READ autoplay WRITE setAutoplay NOTIFY modeChanged)
    // Radio ("Mehr wie dieser Titel"): the queue keeps filling itself even with
    // autoplay off, until something else is played. Label: what it started from.
    Q_PROPERTY(QString radio READ radio NOTIFY modeChanged)

public:
    enum Roles { IdRole = Qt::UserRole + 1, TitleRole, ArtistRole, AlbumRole, DurationRole, IsCurrentRole, IsPastRole, IsAutoRole };

    explicit PlayQueue(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_order.size()); }
    int currentRow() const { return m_pos; }
    qint64 currentId() const;
    qint64 nextId() const; // what plays after the current track (respects repeat), 0 = none
    QString upNext() const;
    bool shuffle() const { return m_shuffle; }
    void setShuffle(bool on);
    int repeat() const { return m_repeat; }
    void setRepeat(int mode);
    bool autoplay() const { return m_autoplay; }
    void setAutoplay(bool on);
    qint64 lastId() const { return m_order.isEmpty() ? 0 : idAtRow(int(m_order.size()) - 1); }
    Q_INVOKABLE qint64 idAt(int row) const { return row >= 0 && row < m_order.size() ? idAtRow(row) : 0; }
    const QVector<qint64> &ids() const { return m_ids; }

    void setTracks(QVector<qint64> ids, int start, bool shuffle);
    void append(const QVector<qint64> &ids, bool automatic = false); // automatic: picked by autoplay / radio
    void insertNext(const QVector<qint64> &ids);
    // Starts a radio: first track chosen by the listener, the rest picked.
    // `firstPicked`: also the first track was picked (a mix), not chosen by the listener.
    void setRadio(const QVector<qint64> &ids, const QString &label, int start = 0, bool firstPicked = false);
    QString radio() const { return m_radio; }
    bool isAuto(int row) const { return row >= 0 && row < m_order.size() && m_auto.value(m_order[row]); }
    // Drops picked tracks after the current one that match (e.g. an artist just skipped).
    int removeUpcomingAuto(const std::function<bool(qint64)> &match);

    bool advance();          // automatic or "next": false at the end of the queue
    bool back();
    Q_INVOKABLE void jumpTo(int row);
    Q_INVOKABLE void removeAt(int row);
    Q_INVOKABLE void move(int from, int to);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void cycleRepeat() { setRepeat((m_repeat + 1) % 3); }

    void save() const;
    void restore();

signals:
    void countChanged();
    void currentChanged();
    void modeChanged();
    void upcomingChanged(); // next track may differ: player re-arms gapless

private:
    qint64 idAtRow(int row) const { return m_ids[m_order[row]]; }
    void setPos(int pos);

    QVector<qint64> m_ids;
    QVector<bool> m_auto;   // per entry of m_ids
    QVector<int> m_order;
    QString m_radio;
    int m_pos = -1;
    bool m_shuffle = false;
    int m_repeat = 0;
    bool m_autoplay = false;
};
