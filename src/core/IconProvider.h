#pragma once

#include <QQuickImageProvider>

// image://icon/<name>/<rrggbb>  - crisp vector icons in any theme colour,
// independent of the desktop icon theme. Paths: Material Icons (Apache 2.0).
class IconProvider : public QQuickImageProvider
{
public:
    IconProvider();
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
