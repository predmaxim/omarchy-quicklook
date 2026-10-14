#include "Thumbnails.h"
#include <QCryptographicHash>
#include <QDir>
#include <QImageReader>

namespace ql {

QString thumbnailPath(const QString &canonicalUri, const QString &sizeDir)
{
    QString base = qEnvironmentVariable("XDG_CACHE_HOME");
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.cache");
    const QByteArray md5 = QCryptographicHash::hash(canonicalUri.toUtf8(), QCryptographicHash::Md5).toHex();
    return base + QStringLiteral("/thumbnails/") + sizeDir + QLatin1Char('/') + QString::fromLatin1(md5)
        + QStringLiteral(".png");
}

QImage loadThumbnail(const QString &canonicalUri, const QDateTime &modified)
{
    for (const char *dir : {"x-large", "large", "normal"}) {
        QImageReader reader(thumbnailPath(canonicalUri, QLatin1String(dir)), "png");
        const QImage image = reader.read();
        if (!image.isNull() && image.text(QStringLiteral("Thumb::MTime")).toLongLong() == modified.toSecsSinceEpoch())
            return image;
    }
    return {};
}

} // namespace ql
