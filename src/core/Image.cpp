#include "Image.h"
#include "Pools.h"
#include <QBuffer>
#include <QImageReader>
#include <QPainter>
#include <QSvgRenderer>
#include <QTransform>
#include <libheif/heif.h>
#include <libraw/libraw.h>

namespace ql::image {

namespace {

bool isBlocked(const QByteArray &fmt)
{
    // PostScript family goes through Ghostscript in kimageformats: never.
    return fmt.isEmpty() || fmt == "eps" || fmt == "epsf" || fmt == "epsi" || fmt == "ps" || fmt == "pdf";
}

QSize fitDown(QSize s, QSize box)
{
    if (s.width() <= box.width() && s.height() <= box.height())
        return s;
    return s.scaled(box, Qt::KeepAspectRatio);
}

QImage finish(QImage img, QSize targetPx)
{
    if (img.isNull())
        return img;
    if (img.width() > targetPx.width() || img.height() > targetPx.height())
        img = img.scaled(targetPx, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return img.convertToFormat(img.hasAlphaChannel() ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32);
}

} // namespace

Header probe(const QString &path)
{
    Header h;
    QImageReader r(path);
    const QByteArray fmt = r.format();
    if (isBlocked(fmt))
        return h;
    h.format = fmt;
    h.nativeScaled = fmt == "jpeg" || fmt == "jpg" || fmt == "svg" || fmt == "svgz";
    QSize size;
    if (fmt == "heif" || fmt == "heic") {
        size = heifSize(path);
    } else if (fmt == "raw") {
        h.raw = true;
        size = r.size();
    } else {
        r.setAutoTransform(true);
        const QSize s = r.size();
        h.rotate90 = r.transformation() & QImageIOHandler::TransformationRotate90;
        size = h.rotate90 ? s.transposed() : s;
        h.animated = r.supportsAnimation() && r.imageCount() > 1;
    }
    if (!size.isValid())
        return h;
    h.displayPx = size;
    h.ok = true;
    return h;
}

QSize heifSize(const QString &path)
{
    QSize out;
    heif_context *ctx = heif_context_alloc();
    if (!ctx)
        return out;
    const QByteArray p = path.toLocal8Bit();
    heif_image_handle *handle = nullptr;
    if (heif_context_read_from_file(ctx, p.constData(), nullptr).code == heif_error_Ok
        && heif_context_get_primary_image_handle(ctx, &handle).code == heif_error_Ok) {
        out = QSize(heif_image_handle_get_width(handle), heif_image_handle_get_height(handle));
        heif_image_handle_release(handle);
    }
    heif_context_free(ctx);
    return out;
}

int rawFlip(const QString &path)
{
    LibRaw lr;
    if (lr.open_file(path.toLocal8Bit().constData()) != LIBRAW_SUCCESS)
        return 0;
    return lr.imgdata.sizes.flip;
}

bool tooLarge(const Header &header)
{
    return !header.nativeScaled && !header.raw
        && qint64(header.displayPx.width()) * header.displayPx.height() > kMaxPixelsNonScalable;
}

QSizeF svgSize(const QString &path)
{
    QSvgRenderer r(path);
    if (!r.isValid())
        return {};
    QSizeF s = r.defaultSize();
    if (s.isEmpty())
        s = r.viewBoxF().size();
    return s;
}

QImage decode(const QString &path, const Header &header, QSize targetPx)
{
    if (!header.ok || !targetPx.isValid())
        return {};
    QImageReader r(path);
    const QByteArray fmt = r.format();
    if (isBlocked(fmt))
        return {};
    if (fmt == "raw") {
        r.setQuality(0);
        QImage img = r.read();
        if (img.isNull())
            return {};
        const int flip = rawFlip(path);
        const bool swap = flip == 5 || flip == 6;
        // only shrink, and before rotating (cheaper)
        const QSize fit = fitDown(img.size(), swap ? targetPx.transposed() : targetPx);
        if (fit != img.size())
            img = img.scaled(fit, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (flip == 3)
            img = img.transformed(QTransform().rotate(180));
        else if (flip == 5)
            img = img.transformed(QTransform().rotate(-90));
        else if (flip == 6)
            img = img.transformed(QTransform().rotate(90));
        return finish(img, targetPx);
    }
    if (fmt == "heif" || fmt == "heic") {
        // size()/transformation() would fully decode the file; the reader applies the rotation itself,
        // and finish() does the downscale (no ScaledSize in kimg_heif)
        r.setAutoTransform(true);
        QImage img;
        r.read(&img);
        return finish(img, targetPx);
    }
    r.setAutoTransform(true);
    const QSize stored = r.size();
    const bool rot = r.transformation() & QImageIOHandler::TransformationRotate90;
    if (stored.isValid())
        r.setScaledSize(fitDown(stored, rot ? targetPx.transposed() : targetPx));
    QImage img;
    r.read(&img);   // on error keeps whatever was decoded (truncated files)
    return finish(img, targetPx);
}

QImage decodeIfCurrent(quint64 generation, const QString &path, const Header &header, QSize targetPx)
{
    if (!pools::isCurrent(generation))
        return {};
    return decode(path, header, targetPx);
}

QImage renderSvg(const QString &path, QSize targetPx)
{
    QSvgRenderer r(path);
    if (!r.isValid() || !targetPx.isValid())
        return {};
    r.setAspectRatioMode(Qt::KeepAspectRatio);
    QImage img(targetPx, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    r.render(&p);
    return img;
}

QImage decodeCover(const QByteArray &bytes, const QByteArray &mime, QSize targetPx)
{
    const QByteArray fmt = mime == "image/png" ? "png" : (mime == "image/jpeg" || mime == "image/jpg") ? "jpeg" : QByteArray();
    if (fmt.isEmpty() || !targetPx.isValid())
        return {};
    QByteArray copy = bytes;
    QBuffer buf(&copy);
    buf.open(QIODevice::ReadOnly);
    QImageReader r(&buf, fmt);
    r.setAutoDetectImageFormat(false);
    const QSize s = r.size();
    if (s.isValid())
        r.setScaledSize(fitDown(s, targetPx));
    QImage img;
    r.read(&img);
    return img;
}

} // namespace ql::image
