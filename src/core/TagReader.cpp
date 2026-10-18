#include "core/TagReader.h"

#include <QFile>
#include <QFileInfo>
#include <cstring>

#include <taglib/apefile.h>
#include <taglib/apeproperties.h>
#include <taglib/apetag.h>
#include <taglib/aifffile.h>
#include <taglib/attachedpictureframe.h>
#include <taglib/fileref.h>
#include <taglib/flacfile.h>
#include <taglib/flacpicture.h>
#include <taglib/flacproperties.h>
#include <taglib/id3v2tag.h>
#include <taglib/mp4file.h>
#include <taglib/mp4properties.h>
#include <taglib/mp4tag.h>
#include <taglib/mpegfile.h>
#include <taglib/opusfile.h>
#include <taglib/tpropertymap.h>
#include <taglib/vorbisfile.h>
#include <taglib/wavfile.h>
#include <taglib/wavpackfile.h>
#include <taglib/wavpackproperties.h>
#include <taglib/xiphcomment.h>

namespace {

QString ts(const TagLib::String &s)
{
    return QString::fromStdString(s.to8Bit(true)).trimmed();
}

QString first(const TagLib::PropertyMap &map, const char *key)
{
    auto it = map.find(key);
    if (it == map.end() || it->second.isEmpty())
        return {};
    // Several values (e.g. multiple artists) are joined for display.
    QStringList parts;
    for (const auto &v : it->second) {
        const QString s = ts(v);
        if (!s.isEmpty())
            parts << s;
    }
    return parts.join(QStringLiteral("; "));
}

int leadingInt(const QString &s)
{
    int n = 0, i = 0;
    while (i < s.size() && s[i].isSpace())
        ++i;
    bool any = false;
    while (i < s.size() && s[i].isDigit()) {
        n = n * 10 + s[i].digitValue();
        any = true;
        if (n > 100000)
            break;
        ++i;
    }
    return any ? n : 0;
}

QString extLower(const QString &path)
{
    const int dot = path.lastIndexOf(QLatin1Char('.'));
    return dot < 0 ? QString() : path.mid(dot + 1).toLower();
}

QByteArray fromId3(TagLib::ID3v2::Tag *tag)
{
    if (!tag)
        return {};
    const auto &frames = tag->frameListMap()["APIC"];
    QByteArray fallback;
    for (auto *f : frames) {
        auto *pic = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame *>(f);
        if (!pic)
            continue;
        const auto data = pic->picture();
        QByteArray bytes(data.data(), int(data.size()));
        if (pic->type() == TagLib::ID3v2::AttachedPictureFrame::FrontCover)
            return bytes;
        if (fallback.isEmpty())
            fallback = bytes;
    }
    return fallback;
}

QByteArray fromFlacPictures(const TagLib::List<TagLib::FLAC::Picture *> &list)
{
    QByteArray fallback;
    for (auto *p : list) {
        const auto data = p->data();
        QByteArray bytes(data.data(), int(data.size()));
        if (p->type() == TagLib::FLAC::Picture::FrontCover)
            return bytes;
        if (fallback.isEmpty())
            fallback = bytes;
    }
    return fallback;
}

QByteArray fromApe(TagLib::APE::Tag *tag)
{
    if (!tag)
        return {};
    const auto &items = tag->itemListMap();
    auto it = items.find("COVER ART (FRONT)");
    if (it == items.end())
        return {};
    const TagLib::ByteVector v = it->second.binaryData();
    // Layout: "<filename>\0<image bytes>"
    const int nul = v.find('\0');
    if (nul < 0)
        return {};
    return QByteArray(v.data() + nul + 1, int(v.size()) - nul - 1);
}

} // namespace

namespace TagReader {

const QStringList &audioExtensions()
{
    static const QStringList exts = {
        QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("ogg"), QStringLiteral("oga"),
        QStringLiteral("opus"), QStringLiteral("m4a"), QStringLiteral("m4b"), QStringLiteral("mp4"),
        QStringLiteral("aac"), QStringLiteral("alac"), QStringLiteral("wav"), QStringLiteral("aif"),
        QStringLiteral("aiff"), QStringLiteral("ape"), QStringLiteral("wv"), QStringLiteral("mpc"),
        QStringLiteral("wma"), QStringLiteral("dsf"), QStringLiteral("dff"),
    };
    return exts;
}

bool isAudioFile(const char *name, size_t len)
{
    // Hot path during the directory walk: no allocations.
    const char *dot = static_cast<const char *>(memrchr(name, '.', len));
    if (!dot)
        return false;
    const size_t elen = len - size_t(dot - name) - 1;
    if (elen < 2 || elen > 4)
        return false;
    char e[5] = {0};
    for (size_t i = 0; i < elen; ++i) {
        char c = dot[1 + i];
        e[i] = (c >= 'A' && c <= 'Z') ? char(c + 32) : c;
    }
    static const char *list[] = {"mp3", "flac", "ogg", "oga", "opus", "m4a", "m4b", "mp4", "aac",
                                 "alac", "wav", "aif", "aiff", "ape", "wv", "mpc", "wma", "dsf", "dff"};
    for (const char *x : list)
        if (std::strcmp(x, e) == 0)
            return true;
    return false;
}

TrackTags read(const QString &path)
{
    TrackTags t;
    const QByteArray fname = QFile::encodeName(path);
    TagLib::FileRef ref(fname.constData(), true, TagLib::AudioProperties::Fast);
    if (ref.isNull() || !ref.file()) {
        t.title = QFileInfo(path).completeBaseName();
        t.codec = extLower(path).toUpper();
        return t;
    }

    const TagLib::PropertyMap props = ref.file()->properties();
    t.title = first(props, "TITLE");
    t.artist = first(props, "ARTIST");
    t.album = first(props, "ALBUM");
    t.albumArtist = first(props, "ALBUMARTIST");
    t.genre = first(props, "GENRE");
    t.composer = first(props, "COMPOSER");
    t.year = leadingInt(first(props, "DATE"));
    if (t.year == 0)
        t.year = leadingInt(first(props, "ORIGINALDATE"));
    if (t.year < 1000 || t.year > 3000)
        t.year = 0;
    t.trackNo = leadingInt(first(props, "TRACKNUMBER"));
    t.discNo = leadingInt(first(props, "DISCNUMBER"));
    if (t.title.isEmpty())
        t.title = QFileInfo(path).completeBaseName();

    const QString ext = extLower(path);
    t.codec = ext.toUpper();
    if (auto *ap = ref.audioProperties()) {
        t.durationMs = ap->lengthInMilliseconds();
        t.bitrate = ap->bitrate();
        t.sampleRate = ap->sampleRate();
        t.channels = ap->channels();
        auto *file = ref.file();
        if (auto *f = dynamic_cast<TagLib::FLAC::File *>(file)) {
            t.bits = f->audioProperties() ? f->audioProperties()->bitsPerSample() : 0;
        } else if (auto *f = dynamic_cast<TagLib::MP4::File *>(file)) {
            if (auto *p = f->audioProperties()) {
                const bool alac = p->codec() == TagLib::MP4::Properties::ALAC;
                t.codec = alac ? QStringLiteral("ALAC") : QStringLiteral("AAC");
                t.bits = alac ? p->bitsPerSample() : 0; // lossy AAC has no real bit depth
            }
        } else if (auto *f = dynamic_cast<TagLib::RIFF::WAV::File *>(file)) {
            t.bits = f->audioProperties() ? f->audioProperties()->bitsPerSample() : 0;
        } else if (auto *f = dynamic_cast<TagLib::RIFF::AIFF::File *>(file)) {
            t.bits = f->audioProperties() ? f->audioProperties()->bitsPerSample() : 0;
            t.codec = QStringLiteral("AIFF");
        } else if (auto *f = dynamic_cast<TagLib::APE::File *>(file)) {
            t.bits = f->audioProperties() ? f->audioProperties()->bitsPerSample() : 0;
        } else if (auto *f = dynamic_cast<TagLib::WavPack::File *>(file)) {
            t.bits = f->audioProperties() ? f->audioProperties()->bitsPerSample() : 0;
        } else if (dynamic_cast<TagLib::Ogg::Opus::File *>(file)) {
            t.codec = QStringLiteral("OPUS");
        } else if (dynamic_cast<TagLib::Ogg::Vorbis::File *>(file)) {
            t.codec = QStringLiteral("OGG");
        }
    }
    if (t.codec == QLatin1String("OGA"))
        t.codec = QStringLiteral("OGG");
    if (t.codec == QLatin1String("AIF"))
        t.codec = QStringLiteral("AIFF");
    t.ok = true;
    return t;
}

QByteArray embeddedCover(const QString &path)
{
    const QByteArray fname = QFile::encodeName(path);
    const QString ext = extLower(path);
    try {
        if (ext == QLatin1String("mp3")) {
            TagLib::MPEG::File f(fname.constData(), false);
            return fromId3(f.ID3v2Tag());
        }
        if (ext == QLatin1String("flac")) {
            TagLib::FLAC::File f(fname.constData(), false);
            QByteArray b = fromFlacPictures(f.pictureList());
            if (b.isEmpty() && f.xiphComment())
                b = fromFlacPictures(f.xiphComment()->pictureList());
            if (b.isEmpty())
                b = fromId3(f.ID3v2Tag());
            return b;
        }
        if (ext == QLatin1String("m4a") || ext == QLatin1String("m4b") || ext == QLatin1String("mp4")
            || ext == QLatin1String("alac") || ext == QLatin1String("aac")) {
            TagLib::MP4::File f(fname.constData(), false);
            if (!f.isValid() || !f.tag())
                return {};
            const TagLib::MP4::Item item = f.tag()->item("covr");
            const auto covers = item.toCoverArtList();
            if (covers.isEmpty())
                return {};
            const auto data = covers.front().data();
            return QByteArray(data.data(), int(data.size()));
        }
        if (ext == QLatin1String("opus")) {
            TagLib::Ogg::Opus::File f(fname.constData(), false);
            return f.tag() ? fromFlacPictures(f.tag()->pictureList()) : QByteArray();
        }
        if (ext == QLatin1String("ogg") || ext == QLatin1String("oga")) {
            TagLib::Ogg::Vorbis::File f(fname.constData(), false);
            return f.tag() ? fromFlacPictures(f.tag()->pictureList()) : QByteArray();
        }
        if (ext == QLatin1String("wav")) {
            TagLib::RIFF::WAV::File f(fname.constData(), false);
            return fromId3(f.ID3v2Tag());
        }
        if (ext == QLatin1String("aif") || ext == QLatin1String("aiff")) {
            TagLib::RIFF::AIFF::File f(fname.constData(), false);
            return fromId3(f.tag());
        }
        if (ext == QLatin1String("ape")) {
            TagLib::APE::File f(fname.constData(), false);
            return fromApe(f.APETag());
        }
        if (ext == QLatin1String("wv")) {
            TagLib::WavPack::File f(fname.constData(), false);
            return fromApe(f.APETag());
        }
    } catch (...) {
    }
    return {};
}

} // namespace TagReader
