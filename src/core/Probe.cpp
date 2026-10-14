#include "Probe.h"
#include "Geometry.h"
#include "Hyprland.h"
#include "OfficeCache.h"
#include "Thumbnails.h"
#include <QFileInfo>
#include <QFontMetricsF>
#include <QtMath>
#include <algorithm>

namespace ql {
namespace {

void fail(ProbeResult &r, CardReason reason)
{
    r.kind = Kind::FileCard;
    r.reason = reason;
}

// Files thumbnail shrunk (never enlarged) to what the window will show; QML stretches small ones itself.
QImage placeholderFor(const ProbeResult &r, QSizeF contentLogical)
{
    QImage t = loadThumbnail(r.file.canonicalUri, r.file.modified);
    if (t.isNull() || !contentLogical.isValid())
        return t;
    const QSize target = (contentLogical * r.monitor.scale).toSize();
    if (t.width() > target.width() || t.height() > target.height())
        t = t.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return t;
}

void detailText(ProbeResult &r)
{
    r.text = text::readFile(r.file.path);
    if (r.text.encoding.isEmpty()) {   // gone or unreadable since classifyStage read the head (task 7)
        r.reason = CardReason::CannotOpen;
        return;
    }
    const QFont font = text::monospaceFont();
    const int rowH = qCeil(QFontMetricsF(font).lineSpacing());
    const int maxRows = std::max(1, (geometry::box(r.monitor).height() - 32) / std::max(1, rowH));
    r.textMeasure = text::measure(r.text.text, font, maxRows);
    r.highlight = !text::hasLongLine(r.text.text);
    r.layout = geometry::textLayout(r.textMeasure.columns, r.textMeasure.rows, r.textMeasure.advance,
                                    r.textMeasure.rowHeight, r.monitor);
}

void detailOffice(ProbeResult &r)
{
    r.office = office::probe(r.file.path, r.file.displayName, r.file.head);
    if (!r.office.office)
        return fail(r, CardReason::None);
    if (r.office.encrypted)
        return fail(r, CardReason::PasswordProtected);
    r.layout = geometry::pageLayout(r.office.firstPagePt, r.monitor);
    const QString cached = OfficeCache().lookup(r.file.path);
    if (cached.isEmpty())
        return;
    const pdf::Header ph = pdf::probe(cached);   // a broken cached PDF is ignored: converted again
    if (!ph.ok || ph.locked || !ph.firstPagePt.isValid())
        return;
    r.kind = Kind::Pdf;
    r.officeCachedPdf = cached;
    r.pdf = ph;
    r.layout = geometry::pageLayout(ph.firstPagePt, r.monitor);
}

} // namespace

ProbeResult classifyStage(const ProbeRequest &request)
{
    ProbeResult r;
    r.generation = request.generation;
    r.file = resolveFile(request.uri);
    r.kind = classify(r.file);
    if (!r.file.exists)
        r.reason = CardReason::CannotOpen;
    else if (!r.file.isDir && r.file.size > 0 && r.file.head.isEmpty())   // no permission to read
        fail(r, CardReason::CannotOpen);
    r.monitor = hyprland::currentMonitor(request.preferredMonitor, request.fallbackMonitor);
    return r;
}

void detailStage(ProbeResult &r)
{
    if (r.reason == CardReason::None) {
        switch (r.kind) {
        case Kind::Image: {
            r.image = image::probe(r.file.path);
            if (!r.image.ok)
                fail(r, CardReason::CannotOpen);
            else if (!r.image.raw && image::tooLarge(r.image))   // RAW: only the embedded preview is decoded
                fail(r, CardReason::TooLarge);
            else {
                r.layout = geometry::imageLayout(r.image.displayPx, r.monitor);
                r.placeholder = placeholderFor(r, r.layout.content.size());
            }
            break;
        }
        case Kind::Svg:
            r.svgSize = image::svgSize(r.file.path);
            if (r.svgSize.isEmpty())
                fail(r, CardReason::CannotOpen);
            else
                r.layout = geometry::svgLayout(r.svgSize, r.monitor);
            break;
        case Kind::Pdf:
            r.pdf = pdf::probe(r.file.path);
            if (r.pdf.locked)
                fail(r, CardReason::PasswordProtected);
            else if (!r.pdf.ok || !r.pdf.firstPagePt.isValid())
                fail(r, CardReason::CannotOpen);
            else {
                r.layout = geometry::pageLayout(r.pdf.firstPagePt, r.monitor);
                r.placeholder = placeholderFor(r, r.layout.content.size());
            }
            break;
        case Kind::Office:
            detailOffice(r);
            break;
        case Kind::Text:
            detailText(r);
            break;
        case Kind::Audio:
            r.audio = readAudioTags(r.file.path);
            r.layout = geometry::cardLayout(Kind::Audio);
            break;
        case Kind::Video:
            r.placeholder = loadThumbnail(r.file.canonicalUri, r.file.modified);   // Controller scales it
            break;
        default:
            break;
        }
    }
    if (r.kind == Kind::FileCard || r.kind == Kind::FolderCard || r.kind == Kind::None || r.reason != CardReason::None) {
        if (r.kind != Kind::FolderCard)
            r.kind = Kind::FileCard;
        r.layout = geometry::cardLayout(r.kind);
    }
    r.detailed = true;
}

Layout fallbackFor(const ProbeResult &r)
{
    switch (r.kind) {
    case Kind::Svg:
        return geometry::fallbackLayout(QSizeF(4, 3), r.monitor);
    case Kind::Pdf:
    case Kind::Office: {
        const QString suffix = QFileInfo(r.file.displayName.isEmpty() ? r.file.path : r.file.displayName).suffix().toLower();
        const bool slides = suffix == QLatin1String("ppt") || suffix == QLatin1String("pptx");
        return geometry::fallbackLayout(slides ? QSizeF(4, 3) : office::kA4, r.monitor);
    }
    case Kind::Video:
        return geometry::fallbackLayout(QSizeF(16, 9), r.monitor);
    default:
        return r.layout.window.isValid() ? r.layout : geometry::cardLayout(r.kind == Kind::None ? Kind::FileCard : r.kind);
    }
}

ProbeResult runProbe(const ProbeRequest &request)
{
    ProbeResult r = classifyStage(request);
    detailStage(r);
    return r;
}

} // namespace ql
