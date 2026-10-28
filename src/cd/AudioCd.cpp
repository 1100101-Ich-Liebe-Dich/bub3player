// SPDX-License-Identifier: GPL-3.0-or-later
#include "cd/AudioCd.h"

#include "core/Http.h"
#include "core/Paths.h"
#include "library/External.h"
#include "library/TrackCache.h"
#include "player/PlayQueue.h"
#include "player/Player.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QtConcurrent>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <gst/gst.h>
#include <linux/cdrom.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

QString cacheFile(const QString &discId, const char *ext)
{
    // disc ids use A-Z a-z 0-9 . _ - only
    QString safe = discId;
    safe.remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")));
    return Paths::cacheDir() + QStringLiteral("/cd/") + safe + QLatin1String(ext);
}

// A name that is safe as one file or folder name (no "/", no control characters,
// not "." or "..", at most 200 bytes: file systems allow 255, with room for
// "NN " and ".flac").
QString fileName(QString s, const QString &fallback)
{
    s.replace(QLatin1Char('/'), QLatin1Char('-'));
    s.remove(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]")));
    s = s.simplified();
    while (s.startsWith(QLatin1Char('.')))
        s.remove(0, 1);
    while (s.toUtf8().size() > 200) {
        s.chop(s.size() > 1 && s.at(s.size() - 1).isLowSurrogate() ? 2 : 1); // never half a character
        s = s.trimmed();
    }
    return s.isEmpty() ? fallback : s;
}

// The elements in a row in a new pipeline; nothing (and nothing left over) if one
// is missing or they do not link.
GstElement *pipelineOf(std::initializer_list<GstElement *> chain)
{
    GstElement *pipe = gst_pipeline_new(nullptr);
    bool complete = pipe != nullptr;
    for (GstElement *e : chain)
        complete = complete && e;
    if (!complete) {
        for (GstElement *e : chain)
            if (e)
                gst_object_unref(gst_object_ref_sink(e));
        if (pipe)
            gst_object_unref(pipe);
        return nullptr;
    }
    GstElement *prev = nullptr;
    bool linked = true;
    for (GstElement *e : chain) {
        gst_bin_add(GST_BIN(pipe), e);
        linked = linked && (!prev || gst_element_link(prev, e));
        prev = e;
    }
    if (!linked) {
        gst_object_unref(pipe);
        return nullptr;
    }
    return pipe;
}

// Is there anything under this name, even a link that points nowhere?
bool taken(const QString &path)
{
    struct stat st;
    return ::lstat(QFile::encodeName(path).constData(), &st) == 0;
}

// Gives the finished file `from` its name `to` without ever replacing or writing
// through anything that is there (not even a dangling link). False: `to` was
// taken (`*error` empty) or the file system refused (`*error` says why); `from`
// is gone either way.
bool place(const QString &from, const QString &to, QString *error)
{
    const QByteArray f = QFile::encodeName(from), t = QFile::encodeName(to);
    const auto fail = [&](int e) {
        ::unlink(f.constData());
        if (e != EEXIST)
            *error = QString::fromLocal8Bit(std::strerror(e));
        return false;
    };
    if (::renameat2(AT_FDCWD, f.constData(), AT_FDCWD, t.constData(), RENAME_NOREPLACE) == 0)
        return true;
    if (errno != EINVAL && errno != ENOSYS && errno != EOPNOTSUPP)
        return fail(errno);
    // file systems without it (some network ones): a hard link never replaces either
    if (::link(f.constData(), t.constData()) == 0) {
        ::unlink(f.constData());
        return true;
    }
    if (errno == EEXIST)
        return fail(EEXIST);
    // no hard links (FAT): copy into a file that has to be new
    const int out = ::open(t.constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    if (out < 0)
        return fail(errno);
    QFile in(from);
    bool ok = in.open(QIODevice::ReadOnly);
    while (ok) {
        const QByteArray chunk = in.read(1 << 20);
        if (chunk.isEmpty()) {
            ok = in.atEnd();
            break;
        }
        for (qsizetype done = 0; ok && done < chunk.size();) {
            const ssize_t n = ::write(out, chunk.constData() + done, size_t(chunk.size() - done));
            ok = n > 0 || (n < 0 && errno == EINTR);
            done += n > 0 ? n : 0;
        }
    }
    const int writeErrno = errno;
    ok = ::close(out) == 0 && ok;
    if (!ok) {
        ::unlink(t.constData()); // the file we just made, not one that was there
        return fail(writeErrno ? writeErrno : EIO);
    }
    ::unlink(f.constData());
    return true;
}

// A temporary name of our own next to the target: ".bub3player-<random><ext>.part".
QString partName(const QString &dir, const char *ext)
{
    QString part;
    do {
        part = dir + QStringLiteral("/.bub3player-%1%2.part").arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0')).arg(QLatin1String(ext));
    } while (taken(part));
    return part;
}

// Our own temporary files left behind by a crash (older than an hour: not those
// of a reading that is still going on).
void removeLeftovers(const QString &dir)
{
    const QDateTime old = QDateTime::currentDateTime().addSecs(-3600);
    for (const QFileInfo &fi : QDir(dir).entryInfoList({QStringLiteral(".bub3player-*.part")}, QDir::Files | QDir::Hidden | QDir::NoSymLinks))
        if (fi.lastModified() < old)
            QFile::remove(fi.filePath());
}

// The names of a disc (from MusicBrainz or typed in), kept by its disc id.
QJsonObject readNames(const QString &discId)
{
    QFile f(cacheFile(discId, ".json"));
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
}

void writeNames(const QString &discId, const QJsonObject &names)
{
    QDir().mkpath(QFileInfo(cacheFile(discId, ".json")).absolutePath());
    QSaveFile f(cacheFile(discId, ".json"));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(names).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

QString joinCredit(const QJsonArray &credit)
{
    QString out;
    for (const QJsonValue &c : credit) {
        const QJsonObject o = c.toObject();
        out += o.value(QLatin1String("name")).toString() + o.value(QLatin1String("joinphrase")).toString();
    }
    return out.trimmed();
}

} // namespace

AudioCd::AudioCd(Player *player, PlayQueue *queue, QObject *parent) : QObject(parent), m_player(player), m_queue(queue)
{
    m_pool.setMaxThreadCount(1); // one drive: reading the contents and ripping take turns
    GstElementFactory *flac = gst_element_factory_find("flacenc");
    m_canRip = flac != nullptr;
    if (flac)
        gst_object_unref(flac);
    if (!qEnvironmentVariableIsEmpty("HALCYON_TEST_CDROM")) {
        // tests play a disc image, which only libcdio reads: playbin must not pick cdparanoia
        if (GstElementFactory *f = gst_element_factory_find("cdparanoiasrc")) {
            gst_plugin_feature_set_rank(GST_PLUGIN_FEATURE(f), GST_RANK_NONE);
            gst_object_unref(f);
        }
    }
    checkDrive();
    m_driveTimer.setInterval(10000); // a USB drive plugged in later
    connect(&m_driveTimer, &QTimer::timeout, this, &AudioCd::checkDrive);
    m_driveTimer.start();
    m_poll.setInterval(2000);
    connect(&m_poll, &QTimer::timeout, this, &AudioCd::poll);
}

AudioCd::~AudioCd()
{
    m_ripCancel = true;
    ++m_gen;
    m_pool.clear();
    m_pool.waitForDone();
}

QString AudioCd::sourceElement() const
{
    // cdparanoia corrects read errors (best for ripping); libcdio can also read disc
    // images, which the tests use
    const char *order[] = {"cdparanoiasrc", "cdiocddasrc"};
    if (m_testImage)
        std::swap(order[0], order[1]);
    for (const char *name : order)
        if (GstElementFactory *f = gst_element_factory_find(name)) {
            gst_object_unref(f);
            return QString::fromLatin1(name);
        }
    return {};
}

bool AudioCd::driveExists()
{
    if (!qEnvironmentVariableIsEmpty("HALCYON_TEST_CDROM"))
        return true;
    if (QFileInfo::exists(QStringLiteral("/dev/cdrom")))
        return true;
    return !QDir(QStringLiteral("/dev")).entryList({QStringLiteral("sr[0-9]*")}, QDir::System).isEmpty();
}

void AudioCd::checkDrive()
{
    QString device;
    const QString test = qEnvironmentVariable("HALCYON_TEST_CDROM"); // tests: a .cue image
    m_testImage = !test.isEmpty();
    if (m_testImage) {
        device = test;
    } else if (QFileInfo::exists(QStringLiteral("/dev/cdrom"))) {
        device = QStringLiteral("/dev/cdrom");
    } else {
        const QStringList sr = QDir(QStringLiteral("/dev")).entryList({QStringLiteral("sr[0-9]*")}, QDir::System, QDir::Name);
        if (!sr.isEmpty())
            device = QStringLiteral("/dev/") + sr.first();
    }
    if (sourceElement().isEmpty())
        device.clear(); // no way to read it
    if (device == m_device)
        return;
    m_device = device;
    m_lastDrive = -1;
    if (m_device.isEmpty()) {
        m_status = 0;
        m_discId.clear();
        m_tracks.clear();
        External::setDisc({});
        emit discChanged();
    } else if (m_status == 0) {
        m_status = 1;
        emit discChanged();
    }
    emit driveChanged();
    if (m_watching)
        poll();
}

void AudioCd::setWatching(bool on)
{
    if (on == m_watching)
        return;
    m_watching = on;
    if (on) {
        // the disc may have been changed while the page was closed: read it again
        // (not while it plays or is being read into files)
        if (m_lastDrive == 1 && !m_testImage && !m_ripping && !External::isCd(m_player->currentId()))
            m_lastDrive = -1;
        poll();
        m_poll.start();
    } else {
        m_poll.stop();
    }
    emit watchingChanged();
}

// What the drive says: 0 no disc (or open), 1 an audio disc, 2 another disc.
void AudioCd::poll()
{
    if (m_device.isEmpty() || m_ripping)
        return;
    if (m_testImage) { // the image is "inserted" at the start and by refresh(), "out" after eject()
        if (m_lastDrive == -1) {
            m_lastDrive = 1;
            readDisc();
        }
        return;
    }
    QPointer<AudioCd> self(this);
    const QByteArray dev = QFile::encodeName(m_device);
    (void)QtConcurrent::run([self, dev] {
        int state = 0;
        const int fd = ::open(dev.constData(), O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            if (::ioctl(fd, CDROM_DRIVE_STATUS, CDSL_CURRENT) == CDS_DISC_OK) {
                const int disc = ::ioctl(fd, CDROM_DISC_STATUS, 0);
                state = disc == CDS_AUDIO || disc == CDS_MIXED ? 1 : 2;
            }
            ::close(fd);
        }
        QMetaObject::invokeMethod(qApp, [self, state] {
            if (!self || state == self->m_lastDrive)
                return;
            self->m_lastDrive = state;
            if (state == 1) {
                self->readDisc();
                return;
            }
            // disc gone (or no audio): nothing of it plays any more
            if (External::isCd(self->m_player->currentId()))
                self->m_player->stop();
            self->m_status = state == 2 ? 4 : 1;
            self->m_discId.clear();
            self->m_tracks.clear();
            self->m_album.clear();
            self->m_artist.clear();
            External::setDisc({});
            TrackCache::instance().invalidate();
            emit self->discChanged();
        }, Qt::QueuedConnection);
    });
}

void AudioCd::refresh()
{
    m_lastDrive = -1;
    poll();
}

// ---------------------------------------------------------------- the contents

void AudioCd::readDisc()
{
    if (m_device.isEmpty())
        return;
    if (m_status != 3) { // a disc already shown stays until the new contents are there
        m_status = 2;
        emit discChanged();
    }
    const int gen = ++m_gen;
    QPointer<AudioCd> self(this);
    const QByteArray dev = QFile::encodeName(m_device);
    const QByteArray element = sourceElement().toLatin1();
    m_pool.start([self, gen, dev, element] {
        // The source reads the table of contents when it starts: track count, the
        // MusicBrainz disc id and, in its full form, where each track starts.
        QString discId, full;
        GstElement *src = gst_element_factory_make(element.constData(), nullptr);
        if (src)
            g_object_set(src, "device", dev.constData(), "track", 1, nullptr);
        GstElement *pipe = pipelineOf({src, gst_element_factory_make("fakesink", nullptr)});
        bool ok = pipe != nullptr;
        if (ok) {
            gst_element_set_state(pipe, GST_STATE_PAUSED);
            GstBus *bus = gst_element_get_bus(pipe);
            const qint64 until = QDateTime::currentMSecsSinceEpoch() + 30000; // a drive takes its time to spin up
            while (ok && full.isEmpty() && QDateTime::currentMSecsSinceEpoch() < until) {
                if (!self || self->m_gen.load() != gen)
                    break;
                GstMessage *msg = gst_bus_timed_pop_filtered(bus, 200 * GST_MSECOND,
                                                             GstMessageType(GST_MESSAGE_TAG | GST_MESSAGE_ERROR));
                if (!msg)
                    continue;
                if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
                    ok = false;
                } else {
                    GstTagList *tags = nullptr;
                    gst_message_parse_tag(msg, &tags);
                    gchar *s = nullptr;
                    if (gst_tag_list_get_string(tags, "musicbrainz-discid", &s)) {
                        discId = QString::fromLatin1(s);
                        g_free(s);
                    }
                    if (gst_tag_list_get_string(tags, "musicbrainz-discid-full", &s)) {
                        full = QString::fromLatin1(s);
                        g_free(s);
                    }
                    gst_tag_list_unref(tags);
                }
                gst_message_unref(msg);
            }
            gst_object_unref(bus);
            gst_element_set_state(pipe, GST_STATE_NULL);
        }
        if (pipe)
            gst_object_unref(pipe);
        // "first last leadout offset1 offset2 …" in hex, in sectors of 1/75 s
        QVector<Track> tracks;
        const QStringList parts = full.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() >= 4) {
            const int first = parts[0].toInt(nullptr, 16), last = parts[1].toInt(nullptr, 16);
            const qint64 leadout = parts[2].toLongLong(nullptr, 16);
            for (int n = first; n <= last && 3 + n - first < parts.size(); ++n) {
                const qint64 start = parts[3 + n - first].toLongLong(nullptr, 16);
                const qint64 end = n < last && 4 + n - first < parts.size() ? parts[4 + n - first].toLongLong(nullptr, 16) : leadout;
                Track t;
                t.number = n;
                t.durationMs = std::max<qint64>(0, end - start) * 1000 / 75;
                tracks << t;
            }
        }
        const bool audio = ok && !tracks.isEmpty();
        QMetaObject::invokeMethod(qApp, [self, gen, discId, tracks, audio] {
            if (self)
                self->discRead(gen, discId, tracks, audio);
        }, Qt::QueuedConnection);
    });
}

void AudioCd::discRead(int gen, const QString &discId, const QVector<Track> &tracks, bool audio)
{
    if (gen != m_gen.load())
        return;
    m_discId = audio ? discId : QString();
    m_tracks = audio ? tracks : QVector<Track>();
    m_album.clear();
    m_artist.clear();
    m_year = 0;
    m_coverFile.clear();
    m_status = audio ? 3 : 4;
    // names found or typed in before for this disc
    const QJsonObject o = audio && !m_discId.isEmpty() ? readNames(m_discId) : QJsonObject();
    bool known = false;
    if (!o.isEmpty()) {
        m_album = o.value(QLatin1String("album")).toString();
        m_artist = o.value(QLatin1String("artist")).toString();
        m_year = o.value(QLatin1String("year")).toInt();
        const QJsonArray t = o.value(QLatin1String("tracks")).toArray();
        for (int i = 0; i < m_tracks.size() && i < t.size(); ++i) {
            m_tracks[i].title = t[i].toObject().value(QLatin1String("title")).toString();
            m_tracks[i].artist = t[i].toObject().value(QLatin1String("artist")).toString();
        }
        known = o.value(QLatin1String("source")).toString() == QLatin1String("musicbrainz") || o.value(QLatin1String("edited")).toBool();
    }
    if (!m_discId.isEmpty() && QFileInfo::exists(cacheFile(m_discId, ".jpg")))
        m_coverFile = cacheFile(m_discId, ".jpg");
    publish();
    if (audio && !known && online())
        lookUp(m_discId);
}

void AudioCd::publish()
{
    External::CdDisc d;
    d.device = m_device;
    d.discId = m_discId;
    d.album = m_album;
    d.albumArtist = m_artist;
    d.year = m_year;
    d.coverFile = m_coverFile;
    for (const Track &t : std::as_const(m_tracks))
        d.tracks << External::CdTrack{t.number, t.title, t.artist, t.durationMs};
    External::setDisc(d);
    m_coverVersion = m_coverFile.isEmpty() ? QString() : QString::number(QFileInfo(m_coverFile).lastModified().toSecsSinceEpoch());
    TrackCache::instance().invalidate();
    emit discChanged();
}

QVariantList AudioCd::tracks() const
{
    QVariantList out;
    for (const Track &t : m_tracks)
        out << QVariantMap{
            {QStringLiteral("number"), t.number},
            {QStringLiteral("item"), External::cdItem(t.number)},
            {QStringLiteral("title"), t.title.isEmpty() ? tr("Titel %1").arg(t.number) : t.title},
            {QStringLiteral("artist"), t.artist.isEmpty() ? m_artist : t.artist},
            {QStringLiteral("duration"), t.durationMs},
        };
    return out;
}

qint64 AudioCd::coverItem() const
{
    return m_coverFile.isEmpty() || m_tracks.isEmpty() ? 0 : External::cdItem(m_tracks.first().number);
}

// ---------------------------------------------------------------- MusicBrainz

void AudioCd::lookUp(const QString &discId)
{
    if (discId.isEmpty())
        return;
    QPointer<AudioCd> self(this);
    const int gen = m_gen.load();
    const QByteArray env = qgetenv("HALCYON_MUSICBRAINZ_URL"); // tests: a local stand-in
    const QString mb = env.isEmpty() ? QStringLiteral("https://musicbrainz.org") : QString::fromUtf8(env);
    const QByteArray envArt = qgetenv("HALCYON_COVERART_URL");
    const QString art = envArt.isEmpty() ? QStringLiteral("https://coverartarchive.org") : QString::fromUtf8(envArt);
    const int count = int(m_tracks.size());
    m_pool.start([self, gen, discId, mb, art, count] {
        // a newer disc (or quitting) ends the request at once
        const auto stale = [self, gen] { return !self || self->m_gen.load() != gen; };
        int status = 0;
        const QUrl url(mb + QStringLiteral("/ws/2/discid/%1?inc=recordings+artist-credits&fmt=json").arg(discId));
        const QByteArray data = Http::get(url, {{"User-Agent", Http::userAgent()}, {"Accept", "application/json"}}, 15000, &status, stale);
        if (stale())
            return;
        // the release that has this very disc
        QJsonObject release, medium;
        for (const QJsonValue &r : QJsonDocument::fromJson(data).object().value(QLatin1String("releases")).toArray())
            for (const QJsonValue &m : r.toObject().value(QLatin1String("media")).toArray())
                for (const QJsonValue &d : m.toObject().value(QLatin1String("discs")).toArray())
                    if (release.isEmpty() && d.toObject().value(QLatin1String("id")).toString() == discId) {
                        release = r.toObject();
                        medium = m.toObject();
                    }
        if (release.isEmpty()) {
            QMetaObject::invokeMethod(qApp, [self, status] {
                if (self)
                    emit self->message(status == 404 || status == 200 ? tr("Diese CD kennt MusicBrainz nicht")
                                                                      : tr("MusicBrainz ist gerade nicht erreichbar"));
            }, Qt::QueuedConnection);
            return;
        }
        QJsonObject out;
        out.insert(QStringLiteral("source"), QStringLiteral("musicbrainz"));
        out.insert(QStringLiteral("release"), release.value(QLatin1String("id")).toString());
        out.insert(QStringLiteral("album"), release.value(QLatin1String("title")).toString());
        const QString albumArtist = joinCredit(release.value(QLatin1String("artist-credit")).toArray());
        out.insert(QStringLiteral("artist"), albumArtist);
        out.insert(QStringLiteral("year"), release.value(QLatin1String("date")).toString().left(4).toInt());
        QJsonArray tracks;
        for (const QJsonValue &t : medium.value(QLatin1String("tracks")).toArray()) {
            const QJsonObject o = t.toObject();
            const QString a = joinCredit(o.value(QLatin1String("artist-credit")).toArray());
            tracks.append(QJsonObject{{QStringLiteral("title"), o.value(QLatin1String("title")).toString()},
                                      {QStringLiteral("artist"), a == albumArtist ? QString() : a}});
        }
        while (tracks.size() > count && count > 0)
            tracks.removeLast();
        out.insert(QStringLiteral("tracks"), tracks);
        // the front cover from the Cover Art Archive
        const QByteArray cover = Http::get(QUrl(art + QStringLiteral("/release/%1/front-500").arg(out.value(QLatin1String("release")).toString())),
                                           {{"User-Agent", Http::userAgent()}}, 20000, &status, stale);
        if (!cover.isEmpty() && cover.size() < 8 * 1024 * 1024) {
            QDir().mkpath(QFileInfo(cacheFile(discId, ".jpg")).absolutePath());
            QSaveFile c(cacheFile(discId, ".jpg"));
            if (c.open(QIODevice::WriteOnly)) {
                c.write(cover);
                c.commit();
            }
        }
        QMetaObject::invokeMethod(qApp, [self, gen, discId, out] {
            // names typed in meanwhile win (written here, on the same thread as setNames)
            if (!readNames(discId).value(QLatin1String("edited")).toBool())
                writeNames(discId, out);
            if (self && self->m_gen.load() == gen && self->m_status == 3)
                self->discRead(gen, self->m_discId, self->m_tracks, true); // with the names now
        }, Qt::QueuedConnection);
    });
}

void AudioCd::setNames(const QString &album, const QString &artist, int year, const QStringList &titles)
{
    if (m_status != 3)
        return;
    m_album = album.trimmed();
    m_artist = artist.trimmed();
    m_year = std::clamp(year, 0, 9999);
    for (int i = 0; i < m_tracks.size() && i < titles.size(); ++i) {
        const QString t = titles[i].trimmed();
        // the stand-in name stays a stand-in
        m_tracks[i].title = t == tr("Titel %1").arg(m_tracks[i].number) ? QString() : t;
    }
    if (!m_discId.isEmpty()) {
        QJsonObject out = readNames(m_discId);
        out.insert(QStringLiteral("album"), m_album);
        out.insert(QStringLiteral("artist"), m_artist);
        out.insert(QStringLiteral("year"), m_year);
        QJsonArray tracks;
        for (const Track &t : std::as_const(m_tracks))
            tracks.append(QJsonObject{{QStringLiteral("title"), t.title}, {QStringLiteral("artist"), t.artist}});
        out.insert(QStringLiteral("tracks"), tracks);
        out.insert(QStringLiteral("edited"), true);
        writeNames(m_discId, out);
    }
    publish();
}

// ---------------------------------------------------------------- playing

void AudioCd::play(int index)
{
    if (m_status != 3 || m_tracks.isEmpty())
        return;
    QVector<qint64> items;
    for (const Track &t : std::as_const(m_tracks))
        items << External::cdItem(t.number);
    m_queue->setTracks(items, std::clamp(index, 0, int(items.size()) - 1), false);
    m_player->playCurrent();
}

void AudioCd::eject()
{
    if (m_ripping)
        return;
    if (External::isCd(m_player->currentId()))
        m_player->stop();
    if (!m_testImage) {
        const int fd = ::open(QFile::encodeName(m_device).constData(), O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            ::ioctl(fd, CDROM_LOCKDOOR, 0);
            if (::ioctl(fd, CDROMEJECT, 0) != 0)
                emit message(tr("Die CD lässt sich nicht auswerfen"));
            ::close(fd);
        }
    }
    m_lastDrive = 0;
    m_status = 1;
    m_tracks.clear();
    m_discId.clear();
    External::setDisc({});
    TrackCache::instance().invalidate();
    emit discChanged();
}

// ---------------------------------------------------------------- settings

bool AudioCd::online() const
{
    return QSettings().value(QStringLiteral("cd/online"), false).toBool();
}

void AudioCd::setOnline(bool on)
{
    if (on == online())
        return;
    QSettings().setValue(QStringLiteral("cd/online"), on);
    emit settingsChanged();
    if (on && m_status == 3 && m_album.isEmpty())
        lookUp(m_discId);
}

QString AudioCd::folder() const
{
    const QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    return QSettings().value(QStringLiteral("cd/folder"), music.isEmpty() ? QDir::homePath() : music).toString();
}

void AudioCd::setFolder(const QString &path)
{
    const QString p = QDir::cleanPath(Paths::localPath(path));
    if (p.isEmpty() || p == folder())
        return;
    QSettings().setValue(QStringLiteral("cd/folder"), p);
    emit settingsChanged();
}

// ---------------------------------------------------------------- reading into files

void AudioCd::cancelRip()
{
    m_ripCancel = true;
}

void AudioCd::rip()
{
    if (m_ripping || m_status != 3 || !m_canRip)
        return;
    const QString root = folder();
    QDir().mkpath(root); // the music folder of a fresh account may not be there yet
    if (!QFileInfo(root).isDir() || !QFileInfo(root).isWritable()) {
        emit message(tr("In den Ordner %1 kann nicht geschrieben werden").arg(root));
        return;
    }
    if (External::isCd(m_player->currentId()))
        m_player->stop(); // one drive cannot play and be read at once
    m_ripping = true;
    External::setCdBusy(true);
    m_ripCancel = false;
    m_ripProgress = 0;
    m_ripText = tr("Wird eingelesen …");
    emit ripChanged();

    const QString artist = fileName(m_artist, tr("Unbekannter Künstler"));
    const QString album = fileName(m_album, tr("Audio-CD"));
    const QString dir = root + QLatin1Char('/') + artist + QLatin1Char('/') + album;
    const QVector<Track> tracks = m_tracks;
    const QString albumName = m_album, albumArtist = m_artist, cover = m_coverFile;
    const int year = m_year;
    const QByteArray dev = QFile::encodeName(m_device);
    const QByteArray element = sourceElement().toLatin1();
    QPointer<AudioCd> self(this);
    m_pool.start([self, dir, tracks, albumName, albumArtist, cover, year, dev, element] {
        int written = 0, skipped = 0;
        QString error;
        QDir().mkpath(dir);
        removeLeftovers(dir);
        const int count = int(tracks.size());
        for (int i = 0; i < count && error.isEmpty(); ++i) {
            if (!self || self->m_ripCancel.load())
                break;
            const Track &t = tracks[i];
            const QString title = t.title.isEmpty() ? tr("Titel %1").arg(t.number) : t.title;
            const QString target = dir + QStringLiteral("/%1 %2.flac").arg(t.number, 2, 10, QLatin1Char('0')).arg(fileName(title, tr("Titel")));
            if (taken(target)) {
                ++skipped; // never overwritten
                continue;
            }
            // written under a name of our own; it gets its real one only when complete
            const QString part = partName(dir, ".flac");

            GstElement *src = gst_element_factory_make(element.constData(), nullptr);
            GstElement *enc = gst_element_factory_make("flacenc", nullptr);
            GstElement *sink = gst_element_factory_make("filesink", nullptr);
            if (src) {
                g_object_set(src, "device", dev.constData(), "track", guint(t.number), nullptr);
                if (element == "cdparanoiasrc")
                    g_object_set(src, "paranoia-mode", 0xff, nullptr); // full error correction
            }
            if (sink)
                g_object_set(sink, "location", QFile::encodeName(part).constData(), nullptr);
            if (enc) {
                GstTagList *tags = gst_tag_list_new_empty();
                gst_tag_list_add(tags, GST_TAG_MERGE_REPLACE, GST_TAG_TITLE, title.toUtf8().constData(), GST_TAG_TRACK_NUMBER,
                                 guint(t.number), GST_TAG_TRACK_COUNT, guint(count), nullptr);
                const QString trackArtist = t.artist.isEmpty() ? albumArtist : t.artist;
                if (!trackArtist.isEmpty())
                    gst_tag_list_add(tags, GST_TAG_MERGE_REPLACE, GST_TAG_ARTIST, trackArtist.toUtf8().constData(), nullptr);
                if (!albumArtist.isEmpty())
                    gst_tag_list_add(tags, GST_TAG_MERGE_REPLACE, GST_TAG_ALBUM_ARTIST, albumArtist.toUtf8().constData(), nullptr);
                if (!albumName.isEmpty())
                    gst_tag_list_add(tags, GST_TAG_MERGE_REPLACE, GST_TAG_ALBUM, albumName.toUtf8().constData(), nullptr);
                if (year > 0) {
                    GstDateTime *dt = gst_date_time_new_y(year);
                    gst_tag_list_add(tags, GST_TAG_MERGE_REPLACE, GST_TAG_DATE_TIME, dt, nullptr);
                    gst_date_time_unref(dt);
                }
                gst_tag_setter_merge_tags(GST_TAG_SETTER(enc), tags, GST_TAG_MERGE_REPLACE);
                gst_tag_list_unref(tags);
            }
            GstElement *pipe = pipelineOf({src, gst_element_factory_make("audioconvert", nullptr), enc, sink});
            if (!pipe) {
                error = tr("GStreamer-Elemente fehlen");
                break;
            }
            gst_element_set_state(pipe, GST_STATE_PLAYING);
            GstBus *bus = gst_element_get_bus(pipe);
            bool ok = true, done = false;
            qint64 lastReport = 0;
            while (ok && !done) {
                if (!self || self->m_ripCancel.load()) {
                    ok = false;
                    break;
                }
                GstMessage *msg = gst_bus_timed_pop_filtered(bus, 200 * GST_MSECOND, GstMessageType(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
                if (msg) {
                    if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
                        GError *err = nullptr;
                        gst_message_parse_error(msg, &err, nullptr);
                        error = err && err->message && *err->message ? QString::fromUtf8(err->message) : tr("Lesefehler");
                        g_clear_error(&err);
                        ok = false;
                    } else {
                        done = true;
                    }
                    gst_message_unref(msg);
                }
                gint64 pos = 0;
                const qint64 now = QDateTime::currentMSecsSinceEpoch();
                if (now - lastReport > 400 && gst_element_query_position(pipe, GST_FORMAT_TIME, &pos) && t.durationMs > 0) {
                    lastReport = now;
                    const double frac = std::clamp(double(pos / GST_MSECOND) / double(t.durationMs), 0.0, 1.0);
                    const double total = (i + frac) / count;
                    const QString text = tr("Titel %1 von %2: %3").arg(i + 1).arg(count).arg(title);
                    QMetaObject::invokeMethod(qApp, [self, total, text] {
                        if (self && self->m_ripping) {
                            self->m_ripProgress = total;
                            self->m_ripText = text;
                            emit self->ripChanged();
                        }
                    }, Qt::QueuedConnection);
                }
            }
            gst_object_unref(bus);
            gst_element_set_state(pipe, GST_STATE_NULL);
            gst_object_unref(pipe);
            if (!ok) {
                QFile::remove(part); // only our own unfinished file
                break;
            }
            QString placeError;
            if (place(part, target, &placeError))
                ++written;
            else if (placeError.isEmpty())
                ++skipped; // appeared meanwhile: left as it is
            else
                error = tr("%1 lässt sich nicht anlegen (%2)").arg(QFileInfo(target).fileName(), placeError);
        }
        const QString coverTarget = dir + QStringLiteral("/cover.jpg");
        if (!cover.isEmpty() && written > 0 && !taken(coverTarget)) {
            const QString part = partName(dir, ".jpg");
            QString ignored;
            if (QFile::copy(cover, part))
                place(part, coverTarget, &ignored);
            else
                QFile::remove(part);
        }
        const bool cancelled = !self || self->m_ripCancel.load();
        QMetaObject::invokeMethod(qApp, [self, written, skipped, error, cancelled, dir] {
            if (!self)
                return;
            self->m_ripping = false;
            External::setCdBusy(false);
            self->m_ripProgress = error.isEmpty() && !cancelled ? 1 : 0;
            QString text = written == 1 ? tr("1 Titel eingelesen") : tr("%1 Titel eingelesen").arg(written);
            if (skipped > 0)
                text += QStringLiteral(", ") + (skipped == 1 ? tr("1 Datei gab es schon (nicht verändert)")
                                                             : tr("%1 Dateien gab es schon (nicht verändert)").arg(skipped));
            if (cancelled)
                text = tr("Einlesen abgebrochen") + QStringLiteral(" – ") + text;
            else if (!error.isEmpty())
                text = tr("Einlesen fehlgeschlagen: %1").arg(error) + QStringLiteral(" – ") + text;
            self->m_ripText = text + QStringLiteral(" · ") + dir;
            emit self->ripChanged();
            emit self->message(text);
        }, Qt::QueuedConnection);
    });
}
