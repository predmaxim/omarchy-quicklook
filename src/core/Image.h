#pragma once
// Image probe and decode (spec: "Картинки").
#include <QByteArray>
#include <QImage>
#include <QSize>
#include <QSizeF>
#include <QString>

namespace ql::image {

inline constexpr qint64 kMaxPixelsNonScalable = 120'000'000;

struct Header {
    bool ok = false;
    QByteArray format;          // QImageReader::format(), called first; eps/epsf/epsi/ps/pdf -> ok = false
    QSize displayPx;            // display orientation: EXIF applied; RAW and HEIC as reported (already rotated)
    bool rotate90 = false;      // EXIF transformation swaps sides (never for RAW/HEIC)
    bool animated = false;      // supportsAnimation() && imageCount() > 1
    bool raw = false;           // kimageformats "raw"
    bool nativeScaled = false;  // jpeg, svg, svgz support ScaledSize natively
};

bool blockedFormat(const QByteArray &format);   // undetected or eps/epsf/epsi/ps/pdf: never read (Ghostscript)
Header probe(const QString &path);
QSize heifSize(const QString &path);     // libheif primary image size (already rotated); invalid on failure
int rawFlip(const QString &path);        // LibRaw imgdata.sizes.flip (0, 3, 5, 6); 0 on failure
bool tooLarge(const Header &header);     // !nativeScaled && width*height > kMaxPixelsNonScalable
QSizeF svgSize(const QString &path);     // QSvgRenderer::defaultSize, viewBox size if empty; empty on failure
QImage decode(const QString &path, const Header &header, QSize targetPx);
        // QImageReader(path) without a format string; format() first (guard); setAutoTransform(true);
        // setScaledSize(target in stored orientation) unless RAW; RAW: setQuality(0), rotate by rawFlip, then
        // scale with Qt::KeepAspectRatio; result fits targetPx, converted to RGB32 or ARGB32_Premultiplied; null on error
QImage decodeIfCurrent(quint64 generation, const QString &path, const Header &header, QSize targetPx);
        // returns a null image at once when !pools::isCurrent(generation) (checked before opening the file)
QImage renderSvg(const QString &path, QSize targetPx);   // QSvgRenderer into ARGB32_Premultiplied of targetPx
QImage decodeCover(const QByteArray &bytes, const QByteArray &mime, QSize targetPx);
        // QImageReader(&buffer, "jpeg"|"png") with setAutoDetectImageFormat(false); null otherwise

} // namespace ql::image
