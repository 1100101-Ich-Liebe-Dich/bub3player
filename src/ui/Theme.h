#pragma once

#include <QColor>
#include <QHash>
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

// Skins and colours.
//
// A skin is a complete look: its own QML shell (layout, effects, player, now
// playing), fonts, colour palettes and a settings schema. Skins are described
// by JSON files in :/skins. Every skin shows the same library content; only
// presentation differs.
//
// Each skin stores its own options ("skin/<id>/<key>"), so switching skins
// never loses the settings of another one.
class Theme : public QObject
{
    Q_OBJECT
    // ---- skin ----
    Q_PROPERTY(QString skin READ skin NOTIFY skinChanged)
    Q_PROPERTY(QString skinName READ skinName NOTIFY skinChanged)
    Q_PROPERTY(QUrl skinQml READ skinQml NOTIFY skinChanged)
    Q_PROPERTY(QVariantList skins READ skins CONSTANT)
    Q_PROPERTY(QVariantList schema READ schema NOTIFY skinChanged)
    Q_PROPERTY(QVariantMap opt READ opt NOTIFY optionsChanged)
    Q_PROPERTY(QVariantMap style READ style NOTIFY skinChanged)
    Q_PROPERTY(QVariantList palettes READ palettes NOTIFY palettesChanged)
    Q_PROPERTY(QVariantMap paletteData READ paletteData NOTIFY changed) // raw palette incl. skin extras
    // ---- colours of the active palette ----
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
    Q_PROPERTY(QColor accent2 READ accent2 NOTIFY changed)
    Q_PROPERTY(QColor accent READ accent NOTIFY accentChanged)
    Q_PROPERTY(QColor onAccent READ onAccent NOTIFY accentChanged)
    Q_PROPERTY(int radius READ radius NOTIFY changed)
    Q_PROPERTY(double ambient READ ambient NOTIFY changed)
    Q_PROPERTY(QString uiFont READ uiFont NOTIFY changed)
    Q_PROPERTY(QString displayFont READ displayFont NOTIFY changed)
    Q_PROPERTY(QString monoFont READ monoFont NOTIFY changed)
    Q_PROPERTY(bool dynamicAccent READ dynamicAccent NOTIFY accentChanged)
    Q_PROPERTY(QColor coverColor READ coverColor WRITE setCoverColor NOTIFY accentChanged)
    // A skin may tint the accent itself (e.g. Aurora with a preset light colour). Alpha 0 = none.
    Q_PROPERTY(QColor accentOverride READ accentOverride WRITE setAccentOverride NOTIFY accentChanged)
    Q_PROPERTY(QVariantList coverPalette READ coverPalette WRITE setCoverPalette NOTIFY coverPaletteChanged)
    Q_PROPERTY(QString userDir READ userDir CONSTANT)
    // Back-compat for the palette list used by the classic skin's settings.
    Q_PROPERTY(QVariantList themes READ palettes NOTIFY palettesChanged)

public:
    explicit Theme(QObject *parent = nullptr);

    QString skin() const { return m_skin; }
    QString skinName() const { return m_skinDef.value(QStringLiteral("name")).toString(); }
    QUrl skinQml() const;
    QVariantList skins() const { return m_skinList; }
    QVariantList schema() const;
    QVariantMap opt() const { return m_opt; }
    QVariantMap style() const { return m_skinDef.value(QStringLiteral("style")).toMap(); }
    QVariantList palettes() const { return m_paletteList; }
    QVariantMap paletteData() const { return m_paletteById.value(m_id); }

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
    QColor accent2() const;
    QColor accent() const;
    QColor onAccent() const;
    int radius() const { return m_radius; }
    double ambient() const;
    QString uiFont() const { return m_uiFont; }
    QString displayFont() const { return m_displayFont; }
    QString monoFont() const { return m_monoFont; }
    bool dynamicAccent() const;
    QColor coverColor() const { return m_cover; }
    QColor accentOverride() const { return m_accentOverride; }
    void setAccentOverride(const QColor &c);
    void setCoverColor(const QColor &c);
    QVariantList coverPalette() const { return m_coverPalette; }
    void setCoverPalette(const QVariantList &p);
    QString userDir() const;

    Q_INVOKABLE void setSkin(const QString &id);
    Q_INVOKABLE void setOption(const QString &key, const QVariant &value);
    Q_INVOKABLE void resetOptions();
    Q_INVOKABLE void setTheme(const QString &paletteId) { setOption(QStringLiteral("palette"), paletteId); }
    Q_INVOKABLE void reloadThemes();
    Q_INVOKABLE QColor alpha(const QColor &c, double a) const;
    Q_INVOKABLE QColor mix(const QColor &a, const QColor &b, double t) const;
    Q_INVOKABLE QString hex(const QColor &c) const; // "aarrggbb" for icon urls
    Q_INVOKABLE QColor coverTone(int index, const QColor &fallback) const;

signals:
    void skinChanged();
    void changed();
    void accentChanged();
    void optionsChanged();
    void palettesChanged();
    void coverPaletteChanged();

private:
    void loadSkins();
    void loadPalettes();
    void loadOptions();
    bool applyPalette(const QString &paletteId);
    QVariantMap defaultOptions() const;

    QVariantList m_skinList;
    QHash<QString, QVariantMap> m_skins;
    QVariantMap m_skinDef;
    QString m_skin;
    QVariantMap m_opt;

    QVariantList m_paletteList;
    QHash<QString, QVariantMap> m_paletteById;

    QString m_id, m_name, m_uiFont, m_displayFont, m_monoFont;
    bool m_dark = true;
    int m_radius = 10;
    double m_paletteAmbient = 0.5;
    QHash<QString, QColor> m_c;
    QColor m_cover;
    QColor m_accentOverride;
    QVariantList m_coverPalette;
};
