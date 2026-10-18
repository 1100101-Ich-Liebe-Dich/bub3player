#pragma once

#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

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
    Q_INVOKABLE void playFolder(FolderModel *model, int row);
    Q_INVOKABLE void playFolderTree(const QString &path, bool shuffle = false);
    Q_INVOKABLE void playGroup(GroupModel *model, int row, bool shuffle = false);
    Q_INVOKABLE void enqueueGroup(GroupModel *model, int row, bool next);
    Q_INVOKABLE void playSpec(const QVariantMap &spec, bool shuffle = false);

    void startupScan();
    // Something other than the scanner changed tracks (e.g. a Jellyfin sync).
    void notifyExternalChange();

signals:
    void rootsChanged();
    void viewsChanged();
    void statsChanged();
    void settingsChanged();
    void changed(); // library content changed: models reload
    void message(const QString &text);

private:
    void maybeAutoplay();
    QVector<qint64> autoplayPicks(qint64 seed, int count);
    void refreshStats();
    void playIds(QVector<qint64> ids, int start, bool shuffle);
    QVector<qint64> idsForSpec(const QVariantMap &spec);

    static Library *s_instance;
    QSettings m_settings;
    Scanner *m_scanner;
    Player *m_player;
    PlayQueue *m_queue;
    qint64 m_autoplaySeed = 0;
    QStringList m_roots;
    QVariantList m_views;
    QString m_viewsError;
    qint64 m_trackCount = 0;
    qint64 m_folderCount = 0;
};
