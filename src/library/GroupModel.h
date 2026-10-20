// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <array>
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
    enum Roles { Key1Role = Qt::UserRole + 1, Key2Role, TitleRole, SubtitleRole, CountRole, CoverIdRole, YearRole };

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
    QString initials() const { return m_initials; }

signals:
    void specChanged();
    void countChanged();
    void loadingChanged();

private:
    static constexpr int JumpSlots = 27; // "#", "A" … "Z"
    struct Jump
    {
        std::array<int, JumpSlots> first; // first group of each initial, -1 none
        QString initials;
    };
    static int jumpSlot(QChar c);
    void apply(int generation, QVector<Group> groups, const Jump &jump);

    QVariantMap m_spec;
    QVector<Group> m_groups;
    std::array<int, JumpSlots> m_jumpFirst{};
    QString m_initials;
    bool m_loading = false;
    bool m_specSet = false;
    int m_generation = 0;
};
