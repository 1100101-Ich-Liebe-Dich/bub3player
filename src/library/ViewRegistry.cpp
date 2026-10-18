#include "library/ViewRegistry.h"

#include "core/Paths.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace {

QVariantMap view(const char *id, const char *name, const char *icon, const char *section, const char *kind,
                 const QVariantMap &spec, const char *layout = "")
{
    return {{QStringLiteral("id"), QString::fromUtf8(id)},
            {QStringLiteral("name"), QString::fromUtf8(name)},
            {QStringLiteral("icon"), QString::fromUtf8(icon)},
            {QStringLiteral("section"), QString::fromUtf8(section)},
            {QStringLiteral("kind"), QString::fromUtf8(kind)},
            {QStringLiteral("spec"), spec},
            {QStringLiteral("layout"), QString::fromUtf8(layout)}};
}

QVariantMap filter(const char *field, const char *op, const QVariant &value = {})
{
    return {{QStringLiteral("field"), QString::fromUtf8(field)}, {QStringLiteral("op"), QString::fromUtf8(op)},
            {QStringLiteral("value"), value}};
}

const char *ExampleViews = R"JSON([
  {
    "id": "my-hires-jazz",
    "name": "Hi-Res Jazz",
    "icon": "sparkle",
    "section": "Eigene Ansichten",
    "kind": "tracks",
    "spec": {
      "filters": [
        { "field": "genre", "op": "contains", "value": "Jazz" },
        { "field": "bits",  "op": "ge",       "value": 24 }
      ],
      "sort": "year",
      "desc": true
    }
  },
  {
    "id": "my-90s-albums",
    "name": "Alben der 90er",
    "icon": "album",
    "section": "Eigene Ansichten",
    "kind": "groups",
    "layout": "grid",
    "spec": {
      "groupBy": "album",
      "groupSort": "year",
      "filters": [ { "field": "decade", "op": "eq", "value": 1990 } ]
    }
  }
]
)JSON";

} // namespace

namespace ViewRegistry {

QString userFile()
{
    return Paths::configDir() + QStringLiteral("/views.json");
}

void writeExampleIfMissing()
{
    const QString dir = Paths::configDir();
    QFile f(dir + QStringLiteral("/views.example.json"));
    if (!f.exists() && f.open(QIODevice::WriteOnly))
        f.write(ExampleViews);
}

QVariantList builtIn()
{
    const char *lib = "Bibliothek";
    const char *disc = "Entdecken";
    const char *hist = "Hörverlauf";
    return {
        view("tracks", "Alle Titel", "note", lib, "tracks", {{QStringLiteral("sort"), QStringLiteral("album")}}),
        view("albums", "Alben", "album", lib, "groups", {{QStringLiteral("groupBy"), QStringLiteral("album")}}, "grid"),
        view("artists", "Künstler", "artist", lib, "groups", {{QStringLiteral("groupBy"), QStringLiteral("albumArtist")}}, "grid"),
        view("genres", "Genres", "tag", lib, "groups", {{QStringLiteral("groupBy"), QStringLiteral("genre")}}, "tiles"),
        view("folders", "Ordner", "folder", lib, "folders", {}),

        view("foryou", "Für dich", "heart", disc, "foryou", {}),
        view("recent", "Zuletzt hinzugefügt", "new", disc, "tracks",
             {{QStringLiteral("sort"), QStringLiteral("added")}, {QStringLiteral("desc"), true}}),
        view("added-month", "Nach Monat hinzugefügt", "calendar", disc, "groups",
             {{QStringLiteral("groupBy"), QStringLiteral("addedMonth")}}, "tiles"),
        view("years", "Erscheinungsjahr", "clock", disc, "groups", {{QStringLiteral("groupBy"), QStringLiteral("year")}}, "tiles"),
        view("decades", "Jahrzehnte", "layers", disc, "groups", {{QStringLiteral("groupBy"), QStringLiteral("decade")}}, "tiles"),
        view("quality", "Formate und Qualität", "eq", disc, "groups", {{QStringLiteral("groupBy"), QStringLiteral("quality")}}, "tiles"),
        view("file-age", "Dateien nach Alter", "file", disc, "tracks", {{QStringLiteral("sort"), QStringLiteral("mtime")}}),
        view("hires", "Hi-Res (24 bit)", "sparkle", disc, "tracks",
             {{QStringLiteral("filters"), QVariantList{filter("bits", "ge", 24)}}, {QStringLiteral("sort"), QStringLiteral("album")}}),
        view("tempo", "Nach Tempo", "speed", disc, "groups", {{QStringLiteral("groupBy"), QStringLiteral("tempo")}}, "tiles"),
        view("keys", "Nach Tonart", "piano", disc, "groups", {{QStringLiteral("groupBy"), QStringLiteral("key")}}, "tiles"),

        view("history", "Zuletzt gespielt", "history", hist, "tracks",
             {{QStringLiteral("filters"), QVariantList{filter("lastPlayed", "gt", 0)}},
              {QStringLiteral("sort"), QStringLiteral("lastPlayed")}, {QStringLiteral("desc"), true}}),
        view("most-played", "Meistgespielt", "trending", hist, "tracks",
             {{QStringLiteral("filters"), QVariantList{filter("playCount", "gt", 0)}},
              {QStringLiteral("sort"), QStringLiteral("playCount")}, {QStringLiteral("desc"), true}}),
        view("forgotten", "Lange nicht gehört", "hourglass", hist, "tracks",
             {{QStringLiteral("filters"), QVariantList{filter("playCount", "ge", 3), filter("lastPlayed", "olderThanDays", 180)}},
              {QStringLiteral("sort"), QStringLiteral("lastPlayed")}}),
        view("never-played", "Nie gespielt", "music-off", hist, "tracks",
             {{QStringLiteral("filters"), QVariantList{filter("playCount", "eq", 0)}},
              {QStringLiteral("sort"), QStringLiteral("added")}, {QStringLiteral("desc"), true}}),
    };
}

QVariantList userViews(QString *error)
{
    QFile f(userFile());
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return {};
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isArray()) {
        if (error)
            *error = QStringLiteral("views.json: %1").arg(pe.errorString());
        return {};
    }
    QVariantList out;
    for (const QJsonValue &v : doc.array()) {
        QVariantMap m = v.toObject().toVariantMap();
        if (m.value(QStringLiteral("id")).toString().isEmpty() || m.value(QStringLiteral("name")).toString().isEmpty())
            continue;
        if (!m.contains(QStringLiteral("section")))
            m.insert(QStringLiteral("section"), QStringLiteral("Eigene Ansichten"));
        if (!m.contains(QStringLiteral("icon")))
            m.insert(QStringLiteral("icon"), QStringLiteral("list"));
        if (!m.contains(QStringLiteral("kind")))
            m.insert(QStringLiteral("kind"), QStringLiteral("tracks"));
        m.insert(QStringLiteral("user"), true);
        out << m;
    }
    return out;
}

QVariantList all()
{
    return builtIn() + userViews();
}

} // namespace ViewRegistry
