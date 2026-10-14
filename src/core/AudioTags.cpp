#include "AudioTags.h"
#include <QFile>
#include <QStringList>
#include <taglib/fileref.h>
#include <taglib/tpropertymap.h>

namespace ql {

namespace {
QString qs(const TagLib::String &s) { return QString::fromStdString(s.to8Bit(true)); }
}

AudioTags readAudioTags(const QString &path)
{
    TagLib::FileRef f(QFile::encodeName(path).constData());
    if (f.isNull())
        return {};
    AudioTags t;
    if (const TagLib::Tag *tag = f.tag()) {
        t.title = qs(tag->title());
        t.album = qs(tag->album());
    }
    // properties(), not tag()->artist(): TagLib joins several artists as "A / B"
    QStringList artists;
    for (const TagLib::String &a : f.properties().value("ARTIST"))
        artists << qs(a);
    t.artist = artists.join(QStringLiteral(", "));
    if (const TagLib::AudioProperties *p = f.audioProperties())
        t.durationMs = p->lengthInMilliseconds();
    const auto pictures = f.complexProperties("PICTURE");
    if (!pictures.isEmpty()) {
        const TagLib::ByteVector data = pictures.front().value("data").toByteVector();
        t.cover = QByteArray(data.data(), qsizetype(data.size()));
        t.coverMime = QByteArray::fromStdString(pictures.front().value("mimeType").toString().to8Bit(true));
    }
    return t;
}

} // namespace ql
