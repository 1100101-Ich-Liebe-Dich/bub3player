// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>
#include <QVector>

#include <atomic>

class Player;

// Lines of a song, synchronised (a time per line) or plain.
struct LyricsText
{
    QStringList lines;
    QVector<qint64> times; // ms, ascending; empty for plain lyrics
    QString source;        // where they came from: "lrc", "tags", "jellyfin", "lrclib"

    bool synced() const { return !times.isEmpty(); }
    bool isEmpty() const { return lines.isEmpty(); }

    // LRC ("[01:02.50]Text", several stamps per line, [offset:±ms], word stamps
    // "<01:02.50>" are dropped) or plain text. Synchronised when at least two
    // lines carry a time stamp.
    static LyricsText parse(const QString &text);
};

// Lyrics of the playing track (QML singleton "Lyrics"), for the big views.
//
// Looked for only while a big view is open (active), in this order:
//  1. "<file name>.lrc" next to the music file,
//  2. the file's tags (ID3v2 SYLT/USLT, LYRICS, UNSYNCEDLYRICS, ©lyr …),
//  3. Jellyfin (/Audio/<id>/Lyrics) for tracks from a server,
//  4. LRCLIB (lrclib.net) – only when switched on ("online") and the big view
//     shows lyrics, for tracks without lyrics of their own or with plain ones
//     only. Answers (also "none") are kept in ~/.cache/halcyon/lyrics.
// Nothing is ever written next to the music.
class Lyrics : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)    // a big view is open
    Q_PROPERTY(bool shown READ shown WRITE setShown NOTIFY settingsChanged)     // big views show lyrics instead of the cover
    Q_PROPERTY(bool online READ online WRITE setOnline NOTIFY settingsChanged)  // ask LRCLIB
    Q_PROPERTY(int status READ status NOTIFY changed)                           // 0 idle, 1 searching, 2 found, 3 none
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool synced READ synced NOTIFY changed)
    Q_PROPERTY(QStringList lines READ lines NOTIFY changed)
    Q_PROPERTY(QString source READ source NOTIFY changed)
    Q_PROPERTY(int current READ current NOTIFY currentChanged)                  // line being sung, -1 before the first

public:
    enum Status { Idle, Searching, Found, None };

    explicit Lyrics(Player *player, QObject *parent = nullptr);
    ~Lyrics() override;

    bool active() const { return m_active; }
    void setActive(bool on);
    bool shown() const { return m_shown; }
    void setShown(bool on);
    bool online() const { return m_online; }
    void setOnline(bool on);
    int status() const { return m_status; }
    bool available() const { return !m_text.isEmpty(); }
    bool synced() const { return m_text.synced(); }
    QStringList lines() const { return m_text.lines; }
    QString source() const { return m_text.source; }
    int current() const { return m_current; }

    Q_INVOKABLE void seekTo(int line);

    // What a local file brings along (.lrc, tags), for "halcyon --lyrics".
    static LyricsText forFile(const QString &path);

signals:
    void activeChanged();
    void settingsChanged();
    void changed();
    void currentChanged();

private:
    enum Level { Nothing, Local, Network }; // how far the search went for this track
    void onTrackChanged();
    void search();
    void apply(qint64 id, quint64 gen, int level, const LyricsText &text);
    void clear(int status);
    void updateCurrent(bool force = false);
    Level wantedLevel() const;

    Player *m_player;
    bool m_active = false;
    bool m_shown = true;
    bool m_online = false;
    int m_status = Idle;
    LyricsText m_text;
    int m_current = -1;
    qint64 m_trackId = 0;   // track being searched for (or found)
    qint64 m_textId = 0;    // track m_text belongs to (the old lines stay a moment on a change)
    int m_level = Nothing;  // reached for m_trackId
    int m_running = Nothing; // a search up to this level is under way
    quint64 m_gen = 0;
    QTimer m_clearTimer;    // old lyrics stay a moment on a track change: no flash of the cover
    std::atomic<quint64> m_liveGen{0}; // read by searches in worker threads: stop when stale
    QThreadPool m_pool;     // after m_liveGen: waits for running searches when destroyed
};
