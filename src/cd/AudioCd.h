// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>
#include <QVariantList>

#include <atomic>

class Player;
class PlayQueue;

// The audio CD in the drive (QML singleton "AudioCd"): play it through the queue
// (negative ids, see External) and read it into FLAC files.
//
// Reading never touches existing files: each track is written under a temporary
// name in the chosen folder ("<folder>/<Album artist>/<Album>/NN Title.flac") and
// only renamed when no file of that name exists; existing ones are left out.
// Names, year and cover come from MusicBrainz and the Cover Art Archive only when
// switched on ("online"); otherwise "Titel 1", "Titel 2" …, editable before reading.
class AudioCd : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY driveChanged)            // a drive and the GStreamer CD source
    Q_PROPERTY(int status READ status NOTIFY discChanged)                    // 0 no drive, 1 no disc, 2 reading, 3 ready, 4 no audio CD
    Q_PROPERTY(QString album READ album NOTIFY discChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY discChanged)
    Q_PROPERTY(int year READ year NOTIFY discChanged)
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY discChanged)           // {number, item, title, artist, duration}
    Q_PROPERTY(qint64 coverItem READ coverItem NOTIFY discChanged)           // for image://cover (0: none)
    Q_PROPERTY(QString coverVersion READ coverVersion NOTIFY discChanged)
    Q_PROPERTY(bool watching READ watching WRITE setWatching NOTIFY watchingChanged) // the page is open: look for discs
    Q_PROPERTY(bool online READ online WRITE setOnline NOTIFY settingsChanged)        // MusicBrainz
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY settingsChanged)     // where reading writes to
    Q_PROPERTY(bool canRip READ canRip CONSTANT)                                    // FLAC encoder there
    Q_PROPERTY(bool ripping READ ripping NOTIFY ripChanged)
    Q_PROPERTY(double ripProgress READ ripProgress NOTIFY ripChanged)
    Q_PROPERTY(QString ripText READ ripText NOTIFY ripChanged)

public:
    AudioCd(Player *player, PlayQueue *queue, QObject *parent = nullptr);
    ~AudioCd() override;

    bool available() const { return !m_device.isEmpty(); }
    int status() const { return m_status; }
    QString album() const { return m_album; }
    QString artist() const { return m_artist; }
    int year() const { return m_year; }
    QVariantList tracks() const;
    qint64 coverItem() const;
    QString coverVersion() const { return m_coverVersion; }
    bool watching() const { return m_watching; }
    void setWatching(bool on);
    bool online() const;
    void setOnline(bool on);
    QString folder() const;
    void setFolder(const QString &path);
    bool canRip() const { return m_canRip; }
    bool ripping() const { return m_ripping; }
    double ripProgress() const { return m_ripProgress; }
    QString ripText() const { return m_ripText; }

    // For the sidebar: is there a drive at all (cheap, no disc access)?
    static bool driveExists();

    Q_INVOKABLE void refresh();               // read the disc again
    Q_INVOKABLE void play(int index);
    Q_INVOKABLE void eject();
    Q_INVOKABLE void setNames(const QString &album, const QString &artist, int year, const QStringList &titles);
    Q_INVOKABLE void rip();
    Q_INVOKABLE void cancelRip();

signals:
    void driveChanged();
    void discChanged();
    void watchingChanged();
    void settingsChanged();
    void ripChanged();
    void message(const QString &text);

private:
    struct Track
    {
        int number = 0;
        QString title, artist;
        qint64 durationMs = 0;
    };
    void checkDrive();
    void poll();
    void readDisc();
    void discRead(int gen, const QString &discId, const QVector<Track> &tracks, bool audio);
    void lookUp(const QString &discId);
    void publish();
    QString sourceElement() const;

    Player *m_player;
    PlayQueue *m_queue;
    QThreadPool m_pool;
    QTimer m_poll, m_driveTimer;
    QString m_device;
    bool m_testImage = false;
    bool m_watching = false;
    bool m_canRip = false;
    int m_status = 0;
    int m_lastDrive = -1;            // what the drive said last (to read a new disc once)
    std::atomic<int> m_gen{0};       // a newer read of the disc (or quitting) makes older results stale
    QString m_discId, m_album, m_artist, m_coverFile, m_coverVersion;
    int m_year = 0;
    QVector<Track> m_tracks;
    bool m_ripping = false;
    std::atomic<bool> m_ripCancel{false};
    double m_ripProgress = 0;
    QString m_ripText;
};
