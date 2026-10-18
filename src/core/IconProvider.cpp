#include "core/IconProvider.h"

#include <QHash>
#include <QPainter>
#include <QSvgRenderer>

namespace {

const QHash<QString, const char *> &paths()
{
    static const QHash<QString, const char *> p = {
        {QStringLiteral("play"), "M8 5.14v13.72a1 1 0 0 0 1.52.85l10.6-6.86a1 1 0 0 0 0-1.7L9.52 4.29A1 1 0 0 0 8 5.14z"},
        {QStringLiteral("pause"), "M7 4.5h3.2a.8.8 0 0 1 .8.8v13.4a.8.8 0 0 1-.8.8H7a.8.8 0 0 1-.8-.8V5.3a.8.8 0 0 1 .8-.8zm6.8 0H17a.8.8 0 0 1 .8.8v13.4a.8.8 0 0 1-.8.8h-3.2a.8.8 0 0 1-.8-.8V5.3a.8.8 0 0 1 .8-.8z"},
        {QStringLiteral("next"), "M5 6.3v11.4a.8.8 0 0 0 1.25.66L14.5 12.7a.8.8 0 0 0 0-1.4L6.25 5.64A.8.8 0 0 0 5 6.3zM16.5 5.5h2a.5.5 0 0 1 .5.5v12a.5.5 0 0 1-.5.5h-2a.5.5 0 0 1-.5-.5V6a.5.5 0 0 1 .5-.5z"},
        {QStringLiteral("prev"), "M19 6.3v11.4a.8.8 0 0 1-1.25.66L9.5 12.7a.8.8 0 0 1 0-1.4l8.25-5.66A.8.8 0 0 1 19 6.3zM7.5 5.5h-2a.5.5 0 0 0-.5.5v12a.5.5 0 0 0 .5.5h2a.5.5 0 0 0 .5-.5V6a.5.5 0 0 0-.5-.5z"},
        {QStringLiteral("shuffle"), "M10.59 9.17L5.41 4 4 5.41l5.17 5.17 1.42-1.41zM14.5 4l2.04 2.04L4 18.59 5.41 20 17.96 7.46 20 9.5V4h-5.5zm.33 9.41l-1.41 1.41 3.13 3.13L14.5 20H20v-5.5l-2.04 2.04-3.13-3.13z"},
        {QStringLiteral("repeat"), "M7 7h10v3l4-4-4-4v3H5v6h2V7zm10 10H7v-3l-4 4 4 4v-3h12v-6h-2v4z"},
        {QStringLiteral("repeat-one"), "M7 7h10v3l4-4-4-4v3H5v6h2V7zm10 10H7v-3l-4 4 4 4v-3h12v-6h-2v4zm-4-2V9h-1l-2 1v1h1.5v4H13z"},
        {QStringLiteral("volume"), "M3 9v6h4l5 5V4L7 9H3zm13.5 3c0-1.77-1.02-3.29-2.5-4.03v8.05c1.48-.73 2.5-2.25 2.5-4.02zM14 3.23v2.06c2.89.86 5 3.54 5 6.71s-2.11 5.85-5 6.71v2.06c4.01-.91 7-4.49 7-8.77s-2.99-7.86-7-8.77z"},
        {QStringLiteral("volume-low"), "M3 9v6h4l5 5V4L7 9H3zm13.5 3c0-1.77-1.02-3.29-2.5-4.03v8.05c1.48-.73 2.5-2.25 2.5-4.02z"},
        {QStringLiteral("volume-off"), "M16.5 12c0-1.77-1.02-3.29-2.5-4.03v2.21l2.45 2.45c.03-.2.05-.41.05-.63zm2.5 0c0 .94-.2 1.82-.54 2.64l1.51 1.51C20.63 14.91 21 13.5 21 12c0-4.28-2.99-7.86-7-8.77v2.06c2.89.86 5 3.54 5 6.71zM4.27 3L3 4.27 7.73 9H3v6h4l5 5v-6.73l4.25 4.25c-.67.52-1.42.93-2.25 1.18v2.06c1.38-.31 2.63-.95 3.69-1.81L19.73 21 21 19.73l-9-9L4.27 3zM12 4L9.91 6.09 12 8.18V4z"},
        {QStringLiteral("queue"), "M15 6H3v2h12V6zm0 4H3v2h12v-2zM3 16h8v-2H3v2zM17 6v8.18c-.31-.11-.65-.18-1-.18-1.66 0-3 1.34-3 3s1.34 3 3 3 3-1.34 3-3V8h3V6h-5z"},
        {QStringLiteral("search"), "M15.5 14h-.79l-.28-.27A6.47 6.47 0 0 0 16 9.5 6.5 6.5 0 1 0 9.5 16c1.61 0 3.09-.59 4.23-1.57l.27.28v.79l5 4.99L20.49 19l-4.99-5zm-6 0C7.01 14 5 11.99 5 9.5S7.01 5 9.5 5 14 7.01 14 9.5 11.99 14 9.5 14z"},
        {QStringLiteral("folder"), "M10 4H4c-1.1 0-1.99.9-1.99 2L2 18c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2h-8l-2-2z"},
        {QStringLiteral("album"), "M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm0 14.5c-2.49 0-4.5-2.01-4.5-4.5S9.51 7.5 12 7.5s4.5 2.01 4.5 4.5-2.01 4.5-4.5 4.5zm0-5.5c-.55 0-1 .45-1 1s.45 1 1 1 1-.45 1-1-.45-1-1-1z"},
        {QStringLiteral("artist"), "M12 12c2.21 0 4-1.79 4-4s-1.79-4-4-4-4 1.79-4 4 1.79 4 4 4zm0 2c-2.67 0-8 1.34-8 4v2h16v-2c0-2.66-5.33-4-8-4z"},
        {QStringLiteral("note"), "M12 3v10.55c-.59-.34-1.27-.55-2-.55-2.21 0-4 1.79-4 4s1.79 4 4 4 4-1.79 4-4V7h4V3h-6z"},
        {QStringLiteral("tag"), "M21.41 11.58l-9-9C12.05 2.22 11.55 2 11 2H4c-1.1 0-2 .9-2 2v7c0 .55.22 1.05.59 1.42l9 9c.36.36.86.58 1.41.58.55 0 1.05-.22 1.41-.59l7-7c.37-.36.59-.86.59-1.41 0-.55-.23-1.06-.59-1.42zM5.5 7C4.67 7 4 6.33 4 5.5S4.67 4 5.5 4 7 4.67 7 5.5 6.33 7 5.5 7z"},
        {QStringLiteral("clock"), "M11.99 2C6.47 2 2 6.48 2 12s4.47 10 9.99 10C17.52 22 22 17.52 22 12S17.52 2 11.99 2zM12 20c-4.42 0-8-3.58-8-8s3.58-8 8-8 8 3.58 8 8-3.58 8-8 8zm.5-13H11v6l5.25 3.15.75-1.23-4.5-2.67z"},
        {QStringLiteral("calendar"), "M19 4h-1V2h-2v2H8V2H6v2H5c-1.11 0-1.99.9-1.99 2L3 20a2 2 0 0 0 2 2h14c1.1 0 2-.9 2-2V6c0-1.1-.9-2-2-2zm0 16H5V9h14v11z"},
        {QStringLiteral("history"), "M13 3a9 9 0 0 0-9 9H1l3.89 3.89.07.14L9 12H6c0-3.87 3.13-7 7-7s7 3.13 7 7-3.13 7-7 7c-1.93 0-3.68-.79-4.94-2.06l-1.42 1.42A8.954 8.954 0 0 0 13 21a9 9 0 0 0 0-18zm-1 5v5l4.28 2.54.72-1.21-3.5-2.08V8H12z"},
        {QStringLiteral("trending"), "M16 6l2.29 2.29-4.88 4.88-4-4L2 16.59 3.41 18l6-6 4 4 6.3-6.29L22 12V6z"},
        {QStringLiteral("eq"), "M7 18h2V6H7v12zm4 4h2V2h-2v20zm-8-8h2v-4H3v4zm12 4h2V6h-2v12zm4-8v4h2v-4h-2z"},
        {QStringLiteral("tune"), "M3 17v2h6v-2H3zM3 5v2h10V5H3zm10 16v-2h8v-2h-8v-2h-2v6h2zM7 9v2H3v2h4v2h2V9H7zm14 4v-2H11v2h10zm-6-4h2V7h4V5h-4V3h-2v6z"},
        {QStringLiteral("palette"), "M12 3a9 9 0 0 0 0 18c.83 0 1.5-.67 1.5-1.5 0-.39-.15-.74-.39-1.01-.23-.26-.38-.61-.38-.99 0-.83.67-1.5 1.5-1.5H16c2.76 0 5-2.24 5-5 0-4.42-4.03-8-9-8zm-5.5 9a1.5 1.5 0 1 1 0-3 1.5 1.5 0 0 1 0 3zm3-4a1.5 1.5 0 1 1 0-3 1.5 1.5 0 0 1 0 3zm5 0a1.5 1.5 0 1 1 0-3 1.5 1.5 0 0 1 0 3zm3 4a1.5 1.5 0 1 1 0-3 1.5 1.5 0 0 1 0 3z"},
        {QStringLiteral("down"), "M16.59 8.59L12 13.17 7.41 8.59 6 10l6 6 6-6z"},
        {QStringLiteral("up"), "M7.41 15.41L12 10.83l4.59 4.58L18 14l-6-6-6 6z"},
        {QStringLiteral("close"), "M19 6.41L17.59 5 12 10.59 6.41 5 5 6.41 10.59 12 5 17.59 6.41 19 12 13.41 17.59 19 19 17.59 13.41 12z"},
        {QStringLiteral("chevron"), "M10 6L8.59 7.41 13.17 12l-4.58 4.59L10 18l6-6z"},
        {QStringLiteral("back"), "M20 11H7.83l5.59-5.59L12 4l-8 8 8 8 1.41-1.41L7.83 13H20v-2z"},
        {QStringLiteral("refresh"), "M17.65 6.35A7.958 7.958 0 0 0 12 4c-4.42 0-7.99 3.58-7.99 8s3.57 8 7.99 8c3.73 0 6.84-2.55 7.73-6h-2.08A5.99 5.99 0 0 1 12 18c-3.31 0-6-2.69-6-6s2.69-6 6-6c1.66 0 3.14.69 4.22 1.78L13 11h7V4l-2.35 2.35z"},
        {QStringLiteral("add"), "M19 13h-6v6h-2v-6H5v-2h6V5h2v6h6v2z"},
        {QStringLiteral("delete"), "M6 19c0 1.1.9 2 2 2h8c1.1 0 2-.9 2-2V7H6v12zM19 4h-3.5l-1-1h-5l-1 1H5v2h14V4z"},
        {QStringLiteral("expand"), "M7 14H5v5h5v-2H7v-3zm-2-4h2V7h3V5H5v5zm12 7h-3v2h5v-5h-2v3zM14 5v2h3v3h2V5h-5z"},
        {QStringLiteral("collapse"), "M5 16h3v3h2v-5H5v2zm3-8H5v2h5V5H8v3zm6 11h2v-3h3v-2h-5v5zm2-11V5h-2v5h5V8h-3z"},
        {QStringLiteral("new"), "M23 12l-2.44-2.78.34-3.68-3.61-.82-1.89-3.18L12 3 8.6 1.54 6.71 4.72l-3.61.81.34 3.68L1 12l2.44 2.78-.34 3.69 3.61.82 1.89 3.18L12 21l3.4 1.46 1.89-3.18 3.61-.82-.34-3.68L23 12zm-10 5h-2v-2h2v2zm0-4h-2V7h2v6z"},
        {QStringLiteral("hourglass"), "M6 2v6h.01L6 8.01 10 12l-4 4 .01.01H6V22h12v-5.99h-.01L18 16l-4-4 4-3.99-.01-.01H18V2H6zm10 14.5V20H8v-3.5l4-4 4 4zm-4-5l-4-4V4h8v3.5l-4 4z"},
        {QStringLiteral("file"), "M6 2c-1.1 0-1.99.9-1.99 2L4 20c0 1.1.89 2 1.99 2H18c1.1 0 2-.9 2-2V8l-6-6H6zm7 7V3.5L18.5 9H13z"},
        {QStringLiteral("music-off"), "M4.27 3L3 4.27l9 9v.28c-.59-.34-1.27-.55-2-.55-2.21 0-4 1.79-4 4s1.79 4 4 4 4-1.79 4-4v-1.73L19.73 21 21 19.73 4.27 3zM14 7h4V3h-6v5.18l2 2z"},
        {QStringLiteral("layers"), "M11.99 18.54l-7.37-5.73L3 14.07l9 7 9-7-1.63-1.27-7.38 5.74zM12 16l7.36-5.73L21 9l-9-7-9 7 1.63 1.27L12 16z"},
        {QStringLiteral("grid"), "M4 11h5V5H4v6zm0 7h5v-6H4v6zm6 0h5v-6h-5v6zm6 0h5v-6h-5v6zm-6-7h5V5h-5v6zm6-6v6h5V5h-5z"},
        {QStringLiteral("list"), "M3 13h2v-2H3v2zm0 4h2v-2H3v2zm0-8h2V7H3v2zm4 4h14v-2H7v2zm0 4h14v-2H7v2zM7 7v2h14V7H7z"},
        {QStringLiteral("play-circle"), "M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm-2 14.5v-9l6 4.5-6 4.5z"},
        {QStringLiteral("more"), "M6 10c-1.1 0-2 .9-2 2s.9 2 2 2 2-.9 2-2-.9-2-2-2zm12 0c-1.1 0-2 .9-2 2s.9 2 2 2 2-.9 2-2-.9-2-2-2zm-6 0c-1.1 0-2 .9-2 2s.9 2 2 2 2-.9 2-2-.9-2-2-2z"},
        {QStringLiteral("playlist-add"), "M14 10H2v2h12v-2zm0-4H2v2h12V6zm4 8v-4h-2v4h-4v2h4v4h2v-4h4v-2h-4zM2 16h8v-2H2v2z"},
        {QStringLiteral("server"), "M20 13H4c-.55 0-1 .45-1 1v6c0 .55.45 1 1 1h16c.55 0 1-.45 1-1v-6c0-.55-.45-1-1-1zM7 19c-1.1 0-2-.9-2-2s.9-2 2-2 2 .9 2 2-.9 2-2 2zM20 3H4c-.55 0-1 .45-1 1v6c0 .55.45 1 1 1h16c.55 0 1-.45 1-1V4c0-.55-.45-1-1-1zM7 9c-1.1 0-2-.9-2-2s.9-2 2-2 2 .9 2 2-.9 2-2 2z"},
        {QStringLiteral("sparkle"), "M19 9l1.25-2.75L23 5l-2.75-1.25L19 1l-1.25 2.75L15 5l2.75 1.25L19 9zm-7.5.5L9 4 6.5 9.5 1 12l5.5 2.5L9 20l2.5-5.5L17 12l-5.5-2.5zM19 15l-1.25 2.75L15 19l2.75 1.25L19 23l1.25-2.75L23 19l-2.75-1.25L19 15z"},
    };
    return p;
}

} // namespace

IconProvider::IconProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage IconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const QString name = id.section(QLatin1Char('/'), 0, 0);
    if (name == QLatin1String("mesh")) {
        // Fine dot mesh in front of a vacuum-fluorescent display.
        QImage img(4, 4, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        for (int i = 0; i < 4; ++i) {
            img.setPixel(i, 3, qRgba(0, 0, 0, 200));
            img.setPixel(3, i, qRgba(0, 0, 0, 200));
        }
        if (size)
            *size = img.size();
        return img;
    }
    if (name == QLatin1String("noise")) {
        // Tileable film grain for skins (fixed seed: identical on every start).
        QImage img(128, 128, QImage::Format_ARGB32_Premultiplied);
        quint32 seed = 0x9E3779B9u;
        for (int y = 0; y < img.height(); ++y) {
            QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
                const int v = int(seed & 0xFF);
                line[x] = qRgba(v, v, v, 255);
            }
        }
        if (size)
            *size = img.size();
        return img;
    }
    QString color = id.section(QLatin1Char('/'), 1, 1);
    if (color.isEmpty())
        color = QStringLiteral("ffffff");
    // QML colours may arrive as aarrggbb; SVG wants rrggbb plus opacity.
    double opacity = 1.0;
    if (color.size() == 8) {
        opacity = color.left(2).toInt(nullptr, 16) / 255.0;
        color = color.mid(2);
    }

    const int w = requestedSize.width() > 0 ? requestedSize.width() : 24;
    const int h = requestedSize.height() > 0 ? requestedSize.height() : w;
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    const char *d = paths().value(name, nullptr);
    if (d) {
        const QByteArray svg = QByteArrayLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\"><path fill=\"#")
                               + color.toLatin1() + QByteArrayLiteral("\" fill-opacity=\"") + QByteArray::number(opacity)
                               + QByteArrayLiteral("\" d=\"") + QByteArray(d) + QByteArrayLiteral("\"/></svg>");
        QSvgRenderer renderer(svg);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        renderer.render(&p);
    }
    if (size)
        *size = img.size();
    return img;
}
