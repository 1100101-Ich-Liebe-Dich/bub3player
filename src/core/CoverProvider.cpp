#include "core/CoverProvider.h"

#include "core/Database.h"
#include "core/Paths.h"
#include "core/TagReader.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QMutex>
#include <QRunnable>
#include <QSet>
#include <QStandardPaths>
#include <atomic>
#include <cmath>
#include <memory>

namespace {

QMutex g_mutex;
QHash<qint64, QString> g_folderCover; // folder id -> image path ("" = none)
QSet<QString> g_noArt;                // source keys known to have no art

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
};

bool resolve(qint64 trackId, Source &src)
{
    thread_local DbConnection db;
    thread_local std::unique_ptr<Stmt> q;
    if (!db.isOpen()) {
        db = Database::open(2048);
        q = std::make_unique<Stmt>(db, "SELECT f.id, f.path, t.filename, t.mtime FROM tracks t "
                                       "JOIN folders f ON f.id = t.folder_id WHERE t.id = ?");
    }
    q->bind(1, trackId);
    if (!q->step()) {
        q->reset();
        return false;
    }
    const qint64 folderId = q->i64(0);
    const QString folder = q->str(1);
    const QString file = folder + QLatin1Char('/') + q->str(2);
    const qint64 mtime = q->i64(3);
    q->reset();

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
        g_folderCover.insert(folderId, folderImage);
    }

    auto hash = [](const QString &s) {
        return QString::fromLatin1(QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
    };
    if (!folderImage.isEmpty()) {
        const qint64 im = QFileInfo(folderImage).lastModified().toSecsSinceEpoch();
        src = {QStringLiteral("f%1_%2").arg(hash(folderImage)).arg(im), folderImage, false};
    } else {
        src = {QStringLiteral("t%1_%2").arg(hash(file)).arg(mtime), file, true};
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
    if (QFileInfo::exists(cached)) {
        QImage img(cached);
        if (!img.isNull()) {
            if (outPath)
                *outPath = cached;
            return img;
        }
    }

    QImage img;
    if (src.embedded) {
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
    if (img.isNull()) {
        QMutexLocker lock(&g_mutex);
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
    CoverResponse(qint64 id, QSize size) : m_id(id), m_size(size) { setAutoDelete(false); }

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
    const qint64 trackId = id.section(QLatin1Char('?'), 0, 0).toLongLong();
    auto *r = new CoverResponse(trackId, requestedSize);
    m_pool.start(r);
    return r;
}

QImage CoverProvider::load(qint64 trackId, int size)
{
    if (trackId <= 0)
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

QString CoverProvider::cachedFile(qint64 trackId, int size)
{
    if (trackId <= 0)
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
