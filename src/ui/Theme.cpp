// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/Theme.h"

#include "core/Paths.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <algorithm>
#include <cmath>

namespace {

QVariantMap readJson(const QString &file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "Theme-Datei" << file << "ungültig:" << err.errorString();
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

QString skinKey(const QString &skin, const QString &key)
{
    return QStringLiteral("skin/%1/%2").arg(skin, key);
}

} // namespace

Theme::Theme(QObject *parent) : QObject(parent)
{
    loadSkins();
    QSettings s;
    QString skin = s.value(QStringLiteral("theme/skin")).toString();
    // "Studio" was rebuilt as "Vintage".
    if (skin == QLatin1String("studio"))
        skin = QStringLiteral("vintage");
    if (!m_skins.contains(skin))
        skin = QStringLiteral("klassik");
    // Before skins existed, "theme/id" was the classic colour scheme.
    if (!s.contains(skinKey(QStringLiteral("klassik"), QStringLiteral("palette"))) && s.contains(QStringLiteral("theme/id")))
        s.setValue(skinKey(QStringLiteral("klassik"), QStringLiteral("palette")), s.value(QStringLiteral("theme/id")));
    setSkin(skin);
}

QString Theme::userDir() const
{
    return Paths::dataDir() + QStringLiteral("/themes");
}

void Theme::loadSkins()
{
    const QDir dir(QStringLiteral(":/skins"));
    QList<QVariantMap> list;
    for (const QString &f : dir.entryList({QStringLiteral("*.json")}, QDir::Files)) {
        QVariantMap m = readJson(dir.filePath(f));
        if (m.isEmpty())
            continue;
        const QString id = m.value(QStringLiteral("id")).toString();
        m_skins.insert(id, m);
        list << m;
    }
    std::sort(list.begin(), list.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value(QStringLiteral("order")).toInt() < b.value(QStringLiteral("order")).toInt();
    });
    for (const QVariantMap &m : list) {
        const QString id = m.value(QStringLiteral("id")).toString();
        const QString preview = QStringLiteral(":/skins/%1.jpg").arg(id);
        m_skinList << QVariantMap{{QStringLiteral("id"), id},
                                  {QStringLiteral("name"), m.value(QStringLiteral("name"))},
                                  {QStringLiteral("tagline"), m.value(QStringLiteral("tagline"))},
                                  {QStringLiteral("preview"), QFile::exists(preview) ? QStringLiteral("qrc") + preview : QString()},
                                  {QStringLiteral("swatch"), m.value(QStringLiteral("swatch")).toList()},
                                  {QStringLiteral("font"), m.value(QStringLiteral("fonts")).toMap().value(QStringLiteral("display"))}};
    }
}

void Theme::loadPalettes()
{
    m_paletteList.clear();
    m_paletteById.clear();
    QStringList order;
    auto add = [&](const QVariantMap &t) {
        if (t.isEmpty())
            return;
        const QString id = t.value(QStringLiteral("id")).toString();
        if (!m_paletteById.contains(id))
            order << id;
        m_paletteById.insert(id, t);
    };
    const QVariant src = m_skinDef.value(QStringLiteral("palettes"));
    if (src.typeId() == QMetaType::QString && src.toString() == QLatin1String("files")) {
        // Classic skin: the colour schemes in :/themes plus the user's own files.
        const QDir builtIn(QStringLiteral(":/themes"));
        for (const QString &f : builtIn.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name))
            add(readJson(builtIn.filePath(f)));
        QDir().mkpath(userDir());
        const QDir user(userDir());
        for (const QString &f : user.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name))
            add(readJson(user.filePath(f)));
    } else {
        for (const QVariant &v : src.toList())
            add(v.toMap());
    }
    for (const QString &id : order) {
        const QVariantMap t = m_paletteById.value(id);
        const QVariantMap c = t.value(QStringLiteral("colors")).toMap();
        m_paletteList << QVariantMap{{QStringLiteral("id"), id},
                                     {QStringLiteral("name"), t.value(QStringLiteral("name"))},
                                     {QStringLiteral("dark"), t.value(QStringLiteral("dark"), true)},
                                     {QStringLiteral("bg"), c.value(QStringLiteral("bg"))},
                                     {QStringLiteral("surface"), c.value(QStringLiteral("surface"))},
                                     {QStringLiteral("text"), c.value(QStringLiteral("text"))},
                                     {QStringLiteral("accent"), c.value(QStringLiteral("accent"))},
                                     {QStringLiteral("accent2"), c.value(QStringLiteral("accent2"), c.value(QStringLiteral("accent")))}};
    }
    emit palettesChanged();
}

QVariantMap Theme::defaultOptions() const
{
    QVariantMap d;
    for (const QVariant &v : m_skinDef.value(QStringLiteral("settings")).toList()) {
        const QVariantMap s = v.toMap();
        d.insert(s.value(QStringLiteral("key")).toString(), s.value(QStringLiteral("default")));
    }
    return d;
}

void Theme::loadOptions()
{
    QSettings s;
    m_opt = defaultOptions();
    for (auto it = m_opt.begin(); it != m_opt.end(); ++it) {
        const QString k = skinKey(m_skin, it.key());
        if (!s.contains(k))
            continue;
        QVariant v = s.value(k);
        // QSettings returns strings for everything stored in INI files.
        switch (it.value().typeId()) {
        case QMetaType::Bool: v = v.toString() == QLatin1String("true") || v.toBool(); break;
        case QMetaType::Double: case QMetaType::Int: case QMetaType::LongLong: v = v.toDouble(); break;
        default: break;
        }
        it.value() = v;
    }
}

QVariantList Theme::schema() const
{
    // Resolve "palette" settings into a choice list with colour previews.
    QVariantList out;
    for (const QVariant &v : m_skinDef.value(QStringLiteral("settings")).toList()) {
        QVariantMap s = v.toMap();
        if (s.value(QStringLiteral("type")).toString() == QLatin1String("palette"))
            s.insert(QStringLiteral("options"), m_paletteList);
        out << s;
    }
    return out;
}

void Theme::setSkin(const QString &id)
{
    auto it = m_skins.constFind(id);
    if (it == m_skins.constEnd())
        return;
    m_skin = id;
    m_skinDef = *it;
    m_accentOverride = QColor();
    QSettings().setValue(QStringLiteral("theme/skin"), id);
    loadPalettes();
    loadOptions();
    QString pal = m_opt.value(QStringLiteral("palette")).toString();
    if (!m_paletteById.contains(pal) && !m_paletteList.isEmpty())
        pal = m_paletteList.first().toMap().value(QStringLiteral("id")).toString();
    applyPalette(pal);
    emit skinChanged();
    emit optionsChanged();
    emit changed();
    emit accentChanged();
}

bool Theme::applyPalette(const QString &paletteId)
{
    auto it = m_paletteById.constFind(paletteId);
    if (it == m_paletteById.constEnd())
        return false;
    const QVariantMap &t = *it;
    const QVariantMap colors = t.value(QStringLiteral("colors")).toMap();
    static const QStringList required = {QStringLiteral("bg"), QStringLiteral("panel"), QStringLiteral("surface"),
                                         QStringLiteral("surfaceHover"), QStringLiteral("border"), QStringLiteral("text"),
                                         QStringLiteral("textDim"), QStringLiteral("textFaint"), QStringLiteral("accent")};
    QHash<QString, QColor> c;
    for (const QString &k : required) {
        const QColor col(colors.value(k).toString());
        if (!col.isValid()) {
            qWarning() << "Farbschema" << paletteId << ": Farbe fehlt oder ungültig:" << k;
            return false;
        }
        c.insert(k, col);
    }
    for (const char *opt : {"onAccent", "accent2"}) {
        const QColor col(colors.value(QLatin1String(opt)).toString());
        if (col.isValid())
            c.insert(QLatin1String(opt), col);
    }
    m_c = c;
    m_id = paletteId;
    m_name = t.value(QStringLiteral("name"), paletteId).toString();
    m_dark = t.value(QStringLiteral("dark"), true).toBool();
    // Radius and fonts: palette may override the skin.
    m_radius = std::clamp(t.value(QStringLiteral("radius"), m_skinDef.value(QStringLiteral("radius"), 10)).toInt(), 0, 40);
    m_paletteAmbient = std::clamp(t.value(QStringLiteral("ambient"), 0.5).toDouble(), 0.0, 1.0);
    const QVariantMap skinFonts = m_skinDef.value(QStringLiteral("fonts")).toMap();
    const QVariantMap fonts = t.contains(QStringLiteral("fonts")) && m_skin == QLatin1String("klassik")
                                  ? t.value(QStringLiteral("fonts")).toMap() : skinFonts;
    m_uiFont = fonts.value(QStringLiteral("ui"), skinFonts.value(QStringLiteral("ui"), QStringLiteral("Manrope"))).toString();
    m_displayFont = fonts.value(QStringLiteral("display"), skinFonts.value(QStringLiteral("display"), QStringLiteral("Bricolage Grotesque"))).toString();
    m_monoFont = skinFonts.value(QStringLiteral("mono"), m_uiFont).toString();
    return true;
}

void Theme::setOption(const QString &key, const QVariant &value)
{
    if (!m_opt.contains(key) || m_opt.value(key) == value)
        return;
    m_opt.insert(key, value);
    QSettings().setValue(skinKey(m_skin, key), value);
    if (key == QLatin1String("palette") && applyPalette(value.toString())) {
        emit changed();
        emit accentChanged();
    }
    if (key == QLatin1String("dynamicAccent"))
        emit accentChanged();
    if (key == QLatin1String("ambient"))
        emit changed();
    emit optionsChanged();
}

void Theme::resetOptions()
{
    QSettings s;
    s.remove(QStringLiteral("skin/") + m_skin);
    setSkin(m_skin);
}

void Theme::reloadThemes()
{
    loadPalettes();
    applyPalette(m_id);
    emit changed();
    emit accentChanged();
}

QUrl Theme::skinQml() const
{
    return QUrl(m_skinDef.value(QStringLiteral("qml")).toString());
}

double Theme::ambient() const
{
    if (m_opt.contains(QStringLiteral("ambient")))
        return m_opt.value(QStringLiteral("ambient")).toBool() ? m_paletteAmbient : 0.0;
    return m_skinDef.value(QStringLiteral("ambient"), 0.0).toDouble();
}

bool Theme::dynamicAccent() const
{
    if (m_opt.contains(QStringLiteral("dynamicAccent")))
        return m_opt.value(QStringLiteral("dynamicAccent")).toBool();
    return m_skinDef.value(QStringLiteral("dynamicAccent"), false).toBool();
}

QColor Theme::accent2() const
{
    const QColor c = m_c.value(QStringLiteral("accent2"));
    return c.isValid() ? c : accentBase();
}

QColor Theme::accent() const
{
    if (m_accentOverride.isValid() && m_accentOverride.alpha() > 0) {
        float h, s, l, a;
        m_accentOverride.getHslF(&h, &s, &l, &a);
        return QColor::fromHslF(std::max(0.0f, h), std::clamp(s, 0.45f, 0.9f), m_dark ? 0.68f : 0.42f);
    }
    if (!dynamicAccent() || !m_cover.isValid())
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
    const bool overridden = m_accentOverride.isValid() && m_accentOverride.alpha() > 0;
    if (!overridden && (!dynamicAccent() || !m_cover.isValid())) {
        const QColor fixed = m_c.value(QStringLiteral("onAccent"));
        if (fixed.isValid())
            return fixed;
    }
    return luminance(a) > 0.35 ? QColor(QStringLiteral("#10131a")) : QColor(QStringLiteral("#ffffff"));
}

void Theme::setCoverColor(const QColor &c)
{
    if (c == m_cover)
        return;
    m_cover = c;
    emit accentChanged();
}

void Theme::setAccentOverride(const QColor &c)
{
    if (c == m_accentOverride)
        return;
    m_accentOverride = c;
    emit accentChanged();
}

void Theme::setCoverPalette(const QVariantList &p)
{
    if (p == m_coverPalette)
        return;
    m_coverPalette = p;
    emit coverPaletteChanged();
}

QColor Theme::coverTone(int index, const QColor &fallback) const
{
    if (index >= 0 && index < m_coverPalette.size()) {
        const QColor c = m_coverPalette.at(index).value<QColor>();
        if (c.isValid())
            return c;
    }
    return fallback;
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
