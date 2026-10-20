// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "library/TrackCache.h"
#include <QAbstractListModel>
#include <QVariantList>
#include <QVector>

// Plain file-system view: sub-folders first, then the tracks of this folder,
// both in natural order ("2 - …" before "10 - …").
class FolderModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(qint64 folderId READ folderId WRITE setFolderId NOTIFY folderChanged)
    Q_PROPERTY(QString path READ path NOTIFY folderChanged)
    Q_PROPERTY(QString name READ name NOTIFY folderChanged)
    Q_PROPERTY(QVariantList crumbs READ crumbs NOTIFY folderChanged)
    Q_PROPERTY(int folderCount READ folderCount NOTIFY folderChanged)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY folderChanged)

public:
    enum Roles {
        IsFolderRole = Qt::UserRole + 1, IdRole, NameRole, DetailRole, CoverIdRole, TitleRole, ArtistRole,
        AlbumRole, DurationRole, FormatRole, TrackNoRole
    };

    explicit FolderModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    qint64 folderId() const { return m_folderId; }
    void setFolderId(qint64 id);
    QString path() const { return m_path; }
    QString name() const { return m_name; }
    QVariantList crumbs() const { return m_crumbs; }
    int folderCount() const { return int(m_folders.size()); }
    int trackCount() const { return int(m_tracks.size()); }

    Q_INVOKABLE void reload();
    Q_INVOKABLE void up();
    QVector<qint64> trackIds() const;
    Q_INVOKABLE int trackIndexOfRow(int row) const { return row - int(m_folders.size()); }
    Q_INVOKABLE QString folderPath(qint64 folderId) const; // full path of any folder

signals:
    void folderChanged();

private:
    struct Folder
    {
        qint64 id = 0, coverId = 0;
        QString name;
        int subfolders = 0, tracks = 0;
    };
    qint64 m_folderId = 0;
    qint64 m_parentId = 0;
    QString m_path, m_name;
    QVariantList m_crumbs;
    QVector<Folder> m_folders;
    QVector<TrackRow> m_tracks;
};
