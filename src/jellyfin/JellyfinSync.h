// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Sql.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

// Writes Jellyfin items into the library database. Runs in a worker thread,
// one page at a time, each page in one transaction. Tracks are keyed by
// (source_id, remote_id), so a sync can be repeated or resumed at any time.
class JellyfinWriter
{
public:
    JellyfinWriter(qint64 sourceId, const QString &serverName);

    struct Page
    {
        int items = 0;     // items in this page
        int total = 0;     // TotalRecordCount reported by the server
        int changed = 0;   // inserted or actually changed rows
        QString error;
    };

    // Items page (BaseItemDto list). `prefix`: server directory that is the
    // library root in the folder view. `track`: remember ids for removal later.
    Page writePage(const QByteArray &json, const QString &libId, const QString &libName, const QString &prefix,
                   bool trackSeen);
    // Ids-only page: only remembers which items still exist.
    Page seenPage(const QByteArray &json);
    void resetSeen();
    // Removes tracks of a library that were not seen since resetSeen().
    int removeUnseen(const QString &libId);
    int removeLibrary(const QString &libId);
    int localCount(const QString &libId);
    int markDuplicates();        // rows whose duplicate state changed
    void pruneFolders();

    static QString commonPrefix(const QByteArray &json); // longest common directory of the items' paths
    static int removeSource(qint64 sourceId);
    static int markAllDuplicates();                      // after a local scan

private:
    qint64 folderFor(const QString &libId, const QString &libName, const QString &relDir);
    void ensureOpen();

    qint64 m_source;
    QString m_serverName;
    DbConnection m_db;
    QHash<QString, qint64> m_folders; // path -> id (this source)
    bool m_seenReady = false;
};
