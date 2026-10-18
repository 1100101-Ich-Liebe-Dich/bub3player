#include "library/FolderModel.h"

#include "core/Database.h"
#include "library/Library.h"
#include <QCollator>
#include <algorithm>

FolderModel::FolderModel(QObject *parent) : QAbstractListModel(parent)
{
    if (Library::instance())
        connect(Library::instance(), &Library::changed, this, &FolderModel::reload);
    reload();
}

int FolderModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_folders.size() + m_tracks.size());
}

QHash<int, QByteArray> FolderModel::roleNames() const
{
    return {{IsFolderRole, "isFolder"}, {IdRole, "itemId"}, {NameRole, "name"}, {DetailRole, "detail"},
            {CoverIdRole, "coverId"}, {TitleRole, "title"}, {ArtistRole, "artist"}, {AlbumRole, "album"},
            {DurationRole, "duration"}, {FormatRole, "format"}, {TrackNoRole, "trackNo"}};
}

QVariant FolderModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};
    const int row = index.row();
    if (row < m_folders.size()) {
        const Folder &f = m_folders[row];
        switch (role) {
        case IsFolderRole: return true;
        case IdRole: return f.id;
        case NameRole:
        case TitleRole: return f.name;
        case CoverIdRole: return f.coverId;
        case DetailRole: {
            QStringList parts;
            if (f.subfolders > 0)
                parts << (f.subfolders == 1 ? QStringLiteral("1 Ordner") : QStringLiteral("%1 Ordner").arg(f.subfolders));
            if (f.tracks > 0)
                parts << (f.tracks == 1 ? QStringLiteral("1 Titel") : QStringLiteral("%1 Titel").arg(f.tracks));
            return parts.join(QStringLiteral(", "));
        }
        default: return {};
        }
    }
    const int t = row - int(m_folders.size());
    if (t >= m_tracks.size())
        return {};
    const TrackRow &r = m_tracks[t];
    switch (role) {
    case IsFolderRole: return false;
    case IdRole: return r.id;
    case CoverIdRole: return r.id;
    case NameRole: return r.filename;
    case TitleRole: return r.title;
    case ArtistRole: return r.artist;
    case AlbumRole: return r.album;
    case DurationRole: return r.durationText();
    case FormatRole: return r.formatLabel();
    case TrackNoRole: return r.trackNo > 0 ? QString::number(r.trackNo) : QString();
    case DetailRole: return r.artist;
    }
    return {};
}

void FolderModel::setFolderId(qint64 id)
{
    if (id == m_folderId)
        return;
    m_folderId = id;
    reload();
}

void FolderModel::up()
{
    setFolderId(m_parentId);
}

QVector<qint64> FolderModel::trackIds() const
{
    QVector<qint64> ids;
    ids.reserve(m_tracks.size());
    for (const TrackRow &r : m_tracks)
        ids << r.id;
    return ids;
}

void FolderModel::reload()
{
    beginResetModel();
    m_folders.clear();
    m_tracks.clear();
    m_crumbs.clear();
    m_path.clear();
    m_name = QStringLiteral("Ordner");
    m_parentId = 0;

    try {
        DbConnection &db = TrackCache::instance().db();
        if (m_folderId > 0) {
            Stmt s(db, "SELECT path, name, parent_id FROM folders WHERE id = ?");
            s.bind(1, m_folderId);
            if (s.step()) {
                m_path = s.str(0);
                m_name = s.str(1);
                m_parentId = s.i64(2);
            } else {
                m_folderId = 0;
            }
            // Breadcrumb trail up to the library root.
            QVariantList trail;
            qint64 cur = m_folderId;
            Stmt up(db, "SELECT id, name, parent_id FROM folders WHERE id = ?");
            for (int guard = 0; cur > 0 && guard < 64; ++guard) {
                up.bind(1, cur);
                if (!up.step())
                    break;
                trail.prepend(QVariantMap{{QStringLiteral("id"), up.i64(0)}, {QStringLiteral("name"), up.str(1)}});
                cur = up.i64(2);
                up.reset();
            }
            m_crumbs = trail;
        }

        // At the top level, show the library roots: local ones with their full
        // path, Jellyfin libraries with their name ("Server · Library").
        Stmt fs(db, "SELECT f.id, f.name, f.path, "
                    "(SELECT COUNT(*) FROM folders c WHERE c.parent_id = f.id), "
                    "(SELECT COUNT(*) FROM tracks t WHERE t.folder_id = f.id), "
                    "(SELECT MIN(t.id) FROM tracks t WHERE t.folder_id = f.id), "
                    "f.source_id "
                    "FROM folders f WHERE f.parent_id = ?");
        fs.bind(1, m_folderId);
        while (fs.step()) {
            Folder f;
            f.id = fs.i64(0);
            f.name = m_folderId == 0 && fs.i64(6) == Database::LocalSource ? fs.str(2) : fs.str(1);
            f.subfolders = fs.i32(3);
            f.tracks = fs.i32(4);
            f.coverId = fs.i64(5);
            m_folders << f;
        }

        if (m_folderId > 0) {
            QVector<qint64> ids;
            Stmt ts(db, "SELECT id FROM tracks WHERE folder_id = ?");
            ts.bind(1, m_folderId);
            while (ts.step())
                ids << ts.i64(0);
            m_tracks = TrackCache::instance().fetch(ids);
        }
    } catch (const std::exception &e) {
        qWarning() << "Ordner konnte nicht geladen werden:" << e.what();
    }

    QCollator coll;
    coll.setNumericMode(true);
    coll.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(m_folders.begin(), m_folders.end(), [&](const Folder &a, const Folder &b) { return coll.compare(a.name, b.name) < 0; });
    std::sort(m_tracks.begin(), m_tracks.end(), [&](const TrackRow &a, const TrackRow &b) { return coll.compare(a.filename, b.filename) < 0; });
    endResetModel();
    emit folderChanged();
}
