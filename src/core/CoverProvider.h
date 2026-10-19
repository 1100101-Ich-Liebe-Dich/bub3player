#pragma once

#include <QColor>
#include <QImage>
#include <QQuickAsyncImageProvider>
#include <QThreadPool>
#include <QVariantList>

// Serves album art as image://cover/<trackId>.
// Order: cover/folder/front image in the track's folder, then art embedded in the file.
// Artist pictures as image://cover/artist/<trackId>/<artist, percent-encoded>: an
// image in the artist's folder above the album folders (artist.jpg, folder.jpg …),
// from Jellyfin the artist's picture; without one the album art of the track.
// Decoded art is scaled once and cached as small JPEGs under ~/.cache, so scrolling a
// grid of 80 000 albums never decodes a 2000px original twice.
class CoverProvider : public QQuickAsyncImageProvider
{
public:
    CoverProvider();
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

    // Thread-safe helpers usable outside QML.
    static QImage load(qint64 trackId, int size);           // null image if none
    static QImage loadArtist(qint64 trackId, const QString &artist, int size); // null if no artist picture
    static QString cachedFile(qint64 trackId, int size);    // path of cached JPEG, empty if none
    static QColor dominantColor(const QImage &img);         // vivid accent colour, invalid if grey
    static QVariantList palette(const QImage &img);         // 4 colours: three vivid hues + a deep shade

private:
    QThreadPool m_pool;
};
