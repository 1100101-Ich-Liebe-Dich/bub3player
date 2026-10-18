#pragma once

#include <QColor>
#include <QImage>
#include <QQuickAsyncImageProvider>
#include <QThreadPool>

// Serves album art as image://cover/<trackId>.
// Order: cover/folder/front image in the track's folder, then art embedded in the file.
// Decoded art is scaled once and cached as small JPEGs under ~/.cache, so scrolling a
// grid of 80 000 albums never decodes a 2000px original twice.
class CoverProvider : public QQuickAsyncImageProvider
{
public:
    CoverProvider();
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

    // Thread-safe helpers usable outside QML.
    static QImage load(qint64 trackId, int size);           // null image if none
    static QString cachedFile(qint64 trackId, int size);    // path of cached JPEG, empty if none
    static QColor dominantColor(const QImage &img);         // vivid accent colour, invalid if grey

private:
    QThreadPool m_pool;
};
