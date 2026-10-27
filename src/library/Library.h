// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QSet>
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

class FolderWatcher;
class Scanner;
class Player;
class PlayQueue;
class TrackListModel;
class FolderModel;
class GroupModel;

// The QML-facing library: folders to scan, scanner, views, and "play this" helpers.
class Library : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList roots READ roots NOTIFY rootsChanged)
    Q_PROPERTY(Scanner *scanner READ scanner CONSTANT)
    Q_PROPERTY(QVariantList views READ views NOTIFY viewsChanged)
    Q_PROPERTY(QString viewsError READ viewsError NOTIFY viewsChanged)
    Q_PROPERTY(QString viewsFile READ viewsFile CONSTANT)
    Q_PROPERTY(qint64 trackCount READ trackCount NOTIFY statsChanged)
    Q_PROPERTY(qint64 folderCount READ folderCount NOTIFY statsChanged)
    Q_PROPERTY(bool scanOnStart READ scanOnStart WRITE setScanOnStart NOTIFY settingsChanged)
    Q_PROPERTY(int scanThreads READ scanThreads WRITE setScanThreads NOTIFY settingsChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY statsChanged)
    // Folder watching: changes show up within seconds, without a scan.
    Q_PROPERTY(bool watchFolders READ watchFolders WRITE setWatchFolders NOTIFY settingsChanged)
    Q_PROPERTY(QString watchStatus READ watchStatus NOTIFY watchStatusChanged)
    Q_PROPERTY(QString watchHint READ watchHint NOTIFY watchStatusChanged) // how to allow more watched folders
    Q_PROPERTY(bool currentFavorite READ currentFavorite NOTIFY currentFavoriteChanged) // the playing track has the heart

public:
    explicit Library(Player *player, PlayQueue *queue, QObject *parent = nullptr);
    ~Library() override;
    static Library *instance();

    QStringList roots() const { return m_roots; }
    Scanner *scanner() const { return m_scanner; }
    QVariantList views() const { return m_views; }
    QString viewsError() const { return m_viewsError; }
    QString viewsFile() const;
    qint64 trackCount() const { return m_trackCount; }
    qint64 folderCount() const { return m_folderCount; }
    bool empty() const { return m_trackCount == 0; }
    bool scanOnStart() const;
    void setScanOnStart(bool on);
    int scanThreads() const;
    void setScanThreads(int n);
    bool watchFolders() const;
    void setWatchFolders(bool on);
    QString watchStatus() const;
    QString watchHint() const;
    bool currentFavorite() const { return m_currentFavorite; }

    // Favourites (the heart): rating 5 in the database, nothing written to the files.
    // Jellyfin tracks tell their server as well (see Jellyfin::setFavorites).
    Q_INVOKABLE bool isFavorite(qint64 trackId);
    Q_INVOKABLE void setFavorite(qint64 trackId, bool on);
    Q_INVOKABLE void setFavoriteRows(TrackListModel *model, int row, bool on); // the selection, if the row is part of it
    Q_INVOKABLE void toggleCurrentFavorite();

    Q_INVOKABLE void addRoot(const QVariant &pathOrUrl);
    Q_INVOKABLE void removeRoot(const QString &path);
    Q_INVOKABLE void rescan();
    Q_INVOKABLE void reloadViews();
    Q_INVOKABLE void openViewsFolder();
    Q_INVOKABLE QVariantMap viewById(const QString &id) const;
    // Small UI preferences (last view, visualizer, column choices ...).
    Q_INVOKABLE QVariant setting(const QString &key, const QVariant &fallback = {}) const;
    Q_INVOKABLE void setSetting(const QString &key, const QVariant &value);
    Q_INVOKABLE QString formatCount(qint64 n) const;
    Q_INVOKABLE QString formatDuration(qint64 ms, bool longForm = false) const;

    // Playback helpers
    Q_INVOKABLE void playList(TrackListModel *model, int row, bool shuffle = false);
    Q_INVOKABLE void enqueueList(TrackListModel *model, int row, bool next);
    Q_INVOKABLE void playSelection(TrackListModel *model, int row); // the selected rows (or `row`) only
    Q_INVOKABLE void enqueueIds(const QVariantList &ids, bool next);
    Q_INVOKABLE void playFolder(FolderModel *model, int row);
    Q_INVOKABLE void playFolderTree(const QString &path, bool shuffle = false);
    Q_INVOKABLE void playGroup(GroupModel *model, int row, bool shuffle = false);
    Q_INVOKABLE void enqueueGroup(GroupModel *model, int row, bool next);
    Q_INVOKABLE void playSpec(const QVariantMap &spec, bool shuffle = false);
    // Files and folders from the file manager (dropped, "Öffnen mit"): play them.
    // Only music of the library plays; for anything outside it outsideLibrary asks first.
    // quiet: no message when none of it is music (a playlist came along).
    Q_INVOKABLE void openPaths(const QStringList &pathsOrUrls, bool quiet = false);
    // The answer "yes": these folders join the library, then `paths` play.
    Q_INVOKABLE void addRootsAndOpen(const QStringList &folders, const QStringList &paths);

    void startupScan(bool scan = true); // scan = false: only start watching (--no-scan)
    // Something other than the scanner changed tracks (e.g. a Jellyfin sync).
    void notifyExternalChange();

signals:
    void rootsChanged();
    void viewsChanged();
    void statsChanged();
    void settingsChanged();
    void changed(); // library content changed: models reload
    void message(const QString &text);
    void watchStatusChanged();
    void currentFavoriteChanged();
    void favoritesChanged(const QVector<qint64> &ids, bool on);    // lists update their rows
    void remoteFavoritesChanged(const QVariantList &items, bool on); // [{source, item}] for Jellyfin
    void outsideLibrary(const QStringList &folders, const QStringList &paths); // openPaths: ask to add

private:
    void refreshStats();
    void scanFinished();
    void startWatching();
    void foldersChanged(const QStringList &paths);
    void scanChangedFolders();
    void periodicCheck();
    void checkMounts();
    QStringList localRoots() const;
    void playIds(QVector<qint64> ids, int start, bool shuffle);
    QVector<qint64> idsForSpec(const QVariantMap &spec);
    void applyFavorites(const QVector<qint64> &ids, bool on);
    void updateCurrentFavorite();
    void open(const QStringList &paths, bool retried, bool quiet = false);
    void opened(const QStringList &paths, QVector<qint64> ids, const QStringList &missing, bool retried);

    static Library *s_instance;
    QSettings m_settings;
    Scanner *m_scanner;
    Player *m_player;
    PlayQueue *m_queue;
    bool m_currentFavorite = false;
    QStringList m_roots;
    QVariantList m_views;
    QString m_viewsError;
    qint64 m_trackCount = 0;
    qint64 m_folderCount = 0;

    FolderWatcher *m_watcher;
    QStringList m_watchRoots;       // roots the watcher was started with
    QSet<QString> m_dirtyFolders;   // reported by the watcher, not scanned yet
    bool m_fullPending = false;     // full scan wanted while a small one ran
    QStringList m_pendingOpen;      // opened files the scan has to read first
    QElapsedTimer m_firstDirty;
    QTimer m_dirtyTimer;            // waits until a copy is done
    bool m_checkAll = false;        // events were lost: look at everything
    QTimer m_periodic;              // network shares, folders over the watch limit
    QTimer m_mounts;                // drives coming back
    QSet<QString> m_missing;        // roots not reachable right now
    QStringList m_subsetQueue;      // roots to check as soon as the scanner is free
};
