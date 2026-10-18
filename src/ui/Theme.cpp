#include "ui/Theme.h"

#include "core/Paths.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

namespace {

QVariantMap readTheme(const QString &file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "Theme" << file << "ungültig:" << err.errorString();
        return {};
    }
    QVariantMap m = doc.object().toVariantMap();
    if (m.value(QStringLiteral("id")).toString().isEmpty())
        return {};
    return m;
}

double luminance(const QColor &c)
{
    auto ch = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * ch(c.redF()) + 0.7152 * ch(c.greenF()) + 0.0722 * ch(c.blueF());
}

} // namespace

Theme::Theme(QObject *parent) : QObject(parent)
{
    QSettings s;
    m_dynamic = s.value(QStringLiteral("theme/dynamicAccent"), true).toBool();
    m_ambientEnabled = s.value(QStringLiteral("theme/ambient"), true).toBool();
    reloadThemes();
    setTheme(s.value(QStringLiteral("theme/id"), QStringLiteral("tiefsee")).toString());
    if (m_id.isEmpty())
        setTheme(QStringLiteral("tiefsee"));
}

QString Theme::userDir() const
{
    return Paths::dataDir() + QStringLiteral("/themes");
}

void Theme::reloadThemes()
{
    m_byId.clear();
    QStringList order;
    auto add = [&](const QString &file) {
        const QVariantMap t = readTheme(file);
        if (t.isEmpty())
            return;
        const QString id = t.value(QStringLiteral("id")).toString();
        if (!m_byId.contains(id))
            order << id;
        m_byId.insert(id, t);
    };
    const QDir builtIn(QStringLiteral(":/themes"));
    QStringList files = builtIn.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    // Keep the curated order from the file names ("01-tiefsee.json", ...).
    for (const QString &f : files)
        add(builtIn.filePath(f));
    QDir().mkpath(userDir());
    const QDir user(userDir());
    for (const QString &f : user.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name))
        add(user.filePath(f));

    m_list.clear();
    for (const QString &id : order) {
        const QVariantMap t = m_byId.value(id);
        const QVariantMap c = t.value(QStringLiteral("colors")).toMap();
        m_list << QVariantMap{{QStringLiteral("id"), id},
                              {QStringLiteral("name"), t.value(QStringLiteral("name"))},
                              {QStringLiteral("dark"), t.value(QStringLiteral("dark"), true)},
                              {QStringLiteral("bg"), c.value(QStringLiteral("bg"))},
                              {QStringLiteral("surface"), c.value(QStringLiteral("surface"))},
                              {QStringLiteral("text"), c.value(QStringLiteral("text"))},
                              {QStringLiteral("accent"), c.value(QStringLiteral("accent"))}};
    }
    emit themesChanged();
}

bool Theme::apply(const QVariantMap &t)
{
    const QVariantMap colors = t.value(QStringLiteral("colors")).toMap();
    static const QStringList required = {QStringLiteral("bg"), QStringLiteral("panel"), QStringLiteral("surface"),
                                         QStringLiteral("surfaceHover"), QStringLiteral("border"), QStringLiteral("text"),
                                         QStringLiteral("textDim"), QStringLiteral("textFaint"), QStringLiteral("accent")};
    QHash<QString, QColor> c;
    for (const QString &k : required) {
        const QColor col(colors.value(k).toString());
        if (!col.isValid()) {
            qWarning() << "Theme" << t.value(QStringLiteral("id")) << ": Farbe fehlt oder ungültig:" << k;
            return false;
        }
        c.insert(k, col);
    }
    const QColor onAccent(colors.value(QStringLiteral("onAccent")).toString());
    if (onAccent.isValid())
        c.insert(QStringLiteral("onAccent"), onAccent);
    m_c = c;
    m_id = t.value(QStringLiteral("id")).toString();
    m_name = t.value(QStringLiteral("name"), m_id).toString();
    m_dark = t.value(QStringLiteral("dark"), true).toBool();
    m_radius = std::clamp(t.value(QStringLiteral("radius"), 10).toInt(), 0, 32);
    m_ambient = std::clamp(t.value(QStringLiteral("ambient"), 0.5).toDouble(), 0.0, 1.0);
    const QVariantMap fonts = t.value(QStringLiteral("fonts")).toMap();
    m_uiFont = fonts.value(QStringLiteral("ui"), QStringLiteral("Manrope")).toString();
    m_displayFont = fonts.value(QStringLiteral("display"), QStringLiteral("Bricolage Grotesque")).toString();
    return true;
}

void Theme::setTheme(const QString &id)
{
    auto it = m_byId.constFind(id);
    if (it == m_byId.constEnd() || !apply(*it))
        return;
    QSettings().setValue(QStringLiteral("theme/id"), id);
    emit changed();
    emit accentChanged();
}

QColor Theme::accent() const
{
    if (!m_dynamic || !m_cover.isValid())
        return accentBase();
    // Cover colours are often too dark or too grey for UI accents: keep the hue,
    // normalise saturation and lightness so it always reads on this background.
    float h, s, l, a;
    m_cover.getHslF(&h, &s, &l, &a);
    if (h < 0)
        return accentBase();
    s = std::clamp(s, 0.45f, 0.85f);
    l = m_dark ? 0.68f : 0.42f;
    return QColor::fromHslF(h, s, l);
}

QColor Theme::onAccent() const
{
    const QColor a = accent();
    if (!m_dynamic || !m_cover.isValid()) {
        const QColor fixed = m_c.value(QStringLiteral("onAccent"));
        if (fixed.isValid())
            return fixed;
    }
    return luminance(a) > 0.35 ? QColor(QStringLiteral("#10131a")) : QColor(QStringLiteral("#ffffff"));
}

void Theme::setDynamicAccent(bool on)
{
    if (on == m_dynamic)
        return;
    m_dynamic = on;
    QSettings().setValue(QStringLiteral("theme/dynamicAccent"), on);
    emit accentChanged();
}

void Theme::setAmbientEnabled(bool on)
{
    if (on == m_ambientEnabled)
        return;
    m_ambientEnabled = on;
    QSettings().setValue(QStringLiteral("theme/ambient"), on);
    emit changed();
}

void Theme::setCoverColor(const QColor &c)
{
    if (c == m_cover)
        return;
    m_cover = c;
    emit accentChanged();
}

QColor Theme::alpha(const QColor &c, double a) const
{
    QColor r = c;
    r.setAlphaF(float(std::clamp(a, 0.0, 1.0)));
    return r;
}

QColor Theme::mix(const QColor &a, const QColor &b, double t) const
{
    t = std::clamp(t, 0.0, 1.0);
    return QColor::fromRgbF(float(a.redF() * (1 - t) + b.redF() * t), float(a.greenF() * (1 - t) + b.greenF() * t),
                            float(a.blueF() * (1 - t) + b.blueF() * t), float(a.alphaF() * (1 - t) + b.alphaF() * t));
}

QString Theme::hex(const QColor &c) const
{
    return c.name(QColor::HexArgb).mid(1);
}
