// SPDX-License-Identifier: GPL-3.0-or-later
#include "radio/StreamList.h"

#include <QMap>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QStringList>
#include <QXmlStreamReader>

namespace StreamList {

namespace {

QString decode(const QByteArray &data)
{
    QStringDecoder utf8(QStringDecoder::Utf8);
    QString s = utf8.decode(data);
    if (utf8.hasError())
        s = QString::fromLatin1(data);
    if (s.startsWith(QChar(0xfeff)))
        s.remove(0, 1);
    return s;
}

bool isStream(const QUrl &u)
{
    return u.isValid() && (u.scheme() == QLatin1String("http") || u.scheme() == QLatin1String("https"));
}

void addEntry(QVector<Entry> &out, const QUrl &base, const QString &address, const QString &title)
{
    const QString a = address.trimmed();
    if (a.isEmpty())
        return;
    const QUrl u = base.isValid() ? base.resolved(QUrl(a)) : QUrl(a);
    if (!isStream(u))
        return;
    for (const Entry &e : std::as_const(out))
        if (e.url == u)
            return;
    out.append({u, title.trimmed()});
}

QVector<Entry> parseXml(const QString &text, const QUrl &base)
{
    // XSPF (<track><location>, <title>) and ASX (<entry><ref href=""/>, <title>)
    QVector<Entry> out;
    QXmlStreamReader xml(text);
    QString title, location;
    while (!xml.atEnd()) {
        xml.readNext();
        const QString name = xml.name().toString().toLower();
        if (xml.isStartElement()) {
            if (name == QLatin1String("track") || name == QLatin1String("entry")) {
                title.clear();
                location.clear();
            } else if (name == QLatin1String("title")) {
                title = xml.readElementText();
            } else if (name == QLatin1String("location")) {
                location = xml.readElementText();
            } else if (name == QLatin1String("ref")) {
                for (const auto &a : xml.attributes())
                    if (a.name().toString().toLower() == QLatin1String("href"))
                        location = a.value().toString();
            }
        } else if (xml.isEndElement() && (name == QLatin1String("track") || name == QLatin1String("entry"))) {
            addEntry(out, base, location, title);
        }
    }
    return out;
}

} // namespace

bool isHls(const QByteArray &data)
{
    return data.contains("#EXT-X-");
}

QVector<Entry> parse(const QByteArray &data, const QUrl &base)
{
    const QString text = decode(data);
    const QString start = text.left(200).trimmed().toLower();
    if (start.startsWith(QLatin1Char('<')))
        return parseXml(text, base);

    QVector<Entry> out;
    static const QRegularExpression lineBreak(QStringLiteral("[\r\n]+"));
    const QStringList lines = text.split(lineBreak, Qt::SkipEmptyParts);
    if (start.startsWith(QLatin1String("[playlist]"))) {
        // PLS: FileN=…, TitleN=…
        QMap<int, QString> files, titles;
        static const QRegularExpression key(QStringLiteral("^(file|title)(\\d+)\\s*=\\s*(.*)$"),
                                            QRegularExpression::CaseInsensitiveOption);
        for (const QString &l : lines) {
            const auto m = key.match(l.trimmed());
            if (!m.hasMatch())
                continue;
            (m.captured(1).toLower() == QLatin1String("file") ? files : titles).insert(m.captured(2).toInt(), m.captured(3));
        }
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            addEntry(out, base, it.value(), titles.value(it.key()));
        return out;
    }
    // M3U: addresses, "#EXTINF:-1,Title" before them
    QString title;
    for (const QString &raw : lines) {
        const QString l = raw.trimmed();
        if (l.startsWith(QLatin1String("#EXTINF:"), Qt::CaseInsensitive)) {
            const int comma = int(l.indexOf(QLatin1Char(',')));
            title = comma > 0 ? l.mid(comma + 1) : QString();
        } else if (!l.startsWith(QLatin1Char('#'))) {
            addEntry(out, base, l, title);
            title.clear();
        }
    }
    return out;
}

bool looksLikeList(const QUrl &url, const QByteArray &contentType, const QByteArray &head)
{
    const QString path = url.path().toLower();
    for (const char *ext : {".pls", ".m3u", ".m3u8", ".xspf", ".asx"})
        if (path.endsWith(QLatin1String(ext)))
            return true;
    const QByteArray ct = contentType.toLower();
    for (const char *t : {"audio/x-scpls", "audio/scpls", "mpegurl", "application/xspf", "video/x-ms-asf", "audio/x-ms-asx"})
        if (ct.contains(t))
            return true;
    if (ct.startsWith("audio/") || ct.startsWith("application/ogg"))
        return false; // the stream itself
    const QByteArray start = head.left(64).trimmed().toLower();
    return start.startsWith("[playlist]") || start.startsWith("#extm3u") || start.startsWith("<?xml")
           || start.startsWith("<asx") || start.startsWith("<playlist");
}

} // namespace StreamList
