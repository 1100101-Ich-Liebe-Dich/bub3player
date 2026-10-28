// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDir>
#include <QStandardPaths>
#include <QString>
#include <QUrl>
#include <QVariant>

// XDG locations: ~/.local/share/halcyon, ~/.config/halcyon, ~/.cache/halcyon
namespace Paths {

inline QString ensure(const QString &dir)
{
    QDir().mkpath(dir);
    return dir;
}
inline QString dataDir() { return ensure(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/halcyon")); }
inline QString configDir() { return ensure(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/halcyon")); }
inline QString cacheDir() { return ensure(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/halcyon")); }

// `path` is `dir` or lies below it ("/musik2" is not below "/musik").
inline bool isUnder(const QString &path, const QString &dir)
{
    return path == dir || path.startsWith(dir.endsWith(QLatin1Char('/')) ? dir : dir + QLatin1Char('/'));
}

// A path from QML: a file URL (Qt's dialogs) or a plain path (the desktop's dialogs).
inline QString localPath(const QVariant &pathOrUrl)
{
    if (pathOrUrl.typeId() == QMetaType::QUrl)
        return pathOrUrl.toUrl().toLocalFile();
    const QString s = pathOrUrl.toString();
    return s.startsWith(QLatin1String("file://")) ? QUrl(s).toLocalFile() : s;
}

} // namespace Paths
