#pragma once

#include <QColor>
#include <QObject>
#include <QVariantList>

// Themes are JSON files: built-ins ship in the binary, user themes live in
// ~/.local/share/halcyon/themes/*.json (same format; same id overrides).
class Theme : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString id READ id NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    Q_PROPERTY(QColor bg READ bg NOTIFY changed)
    Q_PROPERTY(QColor panel READ panel NOTIFY changed)
    Q_PROPERTY(QColor surface READ surface NOTIFY changed)
    Q_PROPERTY(QColor surfaceHover READ surfaceHover NOTIFY changed)
    Q_PROPERTY(QColor border READ border NOTIFY changed)
    Q_PROPERTY(QColor text READ text NOTIFY changed)
    Q_PROPERTY(QColor textDim READ textDim NOTIFY changed)
    Q_PROPERTY(QColor textFaint READ textFaint NOTIFY changed)
    Q_PROPERTY(QColor accentBase READ accentBase NOTIFY changed)
    Q_PROPERTY(QColor accent READ accent NOTIFY accentChanged)
    Q_PROPERTY(QColor onAccent READ onAccent NOTIFY accentChanged)
    Q_PROPERTY(int radius READ radius NOTIFY changed)
    Q_PROPERTY(double ambient READ ambient NOTIFY changed)
    Q_PROPERTY(QString uiFont READ uiFont NOTIFY changed)
    Q_PROPERTY(QString displayFont READ displayFont NOTIFY changed)
    Q_PROPERTY(bool dynamicAccent READ dynamicAccent WRITE setDynamicAccent NOTIFY accentChanged)
    Q_PROPERTY(bool ambientEnabled READ ambientEnabled WRITE setAmbientEnabled NOTIFY changed)
    Q_PROPERTY(QColor coverColor READ coverColor WRITE setCoverColor NOTIFY accentChanged)
    Q_PROPERTY(QVariantList themes READ themes NOTIFY themesChanged)
    Q_PROPERTY(QString userDir READ userDir CONSTANT)

public:
    explicit Theme(QObject *parent = nullptr);

    QString id() const { return m_id; }
    QString name() const { return m_name; }
    bool dark() const { return m_dark; }
    QColor bg() const { return m_c.value(QStringLiteral("bg")); }
    QColor panel() const { return m_c.value(QStringLiteral("panel")); }
    QColor surface() const { return m_c.value(QStringLiteral("surface")); }
    QColor surfaceHover() const { return m_c.value(QStringLiteral("surfaceHover")); }
    QColor border() const { return m_c.value(QStringLiteral("border")); }
    QColor text() const { return m_c.value(QStringLiteral("text")); }
    QColor textDim() const { return m_c.value(QStringLiteral("textDim")); }
    QColor textFaint() const { return m_c.value(QStringLiteral("textFaint")); }
    QColor accentBase() const { return m_c.value(QStringLiteral("accent")); }
    QColor accent() const;
    QColor onAccent() const;
    int radius() const { return m_radius; }
    double ambient() const { return m_ambientEnabled ? m_ambient : 0.0; }
    QString uiFont() const { return m_uiFont; }
    QString displayFont() const { return m_displayFont; }
    bool dynamicAccent() const { return m_dynamic; }
    void setDynamicAccent(bool on);
    bool ambientEnabled() const { return m_ambientEnabled; }
    void setAmbientEnabled(bool on);
    QColor coverColor() const { return m_cover; }
    void setCoverColor(const QColor &c);
    QVariantList themes() const { return m_list; }
    QString userDir() const;

    Q_INVOKABLE void setTheme(const QString &id);
    Q_INVOKABLE void reloadThemes();
    Q_INVOKABLE QColor alpha(const QColor &c, double a) const;
    Q_INVOKABLE QColor mix(const QColor &a, const QColor &b, double t) const;
    Q_INVOKABLE QString hex(const QColor &c) const; // "rrggbb" for icon urls

signals:
    void changed();
    void accentChanged();
    void themesChanged();

private:
    bool apply(const QVariantMap &theme);

    QString m_id, m_name, m_uiFont, m_displayFont;
    bool m_dark = true;
    int m_radius = 10;
    double m_ambient = 0.5;
    bool m_dynamic = true;
    bool m_ambientEnabled = true;
    QHash<QString, QColor> m_c;
    QColor m_cover;
    QVariantList m_list;
    QHash<QString, QVariantMap> m_byId;
};
