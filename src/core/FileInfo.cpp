#include "FileInfo.h"
#include <QFile>
#include <QImageReader>
#include <QMimeDatabase>
#include <QStringConverter>
#include <QTimeZone>
#include <QUrl>
#include <gio/gio.h>

namespace ql {

namespace {

constexpr const char *kAttrs = "standard::target-uri,standard::display-name,standard::type,standard::size,"
                               "time::modified,time::modified-usec";

QString fromGString(const char *s) { return s ? QString::fromUtf8(s) : QString(); }

// Last path segment of a URI or path, percent-decoded for URIs.
QString lastSegment(const QString &uri)
{
    const QUrl url(uri);
    const QString s = url.scheme().isEmpty() ? uri : url.path();
    return QUrl::fromPercentEncoding(s.section(QLatin1Char('/'), -1).toUtf8());
}

bool hasSuffix(const QString &name, std::initializer_list<const char *> suffixes)
{
    for (const char *s : suffixes)
        if (name.endsWith(QLatin1String(s), Qt::CaseInsensitive))
            return true;
    return false;
}

} // namespace

FileInfo resolveFile(const QString &uri)
{
    FileInfo info;
    info.uri = uri;
    info.displayName = lastSegment(uri);
    if (uri.isEmpty())
        return info; // GIO would resolve "" to the working directory

    g_autoptr(GFile) file = g_file_new_for_commandline_arg(uri.toUtf8().constData());
    g_autoptr(GFileInfo) gi = g_file_query_info(file, kAttrs, G_FILE_QUERY_INFO_NONE, nullptr, nullptr);
    if (!gi) {
        return info; // not found / unreadable: no path either
    }
    const char *target = g_file_info_get_attribute_string(gi, G_FILE_ATTRIBUTE_STANDARD_TARGET_URI);
    if (target && *target) {
        g_autoptr(GFile) tfile = g_file_new_for_uri(target);
        g_autoptr(GFileInfo) tinfo = g_file_query_info(tfile, kAttrs, G_FILE_QUERY_INFO_NONE, nullptr, nullptr);
        if (!tinfo)
            return info;
        g_object_unref(file);
        file = static_cast<GFile *>(g_steal_pointer(&tfile));
        g_object_unref(gi);
        gi = static_cast<GFileInfo *>(g_steal_pointer(&tinfo));
    }

    const QString display = fromGString(g_file_info_get_display_name(gi));
    if (!display.isEmpty())
        info.displayName = display;
    const GFileType type = g_file_info_get_file_type(gi);
    info.exists = type == G_FILE_TYPE_REGULAR || type == G_FILE_TYPE_DIRECTORY || type == G_FILE_TYPE_SPECIAL;
    info.isDir = type == G_FILE_TYPE_DIRECTORY;
    info.size = static_cast<qint64>(g_file_info_get_size(gi));
    const qint64 sec = static_cast<qint64>(g_file_info_get_attribute_uint64(gi, G_FILE_ATTRIBUTE_TIME_MODIFIED));
    const quint32 usec = g_file_info_get_attribute_uint32(gi, G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC);
    info.modified = QDateTime::fromMSecsSinceEpoch(sec * 1000 + usec / 1000, QTimeZone::LocalTime);

    g_autofree char *p = g_file_get_path(file);
    info.path = fromGString(p);
    if (info.path.isEmpty())
        return info;
    g_autoptr(GFile) local = g_file_new_for_path(p);
    g_autofree char *canonical = g_file_get_uri(local);
    info.canonicalUri = fromGString(canonical);

    // Only regular files: opening a FIFO would block the probe thread forever.
    if (type == G_FILE_TYPE_REGULAR) {
        QFile f(info.path);
        if (f.open(QIODevice::ReadOnly))
            info.head = f.read(kHeadBytes);
    }
    info.mimeName = info.isDir ? QStringLiteral("inode/directory")
                               : QMimeDatabase().mimeTypeForFileNameAndData(info.path, info.head).name();
    return info;
}

FileInfo infoForPath(const QString &path)
{
    return resolveFile(QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded));
}

bool hasOfficeSignature(const QByteArray &head)
{
    return head.startsWith("PK\x03\x04") || head.startsWith("\xd0\xcf\x11\xe0") || head.startsWith("{\\rtf");
}

bool isOfficeType(const QString &fileName, const QMimeType &mime)
{
    if (hasSuffix(fileName, {".docx", ".xlsx", ".pptx", ".odt", ".ods", ".odp", ".doc", ".xls", ".ppt", ".rtf"}))
        return true;
    static const QStringList names{
        "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
        "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
        "application/vnd.openxmlformats-officedocument.presentationml.presentation",
        "application/vnd.oasis.opendocument.text", "application/vnd.oasis.opendocument.spreadsheet",
        "application/vnd.oasis.opendocument.presentation", "application/msword",
        "application/vnd.ms-excel", "application/vnd.ms-powerpoint", "application/rtf", "text/rtf"};
    return names.contains(mime.name());
}

bool looksLikeText(const QMimeType &mime, const QByteArray &head)
{
    if (QStringConverter::encodingForData(head).has_value())
        return true;
    if (head.left(8 * 1024).contains('\0'))
        return false;
    if (mime.inherits(QStringLiteral("text/plain")) || mime.name().startsWith(QLatin1String("text/"))
        || mime.name() == QLatin1String("application/octet-stream"))
        return true;
    return std::ranges::any_of(mime.allAncestors(), [](const QString &a) { return a.startsWith(QLatin1String("text/")); });
}

Kind classify(const FileInfo &info)
{
    if (info.isDir)
        return Kind::FolderCard;
    if (!info.exists || info.path.isEmpty() || info.size == 0)
        return Kind::FileCard;

    QMimeDatabase db;
    QMimeType mime = db.mimeTypeForName(info.mimeName);
    if (isOfficeType(info.path, mime)) {
        if (hasOfficeSignature(info.head))
            return Kind::Office;
        mime = db.mimeTypeForData(info.head); // not office after all: judge by content
    }
    const QString name = mime.name();
    if (name == QLatin1String("application/pdf"))
        return Kind::Pdf;
    if (name == QLatin1String("image/svg+xml") || name == QLatin1String("image/svg+xml-compressed"))
        return Kind::Svg;
    // The QtPdf and kimageformats plugins claim pdf/eps/ps; those are not images here.
    if (!hasSuffix(name, {"/pdf", "/eps", "/x-eps", "/postscript", "/x-epsi", "/x-epsf"})
        && !QImageReader::imageFormatsForMimeType(name.toUtf8()).isEmpty())
        return Kind::Image;
    if (name.startsWith(QLatin1String("video/")))
        return Kind::Video;
    if (name.startsWith(QLatin1String("audio/")))
        return Kind::Audio;
    return looksLikeText(mime, info.head) ? Kind::Text : Kind::FileCard;
}

} // namespace ql
