#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVector>

class DbConnection;
class Library;
class Player;
class PlayQueue;
class TrackListModel;
class GroupModel;

// Recommendations from the listening history and the audio analysis.
//
// Learning (only from this computer, nothing leaves it):
//  - artists heard close together in a session become linked (both ways),
//    a skip right after an artist weakens the link
//  - per artist: plays, skips and the time of day
//
// Picking: candidates from linked artists, the same artists, genre, era,
// tempo and some never played tracks; each candidate gets a score
// (taste, similarity, flow of tempo/key/energy, how much it is liked, time
// of day, not heard recently, variety) and is drawn by weighted chance, so
// it never plays the same list twice. Works without history (then mostly
// by genre, era and sound) and without analysis (then without tempo/key).
//
// Uses: autoplay at the end of the queue, radio ("Mehr wie dieser Titel"),
// and the mixes on "Für dich".
class Recommender : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int adventure READ adventure WRITE setAdventure NOTIFY settingsChanged) // 0 familiar, 1 balanced, 2 adventurous
    Q_PROPERTY(bool learning READ learning WRITE setLearning NOTIFY settingsChanged)
    Q_PROPERTY(QString learnedText READ learnedText NOTIFY learnedChanged)
    Q_PROPERTY(QVariantList mixes READ mixes NOTIFY mixesChanged)   // [{title, subtitle, ids, kind}]
    Q_PROPERTY(bool mixesLoading READ mixesLoading NOTIFY mixesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    struct Context
    {
        QVector<qint64> seeds;       // oldest first: the last one weighs most
        QSet<qint64> exclude;        // already queued / just played
        QSet<QString> avoidArtists;  // skipped in this session
        int count = 10;
        int mode = 0;                // 0 continue (autoplay, radio), 1 close to the seeds, 2 only never played
        bool automix = false;        // tempo/key/energy flow counts more
        double adventure = 0.5;      // 0..1
        int daypart = 0;             // 0 night, 1 morning, 2 afternoon, 3 evening
        bool hideDuplicates = true;
        QVector<qint64> online;      // reachable Jellyfin sources
        bool learning = true;
    };

    Recommender(Player *player, PlayQueue *queue, Library *library, QObject *parent = nullptr);
    ~Recommender() override;

    int adventure() const { return m_adventure; }
    void setAdventure(int a);
    bool learning() const { return m_learning; }
    void setLearning(bool on);
    QString learnedText() const { return m_learnedText; }
    QVariantList mixes() const { return m_mixes; }
    bool mixesLoading() const { return m_mixesLoading; }
    bool busy() const { return m_busy; }

    // "Mehr wie dieser Titel": the track, then picks; keeps going (radio).
    Q_INVOKABLE void startRadio(qint64 trackId);
    Q_INVOKABLE void startRadioFromList(TrackListModel *model, int row);
    // Radio from a group (album, artist, genre ...): seeds come from its tracks.
    Q_INVOKABLE void startRadioFromGroup(GroupModel *model, int row);
    Q_INVOKABLE void loadMixes(bool force = false);
    Q_INVOKABLE void playMix(int index, int start = 0, bool shuffle = false);
    Q_INVOKABLE void resetLearning();

    static QString artistKey(const QString &artist);
    static QVector<qint64> pick(const Context &ctx);  // worker threads only
    // Learns everything again from the play history (worker threads only).
    static void relearn(DbConnection &db);

signals:
    void settingsChanged();
    void learnedChanged();
    void mixesChanged();
    void busyChanged();

private:
    Context baseContext() const;
    void maybeAutoplay();
    void onTrackFinished(qint64 id, qint64 listenedMs, qint64 durationMs, bool skipped);
    void startRadioFromSeeds(const QVector<qint64> &seeds, qint64 first, const QString &label);
    void refreshLearned();
    void setBusy(bool b);

    Player *m_player;
    PlayQueue *m_queue;
    Library *m_library;
    int m_adventure = 1;
    bool m_learning = true;
    QString m_learnedText;
    QVariantList m_mixes;
    bool m_mixesLoading = false;
    qint64 m_mixesAt = 0;
    bool m_busy = false;
    bool m_autoplayRunning = false;
    qint64 m_autoplaySeed = 0;
    QSet<QString> m_avoid;         // artists skipped in this session
    QVector<qint64> m_recent;      // played in this session (not again soon)
};
