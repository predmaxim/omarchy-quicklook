#pragma once
// Address resolution and viewer choice (spec: "Тип файла и выбор просмотрщика").
#include "Types.h"
#include <QByteArray>
#include <QDateTime>
#include <QMimeType>
#include <QString>

namespace ql {

inline constexpr qint64 kHeadBytes = 64 * 1024;

struct FileInfo {
    QString uri;            // as received from Files
    QString path;           // local path ("" if none)
    QString canonicalUri;   // g_file_get_uri() of the local path: key for the thumbnail cache
    QString displayName;    // standard::display-name, else file name
    bool exists = false;
    bool isDir = false;
    qint64 size = -1;
    QDateTime modified;
    QString mimeName;       // QMimeDatabase::mimeTypeForFileNameAndData(path, head).name()
    QByteArray head;        // first kHeadBytes bytes of a regular file
};

FileInfo resolveFile(const QString &uri);   // blocking (GIO + read): probe pool only
FileInfo infoForPath(const QString &path);  // same for a local path
Kind classify(const FileInfo &info);
        // dir -> FolderCard; !exists or no path -> FileCard; size 0 -> FileCard;
        // application/pdf -> Pdf; isOfficeType && hasOfficeSignature -> Office;
        // svg mime -> Svg; QImageReader::imageFormatsForMimeType non-empty (not pdf/eps/ps) -> Image;
        // video/* -> Video; audio/* -> Audio; looksLikeText -> Text; else FileCard
bool looksLikeText(const QMimeType &mime, const QByteArray &head);
        // BOM, or no NUL in first 8 KiB and (inherits text/plain, or mime/ancestor starts with "text/",
        // or application/octet-stream)
bool hasOfficeSignature(const QByteArray &head);  // "PK\x03\x04", D0 CF 11 E0, "{\rtf"
bool isOfficeType(const QString &fileName, const QMimeType &mime); // docx xlsx pptx odt ods odp doc xls ppt rtf

} // namespace ql
