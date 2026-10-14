#pragma once
// Audio card data via TagLib (spec: "Аудио").
#include <QByteArray>
#include <QString>

namespace ql {

struct AudioTags {
    QString title;            // "" -> UI uses the file name
    QString artist;           // several artists joined with ", "
    QString album;
    qint64 durationMs = 0;
    QByteArray cover;         // first picture (FileRef::complexProperties("PICTURE")), raw bytes
    QByteArray coverMime;     // "image/jpeg" | "image/png" | ...
};

AudioTags readAudioTags(const QString &path);

} // namespace ql
