// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDir>
#include <QStandardPaths>
#include <QString>

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

} // namespace Paths
