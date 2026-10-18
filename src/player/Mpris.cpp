#include "player/Mpris.h"

#include "core/CoverProvider.h"
#include "player/PlayQueue.h"
#include "player/Player.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QPointer>
#include <QUrl>
#include <QWindow>
#include <QtConcurrent>

namespace {
const QString ObjectPath = QStringLiteral("/org/mpris/MediaPlayer2");

QDBusObjectPath trackPath(qint64 id)
{
    return QDBusObjectPath(QStringLiteral("/org/halcyon/track/%1").arg(id > 0 ? id : 0));
}
} // namespace

MprisRoot::MprisRoot(QObject *parent) : QDBusAbstractAdaptor(parent) {}

QStringList MprisRoot::mimeTypes() const
{
    return {QStringLiteral("audio/mpeg"), QStringLiteral("audio/flac"), QStringLiteral("audio/ogg"),
            QStringLiteral("audio/opus"), QStringLiteral("audio/mp4"), QStringLiteral("audio/x-wav")};
}

void MprisRoot::Raise()
{
    if (window) {
        window->show();
        window->raise();
        window->requestActivate();
    }
}

void MprisRoot::Quit()
{
    QCoreApplication::quit();
}

MprisPlayer::MprisPlayer(QObject *parent, Player *player, PlayQueue *queue)
    : QDBusAbstractAdaptor(parent), m_player(player), m_queue(queue)
{
    connect(player, &Player::stateChanged, this, [this] { notify({{QStringLiteral("PlaybackStatus"), playbackStatus()}}); });
    connect(player, &Player::volumeChanged, this, [this] { notify({{QStringLiteral("Volume"), volume()}}); });
    connect(player, &Player::seeked, this, [this](qint64 ms) { emit Seeked(ms * 1000); });
    connect(queue, &PlayQueue::modeChanged, this, [this] {
        notify({{QStringLiteral("Shuffle"), shuffle()}, {QStringLiteral("LoopStatus"), loopStatus()}});
    });
    connect(player, &Player::trackChanged, this, [this] {
        m_artUrl.clear();
        notify({{QStringLiteral("Metadata"), metadata()}});
        // Art lives in the cover cache; resolve it off the UI thread, then update.
        const qint64 id = m_player->currentId();
        QPointer<MprisPlayer> self(this);
        (void)QtConcurrent::run([self, id] {
            const QString file = CoverProvider::cachedFile(id, 512);
            QMetaObject::invokeMethod(qApp, [self, id, file] {
                if (self && self->m_player->currentId() == id && !file.isEmpty()) {
                    self->m_artUrl = QUrl::fromLocalFile(file).toString();
                    self->notify({{QStringLiteral("Metadata"), self->metadata()}});
                }
            }, Qt::QueuedConnection);
        });
    });
}

QString MprisPlayer::playbackStatus() const
{
    switch (m_player->state()) {
    case 1: return QStringLiteral("Playing");
    case 2: return QStringLiteral("Paused");
    default: return QStringLiteral("Stopped");
    }
}

QString MprisPlayer::loopStatus() const
{
    switch (m_queue->repeat()) {
    case 1: return QStringLiteral("Playlist");
    case 2: return QStringLiteral("Track");
    default: return QStringLiteral("None");
    }
}

void MprisPlayer::setLoopStatus(const QString &s)
{
    m_queue->setRepeat(s == QLatin1String("Playlist") ? 1 : s == QLatin1String("Track") ? 2 : 0);
}

bool MprisPlayer::shuffle() const
{
    return m_queue->shuffle();
}

void MprisPlayer::setShuffle(bool on)
{
    m_queue->setShuffle(on);
}

QVariantMap MprisPlayer::metadata() const
{
    const qint64 id = m_player->currentId();
    if (id <= 0)
        return {{QStringLiteral("mpris:trackid"), QVariant::fromValue(trackPath(0))}};
    QVariantMap m{
        {QStringLiteral("mpris:trackid"), QVariant::fromValue(trackPath(id))},
        {QStringLiteral("mpris:length"), QVariant::fromValue<qlonglong>(m_player->duration() * 1000)},
        {QStringLiteral("xesam:title"), m_player->title()},
        {QStringLiteral("xesam:artist"), QStringList{m_player->artist()}},
        {QStringLiteral("xesam:album"), m_player->album()},
        {QStringLiteral("xesam:albumArtist"), QStringList{m_player->albumArtist()}},
    };
    if (!m_artUrl.isEmpty())
        m.insert(QStringLiteral("mpris:artUrl"), m_artUrl);
    return m;
}

double MprisPlayer::volume() const
{
    return m_player->volume();
}

void MprisPlayer::setVolume(double v)
{
    m_player->setVolume(v);
}

qlonglong MprisPlayer::position() const
{
    return m_player->position() * 1000;
}

void MprisPlayer::Next() { m_player->next(); }
void MprisPlayer::Previous() { m_player->previous(); }
void MprisPlayer::Pause() { m_player->pause(); }
void MprisPlayer::PlayPause() { m_player->toggle(); }
void MprisPlayer::Stop() { m_player->stop(); }
void MprisPlayer::Play() { m_player->play(); }

void MprisPlayer::Seek(qlonglong offsetUs)
{
    m_player->seekRelative(offsetUs / 1000);
}

void MprisPlayer::SetPosition(const QDBusObjectPath &trackId, qlonglong positionUs)
{
    if (trackId == trackPath(m_player->currentId()))
        m_player->seek(positionUs / 1000);
}

void MprisPlayer::notify(const QVariantMap &changed)
{
    QDBusMessage msg = QDBusMessage::createSignal(ObjectPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                  QStringLiteral("PropertiesChanged"));
    msg << QStringLiteral("org.mpris.MediaPlayer2.Player") << changed << QStringList();
    QDBusConnection::sessionBus().send(msg);
}

namespace Mpris {

void install(Player *player, PlayQueue *queue, QWindow *window)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;
    auto *holder = new QObject(player);
    auto *root = new MprisRoot(holder);
    root->window = window;
    new MprisPlayer(holder, player, queue);
    if (!bus.registerObject(ObjectPath, holder))
        return;
    // A second instance gets its own name instead of failing.
    if (!bus.registerService(QStringLiteral("org.mpris.MediaPlayer2.halcyon")))
        bus.registerService(QStringLiteral("org.mpris.MediaPlayer2.halcyon.instance%1").arg(QCoreApplication::applicationPid()));
}

} // namespace Mpris
