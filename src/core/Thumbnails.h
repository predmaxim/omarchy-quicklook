#pragma once
// Freedesktop thumbnail cache as placeholder (spec: "Показ", п. 5).
#include <QDateTime>
#include <QImage>
#include <QString>

namespace ql {

QString thumbnailPath(const QString &canonicalUri, const QString &sizeDir); // $XDG_CACHE_HOME/thumbnails/<dir>/<md5(uri)>.png
QImage loadThumbnail(const QString &canonicalUri, const QDateTime &modified);
        // x-large -> large -> normal; QImage::text("Thumb::MTime") must equal modified (seconds); null if none

} // namespace ql
