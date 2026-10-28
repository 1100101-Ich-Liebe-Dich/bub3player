// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>

class Player;
class PlayQueue;

// Listening statistics and "your year in music" (QML singleton "Stats"), all from
// the listening history in the database (plays, station_plays): nothing leaves the
// computer. Every query runs off the UI thread on its own connection; the results
// are small maps for QML.
//
// A play counts when it was listened to for long enough (see Player: half the
// track or four minutes); listening time adds up everything heard, skips and radio
// included. Names come from the history itself, so a file that is gone or was
// moved still counts.
class Stats : public QObject
{
    Q_OBJECT
    // "week", "month", "year" or "all"; offset 0 is the current one, -1 the one before …
    Q_PROPERTY(QString period READ period NOTIFY periodChanged)
    Q_PROPERTY(int offset READ offset NOTIFY periodChanged)
    Q_PROPERTY(QVariantMap overview READ overview NOTIFY overviewChanged)
    Q_PROPERTY(QVariantMap library READ library NOTIFY libraryChanged)
    Q_PROPERTY(QVariantList years READ years NOTIFY yearsChanged)      // [{year, plays}], newest first
    Q_PROPERTY(QVariantMap review READ review NOTIFY reviewChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool reviewBusy READ reviewBusy NOTIFY busyChanged)

public:
    Stats(Player *player, PlayQueue *queue, QObject *parent = nullptr);
    ~Stats() override;

    QString period() const { return m_period; }
    int offset() const { return m_offset; }
    QVariantMap overview() const { return m_overview; }
    QVariantMap library() const { return m_library; }
    QVariantList years() const { return m_years; }
    QVariantMap review() const { return m_review; }
    bool busy() const { return m_busy > 0; }
    bool reviewBusy() const { return m_reviewBusy; }

    Q_INVOKABLE void setPeriod(const QString &period, int offset = 0);
    Q_INVOKABLE void refresh();                  // the overview, the library, the years
    Q_INVOKABLE void loadReview(int year);
    // Plays these titles (a top list; 0 = no longer in the library) from `startId` on.
    Q_INVOKABLE void playIds(const QVariantList &ids, qint64 startId = 0);
    // The year whose review is due: the current one, or in January the one before.
    Q_INVOKABLE int reviewYear() const;
    Q_INVOKABLE QUrl picturesFolder() const; // where a picture of the year is saved by default

    struct Range
    {
        qint64 from = 0, to = 0; // seconds since the epoch, [from, to)
    };
    static Range rangeOf(const QString &period, int offset);

    // The numbers themselves (any thread, own connection; also for --stats).
    static QVariantMap overviewOf(const QString &period, int offset);
    static QVariantMap reviewOf(int year);
    static QVariantMap libraryNumbers();
    static QVariantList yearsWithPlays();

signals:
    void periodChanged();
    void overviewChanged();
    void libraryChanged();
    void yearsChanged();
    void reviewChanged();
    void busyChanged();

private:
    void loadOverview();
    void loadLibrary();
    void loadYears();
    void started();
    void finished();

    Player *m_player;
    PlayQueue *m_queue;
    QString m_period = QStringLiteral("month");
    int m_offset = 0;
    QVariantMap m_overview, m_library, m_review;
    QVariantList m_years;
    int m_busy = 0;
    bool m_reviewBusy = false;
    qint64 m_libraryAt = 0;          // when the library numbers were counted (ms)
    std::atomic<int> m_overviewGen{0}, m_reviewGen{0};
};
