#include "library/GroupModel.h"

#include "analysis/AudioAnalyzer.h"
#include "core/Database.h"
#include "library/Library.h"
#include "library/QueryBuilder.h"

#include <QCoreApplication>
#include <QDate>
#include <QLocale>
#include <QPointer>
#include <QtConcurrent>

namespace {

QString countText(int n)
{
    return n == 1 ? QStringLiteral("1 Titel") : QStringLiteral("%1 Titel").arg(QLocale(QLocale::German).toString(n));
}

QString monthTitle(const QString &ym)
{
    const QDate d = QDate::fromString(ym + QStringLiteral("-01"), QStringLiteral("yyyy-MM-dd"));
    if (!d.isValid())
        return QStringLiteral("Unbekannt");
    return QLocale(QLocale::German).toString(d, QStringLiteral("MMMM yyyy"));
}

} // namespace

GroupModel::GroupModel(QObject *parent) : QAbstractListModel(parent)
{
    if (Library::instance())
        connect(Library::instance(), &Library::changed, this, &GroupModel::reload);
}

int GroupModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_groups.size());
}

QHash<int, QByteArray> GroupModel::roleNames() const
{
    return {{Key1Role, "key1"}, {Key2Role, "key2"}, {TitleRole, "title"}, {SubtitleRole, "subtitle"},
            {CountRole, "count"}, {CoverIdRole, "coverId"}, {YearRole, "year"}, {InitialRole, "initial"}};
}

QVariant GroupModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_groups.size())
        return {};
    const Group &g = m_groups[index.row()];
    switch (role) {
    case Key1Role: return g.key1;
    case Key2Role: return g.key2;
    case TitleRole: return g.title;
    case SubtitleRole: return g.subtitle;
    case CountRole: return g.count;
    case CoverIdRole: return g.coverId;
    case YearRole: return g.year;
    case InitialRole: return g.title.isEmpty() ? QString() : g.title.left(1).toUpper();
    }
    return {};
}

void GroupModel::setSpec(const QVariantMap &spec)
{
    if (m_specSet && spec == m_spec)
        return;
    m_spec = spec;
    m_specSet = true;
    emit specChanged();
    reload();
}

void GroupModel::reload()
{
    if (!m_specSet)
        return;
    const int gen = ++m_generation;
    if (!m_loading) {
        m_loading = true;
        emit loadingChanged();
    }
    const QueryBuilder::Built q = QueryBuilder::groups(m_spec);
    const QString by = m_spec.value(QStringLiteral("groupBy"), QStringLiteral("album")).toString();
    QPointer<GroupModel> self(this);
    (void)QtConcurrent::run([self, gen, q, by] {
        QVector<Group> groups;
        if (q.ok) {
            try {
                DbConnection db = Database::open(8 * 1024);
                Stmt s(db, q.sql);
                for (int i = 0; i < q.binds.size(); ++i)
                    s.bind(i + 1, q.binds[i]);
                while (s.step()) {
                    Group g;
                    g.key1 = s.var(0);
                    g.key2 = s.var(1);
                    g.count = s.i32(2);
                    g.coverId = s.i64(3);
                    g.year = s.i32(4);
                    const QString k = s.str(0);
                    if (by == QLatin1String("album")) {
                        g.title = s.str(1).isEmpty() ? QStringLiteral("Unbekanntes Album") : s.str(1);
                        g.subtitle = k.isEmpty() ? QStringLiteral("Unbekannter Künstler") : k;
                    } else if (by == QLatin1String("addedMonth")) {
                        g.title = monthTitle(k);
                        g.subtitle = countText(g.count);
                    } else if (by == QLatin1String("decade")) {
                        const int d = s.i32(0);
                        g.title = d > 0 ? QStringLiteral("%1er").arg(d) : QStringLiteral("Ohne Jahr");
                        g.subtitle = countText(g.count);
                    } else if (by == QLatin1String("tempo")) {
                        const int t = s.i32(0);
                        g.title = QStringLiteral("%1–%2 BPM").arg(t).arg(t + 9);
                        g.subtitle = countText(g.count);
                    } else if (by == QLatin1String("key")) {
                        const int code = s.i32(0);
                        g.title = AudioAnalyzer::camelot(code / 2, code % 2) + QStringLiteral(" · ")
                                  + AudioAnalyzer::keyName(code / 2, code % 2);
                        g.subtitle = countText(g.count);
                    } else if (by == QLatin1String("year") || by == QLatin1String("fileYear")) {
                        const int y = s.i32(0);
                        g.title = y > 0 ? QString::number(y) : QStringLiteral("Ohne Jahr");
                        g.subtitle = countText(g.count);
                    } else {
                        g.title = k.isEmpty() ? QStringLiteral("Unbekannt") : k;
                        g.subtitle = countText(g.count);
                    }
                    groups.push_back(std::move(g));
                }
            } catch (const std::exception &e) {
                qWarning() << "Gruppenabfrage fehlgeschlagen:" << e.what();
            }
        }
        if (by == QLatin1String("key")) // around the Camelot wheel: 1A 1B 2A 2B ...
            std::stable_sort(groups.begin(), groups.end(), [](const Group &a, const Group &b) {
                const int ka = a.key1.toInt(), kb = b.key1.toInt();
                const QString ca = AudioAnalyzer::camelot(ka / 2, ka % 2), cb = AudioAnalyzer::camelot(kb / 2, kb % 2);
                const int na = ca.chopped(1).toInt(), nb = cb.chopped(1).toInt();
                return na != nb ? na < nb : ca.back() < cb.back();
            });
        QMetaObject::invokeMethod(qApp, [self, gen, groups = std::move(groups)]() mutable {
            if (self)
                self->apply(gen, std::move(groups));
        }, Qt::QueuedConnection);
    });
}

void GroupModel::apply(int generation, QVector<Group> groups)
{
    if (generation != m_generation)
        return;
    const bool sameShape = groups.size() == m_groups.size();
    if (sameShape && !groups.isEmpty()) {
        m_groups = std::move(groups);
        emit dataChanged(index(0), index(int(m_groups.size()) - 1));
    } else {
        beginResetModel();
        m_groups = std::move(groups);
        endResetModel();
    }
    m_loading = false;
    emit countChanged();
    emit loadingChanged();
}

QVariantMap GroupModel::trackSpec(int row) const
{
    if (row < 0 || row >= m_groups.size())
        return {};
    const Group &g = m_groups[row];
    const QString by = m_spec.value(QStringLiteral("groupBy"), QStringLiteral("album")).toString();
    QVariantList filters = m_spec.value(QStringLiteral("filters")).toList();
    filters += QueryBuilder::groupFilters(by, g.key1, g.key2);
    QVariantMap spec;
    spec.insert(QStringLiteral("filters"), filters);
    spec.insert(QStringLiteral("sort"), QStringLiteral("album"));
    return spec;
}

QVariantMap GroupModel::get(int row) const
{
    QVariantMap m;
    if (row < 0 || row >= m_groups.size())
        return m;
    const Group &g = m_groups[row];
    m.insert(QStringLiteral("title"), g.title);
    m.insert(QStringLiteral("subtitle"), g.subtitle);
    m.insert(QStringLiteral("count"), g.count);
    m.insert(QStringLiteral("coverId"), g.coverId);
    m.insert(QStringLiteral("year"), g.year);
    return m;
}

int GroupModel::indexForLetter(const QString &letter) const
{
    const QString l = letter.toUpper();
    // Albums sorted by name are ordered by artist, so jump by the artist's initial.
    const bool byArtist = m_spec.value(QStringLiteral("groupBy"), QStringLiteral("album")).toString() == QLatin1String("album")
                          && m_spec.value(QStringLiteral("groupSort"), QStringLiteral("name")).toString() == QLatin1String("name");
    for (int i = 0; i < m_groups.size(); ++i) {
        const QString key = byArtist ? m_groups[i].key1.toString() : m_groups[i].title;
        const QString first = key.left(1).toUpper();
        if (l == QLatin1String("#") ? !first.isEmpty() && !first[0].isLetter() : first >= l)
            return i;
    }
    return int(m_groups.size()) - 1;
}
