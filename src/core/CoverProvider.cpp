// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/CoverProvider.h"

#include "core/Database.h"
#include "core/Paths.h"
#include "core/TagReader.h"
#include "jellyfin/JellyfinAccess.h"
#include "library/External.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMutex>
#include <QRegularExpression>
#include <QRunnable>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <atomic>
#include <cmath>
#include <memory>

namespace {

QMutex g_mutex;
QHash<qint64, QString> g_folderCover; // folder id -> image path ("" = none)
QSet<QString> g_noArt;                // source keys known to have no art
QHash<QString, QString> g_artistImage; // track folder + artist -> image path ("" = none)
QHash<qint64, qint64> g_offlineUntil; // Jellyfin source -> retry time (ms since epoch)

QString cacheDir()
{
    static const QString dir = [] {
        const QString d = Paths::cacheDir() + QStringLiteral("/covers");
        QDir().mkpath(d);
        return d;
    }();
    return dir;
}

int bucketFor(int size)
{
    for (int b : {64, 128, 256, 512, 1024})
        if (size <= b)
            return b;
    return 1024;
}

QString findFolderImage(const QString &folder)
{
    QDir dir(folder);
    const QStringList images = dir.entryList({QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"), QStringLiteral("*.png"),
                                              QStringLiteral("*.webp"), QStringLiteral("*.JPG"), QStringLiteral("*.JPEG"),
                                              QStringLiteral("*.PNG")},
                                             QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
    if (images.isEmpty())
        return {};
    static const QStringList preferred = {QStringLiteral("cover"), QStringLiteral("folder"), QStringLiteral("front"),
                                          QStringLiteral("album"), QStringLiteral("albumart")};
    for (const QString &want : preferred)
        for (const QString &f : images)
            if (QFileInfo(f).completeBaseName().compare(want, Qt::CaseInsensitive) == 0)
                return dir.filePath(f);
    for (const QString &f : images) {
        const QString l = f.toLower();
        if (l.contains(QLatin1String("cover")) || l.contains(QLatin1String("front")))
            return dir.filePath(f);
    }
    // Only fall back to a random image if it is the only one (avoid booklet scans).
    return images.size() == 1 ? dir.filePath(images.first()) : QString();
}

QImage scaleSquare(QImageReader &reader, int size)
{
    const QSize orig = reader.size();
    if (orig.isValid() && orig.width() > 0 && orig.height() > 0) {
        // Let the decoder downscale (JPEG DCT scaling): far cheaper than decode-then-scale.
        QSize target = orig.scaled(size, size, Qt::KeepAspectRatioByExpanding);
        if (target.width() < orig.width())
            reader.setScaledSize(target);
    }
    QImage img = reader.read();
    if (img.isNull())
        return {};
    if (img.width() != size || img.height() != size) {
        img = img.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        img = img.copy((img.width() - size) / 2, (img.height() - size) / 2, size, size);
    }
    return img.convertToFormat(QImage::Format_RGB32);
}

struct Source
{
    QString key;      // stable cache key without size
    QString file;     // folder image, or audio file for embedded art
    bool embedded = false;
    qint64 remoteSource = 0; // Jellyfin: server, item id and image tag
    QString remoteItem, remoteTag;
    QString remoteArtist;    // Jellyfin: the artist's picture instead of the item's
};

QString hashOf(const QString &s)
{
    return QString::fromLatin1(QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
}

// Artist and folder names compare without case, accents, punctuation and a
// leading or trailing "The": "AC/DC" = "ACDC", "Cure, The" = "The Cure", "Björk" = "Bjork".
QString nameKey(const QString &name)
{
    const QString d = name.normalized(QString::NormalizationForm_KD).toCaseFolded();
    QString out;
    for (const QChar c : d)
        if (c.isLetterOrNumber())
            out += c;
    const QString t = name.trimmed();
    if (out.size() > 3 && t.startsWith(QLatin1String("the "), Qt::CaseInsensitive))
        out.remove(0, 3);
    else if (out.size() > 3 && t.endsWith(QLatin1String(", the"), Qt::CaseInsensitive))
        out.chop(3);
    return out;
}

QStringList imagesIn(const QDir &dir)
{
    return dir.entryList({QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"), QStringLiteral("*.png"), QStringLiteral("*.webp"),
                          QStringLiteral("*.JPG"), QStringLiteral("*.JPEG"), QStringLiteral("*.PNG"), QStringLiteral("*.WEBP")},
                         QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
}

// The picture in an artist's own folder: artist.jpg, folder.jpg, … or the only image there.
QString pickArtistImage(const QDir &dir, const QString &artist)
{
    const QStringList images = imagesIn(dir);
    if (images.isEmpty())
        return {};
    const QStringList preferred = {QStringLiteral("artist"), QStringLiteral("folder"), QStringLiteral("poster"),
                                   QStringLiteral("cover"), QStringLiteral("thumb"), artist};
    for (const QString &want : preferred)
        for (const QString &f : images)
            if (nameKey(QFileInfo(f).completeBaseName()) == nameKey(want))
                return dir.filePath(f);
    return images.size() == 1 ? dir.filePath(images.first()) : QString();
}

// "artist.jpg" (any case, jpg/jpeg/png/webp) in a folder – looked up by name,
// a big folder (the library root) is never listed for it.
QString probeArtistFile(const QDir &dir)
{
    for (const char *base : {"artist", "Artist", "ARTIST"})
        for (const char *ext : {"jpg", "jpeg", "png", "webp", "JPG", "JPEG", "PNG", "WEBP"}) {
            const QString f = dir.filePath(QStringLiteral("%1.%2").arg(QLatin1String(base), QLatin1String(ext)));
            if (QFileInfo::exists(f))
                return f;
        }
    return {};
}

// MUSIK/KÜNSTLER/ALBUM/titel.mp3 or MUSIK/KÜNSTLER/EPs/ALBUM/CD1/titel.mp3: of the
// three folders above the track's, the outermost one named like the artist (so
// MUSIK/Metallica/Metallica/CD1 finds the artist's folder, not the album's). The
// track's own folder only counts for loose tracks right in the artist's folder,
// and then only with an artist.jpg. No folder of that name, no picture: an
// image in a folder like "Sammlung" or "Various Artists" belongs to nobody in particular.
QString findArtistImage(const QString &trackFolder, const QString &artist)
{
    const QString want = nameKey(artist);
    if (want.isEmpty())
        return {};
    QDir d(trackFolder);
    QString outer;
    int outerLevel = 0;
    for (int level = 1; level <= 3 && d.cdUp(); ++level)
        if (nameKey(d.dirName()) == want) {
            outer = d.absolutePath();
            outerLevel = level;
        }
    // ".../Metallica/CD1": right above a disc folder is the album (a self-titled one),
    // its cover.jpg is no artist picture – only an artist.jpg there counts.
    static const QRegularExpression disc(QStringLiteral("^(cd|disc|disk|dis[ck]o|platte|seite|side)\\s*[0-9a-d]{1,2}\\b"),
                                         QRegularExpression::CaseInsensitiveOption);
    if (outerLevel == 1 && disc.match(QDir(trackFolder).dirName()).hasMatch())
        return probeArtistFile(QDir(outer));
    if (!outer.isEmpty())
        return pickArtistImage(QDir(outer), artist);
    d = QDir(trackFolder);
    return nameKey(d.dirName()) == want ? probeArtistFile(d) : QString();
}

// One database connection per decoder thread for both lookups.
DbConnection &coverDb()
{
    thread_local DbConnection db;
    if (!db.isOpen())
        db = Database::open(2048);
    return db;
}

// The artist's picture for a track of the artist. False when there is none
// (the caller falls back to the album art).
bool resolveArtist(qint64 trackId, const QString &artist, Source &src)
{
    DbConnection &db = coverDb();
    thread_local std::unique_ptr<Stmt> q;
    if (!q)
        q = std::make_unique<Stmt>(db, "SELECT f.path, t.source_id FROM tracks t JOIN folders f ON f.id = t.folder_id WHERE t.id = ?");
    q->bind(1, trackId);
    if (!q->step()) {
        q->reset();
        return false;
    }
    const QString folder = q->str(0);
    const qint64 sourceId = q->i64(1);
    q->reset();
    src = {};
    if (sourceId != Database::LocalSource) {
        src.remoteSource = sourceId;
        src.remoteArtist = artist;
        src.key = QStringLiteral("ja%1_%2").arg(sourceId).arg(hashOf(artist.toCaseFolded())); // refreshed weekly, see produce()
        return true;
    }
    const QString cacheKey = folder + QChar(0x1f) + artist;
    QString image;
    bool known = false;
    {
        QMutexLocker lock(&g_mutex);
        auto it = g_artistImage.constFind(cacheKey);
        if (it != g_artistImage.constEnd()) {
            known = true;
            image = *it;
        }
    }
    if (!known) {
        image = findArtistImage(folder, artist);
        QMutexLocker lock(&g_mutex);
        if (g_artistImage.size() > 20000)
            g_artistImage.clear();
        g_artistImage.insert(cacheKey, image);
    }
    if (image.isEmpty())
        return false;
    src.key = QStringLiteral("a%1_%2").arg(hashOf(image)).arg(QFileInfo(image).lastModified().toSecsSinceEpoch());
    src.file = image;
    return true;
}

bool resolve(qint64 trackId, Source &src)
{
    if (trackId < 0) {
        // radio: the station's logo; CD: the cover found for the disc
        const QString file = External::isStation(trackId) ? External::stationLogoFile(External::stationOf(trackId))
                                                          : External::disc().coverFile;
        const QFileInfo fi(file);
        if (file.isEmpty() || !fi.isFile())
            return false;
        src = {};
        src.key = QStringLiteral("x%1_%2").arg(hashOf(file)).arg(fi.lastModified().toSecsSinceEpoch());
        src.file = file;
        return true;
    }
    DbConnection &db = coverDb();
    thread_local std::unique_ptr<Stmt> q;
    if (!q)
        q = std::make_unique<Stmt>(db, "SELECT f.id, f.path, t.filename, t.mtime, t.source_id, t.image_ref FROM tracks t "
                                       "JOIN folders f ON f.id = t.folder_id WHERE t.id = ?");
    q->bind(1, trackId);
    if (!q->step()) {
        q->reset();
        return false;
    }
    const qint64 folderId = q->i64(0);
    const QString folder = q->str(1);
    const QString file = folder + QLatin1Char('/') + q->str(2);
    const qint64 mtime = q->i64(3);
    const qint64 sourceId = q->i64(4);
    const QString imageRef = q->str(5);
    q->reset();

    if (sourceId != Database::LocalSource) {
        // Jellyfin: "<itemId>:<imageTag>". The tag changes when the image does.
        const int colon = imageRef.indexOf(QLatin1Char(':'));
        if (imageRef.isEmpty() || colon <= 0)
            return false;
        src = {};
        src.remoteSource = sourceId;
        src.remoteItem = imageRef.left(colon);
        src.remoteTag = imageRef.mid(colon + 1);
        QString tagKey = src.remoteTag.left(16);
        tagKey.remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9]")));
        src.key = QStringLiteral("j%1_%2_%3").arg(sourceId).arg(src.remoteItem.left(40), tagKey);
        return true;
    }

    QString folderImage;
    bool known = false;
    {
        QMutexLocker lock(&g_mutex);
        auto it = g_folderCover.constFind(folderId);
        if (it != g_folderCover.constEnd()) {
            known = true;
            folderImage = *it;
        }
    }
    if (!known) {
        folderImage = findFolderImage(folder);
        QMutexLocker lock(&g_mutex);
        // Bounded: with huge libraries this map would otherwise only ever grow.
        if (g_folderCover.size() > 20000)
            g_folderCover.clear();
        g_folderCover.insert(folderId, folderImage);
    }

    if (!folderImage.isEmpty()) {
        const qint64 im = QFileInfo(folderImage).lastModified().toSecsSinceEpoch();
        src = {};
        src.key = QStringLiteral("f%1_%2").arg(hashOf(folderImage)).arg(im);
        src.file = folderImage;
    } else {
        src = {};
        src.key = QStringLiteral("t%1_%2").arg(hashOf(file)).arg(mtime);
        src.file = file;
        src.embedded = true;
    }
    return true;
}

QImage produce(const Source &src, int bucket, QString *outPath)
{
    const QString cached = cacheDir() + QLatin1Char('/') + src.key + QLatin1Char('_') + QString::number(bucket) + QStringLiteral(".jpg");
    {
        QMutexLocker lock(&g_mutex);
        if (g_noArt.contains(src.key))
            return {};
    }
    // A Jellyfin artist picture has no tag to notice changes by: refreshed (same
    // file) when older than a week; the old one stays if the server is away.
    QImage old;
    if (QFileInfo::exists(cached)) {
        QImage img(cached);
        const bool stale = !src.remoteArtist.isEmpty()
                           && QFileInfo(cached).lastModified().secsTo(QDateTime::currentDateTime()) > 7 * 86400;
        if (!img.isNull() && !stale) {
            if (outPath)
                *outPath = cached;
            return img;
        }
        old = img;
    }

    QImage img;
    if (src.remoteSource > 0) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        {
            QMutexLocker lock(&g_mutex);
            if (g_offlineUntil.value(src.remoteSource) > now)
                return {}; // server unreachable a moment ago: don't block the decoder threads
        }
        JellyfinAccess::Server server;
        if (!JellyfinAccess::get(src.remoteSource, server))
            return {};
        int status = 0;
        QByteArray bytes;
        if (src.remoteArtist.isEmpty()) {
            bytes = JellyfinAccess::httpGet(JellyfinAccess::imageUrl(src.remoteSource, src.remoteItem, src.remoteTag, bucket),
                                            server.token, 8000, &status);
        } else {
            // The artist's item by name (works for "AC/DC" too), then its picture.
            const QByteArray found = JellyfinAccess::httpGet(JellyfinAccess::artistSearchUrl(src.remoteSource, src.remoteArtist),
                                                             server.token, 8000, &status);
            QString id, tag;
            for (const QJsonValue v : QJsonDocument::fromJson(found).object().value(QStringLiteral("Items")).toArray()) {
                const QJsonObject o = v.toObject();
                if (o.value(QStringLiteral("Name")).toString().compare(src.remoteArtist, Qt::CaseInsensitive) == 0) {
                    id = o.value(QStringLiteral("Id")).toString();
                    tag = o.value(QStringLiteral("ImageTags")).toObject().value(QStringLiteral("Primary")).toString();
                    break;
                }
            }
            if (!id.isEmpty() && !tag.isEmpty())
                bytes = JellyfinAccess::httpGet(JellyfinAccess::imageUrl(src.remoteSource, id, tag, bucket), server.token, 8000, &status);
        }
        if (status == 0 || status >= 500) {
            QMutexLocker lock(&g_mutex);
            g_offlineUntil.insert(src.remoteSource, now + 60000);
            return {};
        }
        if (!bytes.isEmpty()) {
            QBuffer buf(&bytes);
            buf.open(QIODevice::ReadOnly);
            QImageReader reader(&buf);
            img = scaleSquare(reader, bucket);
        }
    } else if (src.embedded) {
        QByteArray bytes = TagReader::embeddedCover(src.file);
        if (!bytes.isEmpty()) {
            QBuffer buf(&bytes);
            buf.open(QIODevice::ReadOnly);
            QImageReader reader(&buf);
            img = scaleSquare(reader, bucket);
        }
    } else {
        QImageReader reader(src.file);
        img = scaleSquare(reader, bucket);
    }
    if (img.isNull() && !old.isNull()) {
        if (outPath)
            *outPath = cached;
        return old;
    }
    if (img.isNull()) {
        QMutexLocker lock(&g_mutex);
        if (g_noArt.size() > 50000)
            g_noArt.clear();
        g_noArt.insert(src.key);
        return {};
    }
    if (img.save(cached, "JPG", 90) && outPath)
        *outPath = cached;
    return img;
}

class CoverResponse : public QQuickImageResponse, public QRunnable
{
public:
    CoverResponse(qint64 id, QString artist, QSize size) : m_id(id), m_artist(std::move(artist)), m_size(size) { setAutoDelete(false); }

    QQuickTextureFactory *textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

    // Fast scrolling cancels requests for rows that already left the screen.
    void cancel() override { m_cancelled = true; }

    void run() override
    {
        if (m_cancelled.load()) {
            m_image = QImage(1, 1, QImage::Format_ARGB32);
            m_image.fill(Qt::transparent);
            emit finished();
            return;
        }
        const int want = std::max(m_size.width(), m_size.height());
        if (!m_artist.isEmpty())
            m_image = CoverProvider::loadArtist(m_id, m_artist, want > 0 ? want : 256);
        if (m_image.isNull()) // no artist picture: the album art of the track
            m_image = CoverProvider::load(m_id, want > 0 ? want : 256);
        if (m_image.isNull()) {
            // Transparent pixel: the QML placeholder underneath stays visible, no error spam.
            m_image = QImage(1, 1, QImage::Format_ARGB32);
            m_image.fill(Qt::transparent);
        }
        emit finished();
    }

private:
    qint64 m_id;
    QString m_artist;
    QSize m_size;
    QImage m_image;
    std::atomic<bool> m_cancelled{false};
};

} // namespace

CoverProvider::CoverProvider()
{
    // Two decoders at most: covers must never compete with playback for disk time.
    m_pool.setMaxThreadCount(2);
}

QQuickImageResponse *CoverProvider::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    // "<trackId>" or "artist/<trackId>/<artist, percent-encoded>"
    const QString path = id.section(QLatin1Char('?'), 0, 0);
    qint64 trackId = 0;
    QString artist;
    if (path.startsWith(QLatin1String("artist/"))) {
        trackId = path.section(QLatin1Char('/'), 1, 1).toLongLong();
        artist = QUrl::fromPercentEncoding(path.section(QLatin1Char('/'), 2).toUtf8());
    } else {
        trackId = path.toLongLong();
    }
    auto *r = new CoverResponse(trackId, artist, requestedSize);
    m_pool.start(r);
    return r;
}

QImage CoverProvider::load(qint64 trackId, int size)
{
    if (trackId == 0)
        return {};
    try {
        Source src;
        if (!resolve(trackId, src))
            return {};
        return produce(src, bucketFor(size), nullptr);
    } catch (...) {
        return {};
    }
}

QImage CoverProvider::loadArtist(qint64 trackId, const QString &artist, int size)
{
    if (trackId <= 0 || artist.trimmed().isEmpty())
        return {};
    try {
        Source src;
        if (!resolveArtist(trackId, artist, src))
            return {};
        return produce(src, bucketFor(size), nullptr);
    } catch (...) {
        return {};
    }
}

QString CoverProvider::cachedFile(qint64 trackId, int size)
{
    if (trackId == 0)
        return {};
    try {
        Source src;
        if (!resolve(trackId, src))
            return {};
        QString path;
        produce(src, bucketFor(size), &path);
        return path;
    } catch (...) {
        return {};
    }
}

QColor CoverProvider::dominantColor(const QImage &source)
{
    if (source.isNull())
        return {};
    const QImage img = source.scaled(24, 24, Qt::IgnoreAspectRatio, Qt::FastTransformation).convertToFormat(QImage::Format_RGB32);
    // Hue histogram weighted by saturation and brightness: picks the most vivid
    // colour family instead of the (usually muddy) average.
    double weight[12] = {0};
    double r[12] = {0}, g[12] = {0}, b[12] = {0};
    for (int y = 0; y < img.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QColor c(line[x]);
            const double s = c.hsvSaturationF(), v = c.valueF();
            if (s < 0.25 || v < 0.2)
                continue;
            const int h = c.hsvHue() < 0 ? 0 : (c.hsvHue() * 12 / 360) % 12;
            const double w = s * s * v;
            weight[h] += w;
            r[h] += c.redF() * w;
            g[h] += c.greenF() * w;
            b[h] += c.blueF() * w;
        }
    }
    int best = -1;
    for (int i = 0; i < 12; ++i)
        if (weight[i] > 0.6 && (best < 0 || weight[i] > weight[best]))
            best = i;
    if (best < 0)
        return {};
    return QColor::fromRgbF(float(r[best] / weight[best]), float(g[best] / weight[best]), float(b[best] / weight[best]));
}

QVariantList CoverProvider::palette(const QImage &source)
{
    QVariantList out;
    if (source.isNull())
        return out;
    const QImage img = source.scaled(32, 32, Qt::IgnoreAspectRatio, Qt::FastTransformation).convertToFormat(QImage::Format_RGB32);
    // 18 hue buckets (20 degrees), weighted by how vivid each pixel is.
    constexpr int N = 18;
    double w[N] = {0}, r[N] = {0}, g[N] = {0}, b[N] = {0};
    double dr = 0, dg = 0, db = 0, dw = 0;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QColor c(line[x]);
            const double s = c.hsvSaturationF(), v = c.valueF();
            if (v < 0.35) { // collect the dark tones separately
                dr += c.redF(); dg += c.greenF(); db += c.blueF(); dw += 1;
            }
            if (s < 0.18 || v < 0.15)
                continue;
            const int h = std::max(0, c.hsvHue()) * N / 360 % N;
            const double wt = s * s * (0.3 + v);
            w[h] += wt;
            r[h] += c.redF() * wt;
            g[h] += c.greenF() * wt;
            b[h] += c.blueF() * wt;
        }
    }
    // Pick up to three buckets that are at least 3 buckets (60 degrees) apart.
    QVector<int> picked;
    for (int k = 0; k < 3; ++k) {
        int best = -1;
        for (int i = 0; i < N; ++i) {
            if (w[i] < 0.4)
                continue;
            bool near = false;
            for (int p : picked) {
                const int d = std::abs(p - i);
                if (std::min(d, N - d) < 3)
                    near = true;
            }
            if (!near && (best < 0 || w[i] > w[best]))
                best = i;
        }
        if (best < 0)
            break;
        picked << best;
    }
    QVector<QColor> cols;
    for (int i : picked)
        cols << QColor::fromRgbF(float(r[i] / w[i]), float(g[i] / w[i]), float(b[i] / w[i]));
    if (cols.isEmpty())
        return out; // greyscale cover: let the skin use its own colours
    // Fill missing slots with hue-shifted siblings of the main colour.
    while (cols.size() < 3) {
        float h, sat, l, a;
        cols.first().getHslF(&h, &sat, &l, &a);
        cols << QColor::fromHslF(std::fmod(h + (cols.size() == 1 ? 0.12f : 0.88f), 1.0f), sat, l);
    }
    for (const QColor &c : cols)
        out << c;
    out << (dw > 0 ? QColor::fromRgbF(float(dr / dw), float(dg / dw), float(db / dw)) : cols.first().darker(300));
    return out;
}
