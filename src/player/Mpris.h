#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

class Player;
class PlayQueue;
class QWindow;

// MPRIS2: media keys, desktop widgets, KDE Connect, playerctl ...
class MprisRoot : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(bool HasTrackList READ hasTrackList)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(QStringList SupportedUriSchemes READ uriSchemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ mimeTypes)

public:
    explicit MprisRoot(QObject *parent);
    bool canQuit() const { return true; }
    bool canRaise() const { return true; }
    bool hasTrackList() const { return false; }
    QString identity() const { return QStringLiteral("BuB3Player"); }
    QString desktopEntry() const { return QStringLiteral("de.bauenundbooten.BuB3Player"); }
    QStringList uriSchemes() const { return {QStringLiteral("file")}; }
    QStringList mimeTypes() const;

    QWindow *window = nullptr;

public slots:
    void Raise();
    void Quit();
};

class MprisPlayer : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QString LoopStatus READ loopStatus WRITE setLoopStatus)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double MinimumRate READ rate)
    Q_PROPERTY(double MaximumRate READ rate)
    Q_PROPERTY(bool CanGoNext READ canControl)
    Q_PROPERTY(bool CanGoPrevious READ canControl)
    Q_PROPERTY(bool CanPlay READ canControl)
    Q_PROPERTY(bool CanPause READ canControl)
    Q_PROPERTY(bool CanSeek READ canControl)
    Q_PROPERTY(bool CanControl READ canControl)

public:
    MprisPlayer(QObject *parent, Player *player, PlayQueue *queue);

    QString playbackStatus() const;
    QString loopStatus() const;
    void setLoopStatus(const QString &s);
    double rate() const { return 1.0; }
    void setRate(double) {}
    bool shuffle() const;
    void setShuffle(bool on);
    QVariantMap metadata() const;
    double volume() const;
    void setVolume(double v);
    qlonglong position() const;
    bool canControl() const { return true; }

public slots:
    void Next();
    void Previous();
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qlonglong offsetUs);
    void SetPosition(const QDBusObjectPath &trackId, qlonglong positionUs);
    void OpenUri(const QString &) {}

signals:
    void Seeked(qlonglong positionUs);

private:
    void notify(const QVariantMap &changed);
    Player *m_player;
    PlayQueue *m_queue;
    QString m_artUrl;
};

namespace Mpris {
// Registers org.mpris.MediaPlayer2.halcyon on the session bus. Silently does
// nothing without a session bus (e.g. headless).
void install(Player *player, PlayQueue *queue, QWindow *window);
}
