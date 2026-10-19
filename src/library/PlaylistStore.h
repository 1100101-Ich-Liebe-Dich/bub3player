#pragma once

#include <QObject>
#include <QThreadPool>
#include <functional>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class DbConnection;
class GroupModel;
class PlayQueue;
class TrackListModel;

// Playlists and smart playlists (QML singleton "Playlists").
//
// A playlist is an ordered list of entries in `playlist_items`; every entry
// carries a hint (path, title, artist, album, length). When a track leaves the
// library (file moved, drive away, server removed) the entry stays as
// "missing" and is linked again after the next scan or sync.
// A smart playlist is a view spec (rules, sort, limit) stored as JSON and
// evaluated live by QueryBuilder.
// Each playlist shows up as a view in the section "Playlists", in every theme.
class PlaylistStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList playlists READ playlists NOTIFY playlistsChanged) // [{id, name, smart, count, missing}]
    Q_PROPERTY(QVariantList normalPlaylists READ normalPlaylists NOTIFY playlistsChanged)

public:
    explicit PlaylistStore(PlayQueue *queue, QObject *parent = nullptr);
    ~PlaylistStore() override;
    static PlaylistStore *instance();

    QVariantList playlists() const { return m_list; }
    QVariantList normalPlaylists() const;
    // Sidebar entries for Library::views (plus "Neue Playlist").
    QVariantList views() const;

    Q_INVOKABLE QVariantMap get(qint64 id) const;
    Q_INVOKABLE QString uniqueName(const QString &base) const;
    // `rules` empty: a normal playlist, else a smart playlist with these rules.
    Q_INVOKABLE qint64 create(const QString &name, const QVariantMap &rules = {});
    Q_INVOKABLE bool rename(qint64 id, const QString &name);
    Q_INVOKABLE bool setRules(qint64 id, const QVariantMap &rules);
    Q_INVOKABLE void remove(qint64 id);
    // Smart playlist -> normal playlist with the tracks it shows right now.
    Q_INVOKABLE qint64 freeze(qint64 id, TrackListModel *model);

    // Adding (append). Rows follow the list's selection (see TrackListModel::actionRows).
    Q_INVOKABLE void addRows(qint64 id, TrackListModel *model, int row);
    Q_INVOKABLE void addAll(qint64 id, TrackListModel *model);
    Q_INVOKABLE void addGroup(qint64 id, GroupModel *model, int row);
    Q_INVOKABLE void addFolder(qint64 id, const QString &path);
    Q_INVOKABLE void addQueue(qint64 id);
    Q_INVOKABLE void addTrack(qint64 id, qint64 trackId);

    // Editing a playlist shown in `model` (spec.playlist = id).
    Q_INVOKABLE void removeRows(qint64 id, TrackListModel *model, int row);
    // Moves the rows (selection or `row`) in front of row `to` (count = to the end).
    Q_INVOKABLE void moveRows(qint64 id, TrackListModel *model, int row, int to);
    Q_INVOKABLE int removeMissing(qint64 id);

    Q_INVOKABLE qint64 importFile(const QUrl &file);
    Q_INVOKABLE bool exportFile(qint64 id, const QUrl &file);
    Q_INVOKABLE QString suggestedExportName(qint64 id) const;
    Q_INVOKABLE QUrl defaultFolder() const; // where file dialogs start: ~/Musik

    // Smart playlist editor: counts the tracks of `rules` off the UI thread,
    // answers with previewReady(serial, ...).
    Q_INVOKABLE int preview(const QVariantMap &rules);

    // Links missing entries to tracks that showed up again (after scans / syncs).
    void relink();

signals:
    void playlistsChanged();
    void contentChanged(qint64 id);
    void previewReady(int serial, qint64 count, qint64 durationMs, const QString &error);
    void message(const QString &text);

private:
    void reload();
    void append(qint64 id, QVector<qint64> trackIds, bool announce = true); // in the background
    void touch(qint64 id);
    // Runs a change in the writer thread, then reloads and announces it.
    void writeAsync(qint64 id, std::function<void(DbConnection &)> work, std::function<void()> after = {});
    QString hintFor(DbConnection &db, qint64 trackId);

    static PlaylistStore *s_instance;
    PlayQueue *m_queue;
    QVariantList m_list;
    int m_previewSerial = 0;
    int m_pendingWrites = 0;
    QThreadPool m_writer;
};
