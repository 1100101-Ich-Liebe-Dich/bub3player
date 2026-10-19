#pragma once

#include <QAbstractListModel>
#include <QVariantMap>
#include <QVector>

// Albums, artists, genres, years, formats, "added by month"... any GROUP BY over
// the library. Loaded off the UI thread; ~100 bytes per group.
class GroupModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap spec READ spec WRITE setSpec NOTIFY specChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString initials READ initials NOTIFY countChanged) // letters that have groups ("#ABC…")

public:
    enum Roles { Key1Role = Qt::UserRole + 1, Key2Role, TitleRole, SubtitleRole, CountRole, CoverIdRole, YearRole, InitialRole };

    struct Group
    {
        QVariant key1, key2;
        QString title, subtitle;
        int count = 0;
        qint64 coverId = 0;
        int year = 0;
    };

    explicit GroupModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QVariantMap spec() const { return m_spec; }
    void setSpec(const QVariantMap &spec);
    int count() const { return int(m_groups.size()); }
    bool loading() const { return m_loading; }

    Q_INVOKABLE void reload();
    // Spec for the tracks of one group (inherits this model's filters).
    Q_INVOKABLE QVariantMap trackSpec(int row) const;
    Q_INVOKABLE QVariantMap get(int row) const;
    // Index of the first group whose title starts with the letter (for the A–Z jump bar).
    Q_INVOKABLE int indexForLetter(const QString &letter) const;
    QString initials() const;

signals:
    void specChanged();
    void countChanged();
    void loadingChanged();

private:
    void apply(int generation, QVector<Group> groups);
    QString jumpKey(const Group &g) const;

    QVariantMap m_spec;
    QVector<Group> m_groups;
    bool m_loading = false;
    bool m_specSet = false;
    int m_generation = 0;
};
