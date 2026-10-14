#include "Geometry.h"
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace ql::geometry {

namespace {
bool empty(QSizeF s) { return !(s.width() > 0 && s.height() > 0); }

QRectF centered(QSizeF s, QSizeF area)
{
    return QRectF(QPointF((area.width() - s.width()) / 2, (area.height() - s.height()) / 2), s);
}

int effectiveMin(const Monitor &m)
{
    const QSize b = box(m);
    return std::min({kMinSide, b.width(), b.height()});
}

Layout fitIntoBox(QSizeF natural, const Monitor &m)
{
    return around(fitDown(natural, QSizeF(box(m))), effectiveMin(m));
}
} // namespace

QSize box(const Monitor &m)
{
    // epsilon: 0.9 is inexact in binary, keep e.g. 10 * 0.9 from flooring to 8
    return {std::max(1, qFloor(m.work.width() * kBoxFraction + 1e-9)),
            std::max(1, qFloor(m.work.height() * kBoxFraction + 1e-9))};
}

QSizeF fit(QSizeF content, QSizeF bounds)
{
    if (empty(content) || empty(bounds))
        return {0, 0};
    // scaled() alone can overshoot a side by 1 ulp at exactly the bounds' aspect
    return content.scaled(bounds, Qt::KeepAspectRatio).boundedTo(bounds);
}

QSizeF fitDown(QSizeF content, QSizeF bounds)
{
    if (content.width() <= bounds.width() && content.height() <= bounds.height())
        return content;
    return fit(content, bounds);
}

Layout around(QSizeF content, int minSide)
{
    const QSize window(std::max(qCeil(content.width()), minSide), std::max(qCeil(content.height()), minSide));
    return {window, centered(content, QSizeF(window))};
}

Layout imageLayout(QSize pixels, const Monitor &m)
{
    return fitIntoBox(QSizeF(pixels) / m.scale, m);
}

Layout svgLayout(QSizeF documentSize, const Monitor &m)
{
    QSizeF scaled = documentSize;
    if (!empty(documentSize))
        scaled *= std::max(1.0, kMinSide / std::min(documentSize.width(), documentSize.height()));
    const QSizeF b(box(m));
    if (scaled.width() > b.width() || scaled.height() > b.height())
        scaled = fit(scaled, b);
    return around(scaled, effectiveMin(m));
}

Layout pageLayout(QSizeF pagePt, const Monitor &m)
{
    return around(fit(pagePt, QSizeF(box(m))), effectiveMin(m));
}

Layout videoLayout(QSize pixels, const Monitor &m)
{
    return imageLayout(pixels, m);
}

Layout fallbackLayout(QSizeF aspect, const Monitor &m)
{
    return pageLayout(aspect, m);
}

int textViewportWidth(int columns, double advance)
{
    return qCeil(columns * advance) + 1;
}

int textWindowWidth(int columns, double advance)
{
    return textViewportWidth(columns, advance) + 2 * kTextPadding;
}

Layout textLayout(int columns, int rows, double advance, int rowHeight, const Monitor &m)
{
    const QSize b = box(m);
    const int minSide = effectiveMin(m);
    const int w = std::max(std::min(textWindowWidth(columns, advance), b.width()), minSide);
    const int h = std::max(std::min(rows * rowHeight + 2 * kTextPadding, b.height()), minSide);
    return {QSize(w, h), QRectF(0, 0, w, h)};
}

Layout cardLayout(Kind kind)
{
    const QSize s = kind == Kind::Audio ? kAudioCard : kFileCard;
    return {s, QRectF(QPointF(0, 0), QSizeF(s))};
}

QRectF cardRectIn(QSize window, Kind kind)
{
    const QSize s = (kind == Kind::Audio ? kAudioCard : kFileCard).boundedTo(window);
    return centered(QSizeF(s), QSizeF(window));
}

QRectF pageRect(QSizeF viewport, QSizeF pagePt)
{
    if (empty(pagePt))
        return centered({0, 0}, viewport);
    const double w = std::min(viewport.width(), viewport.height() * pagePt.width() / pagePt.height());
    const double h = w * pagePt.height() / pagePt.width();
    return QRectF((viewport.width() - w) / 2, 0, w, h);
}

QRectF fullscreenContent(Kind kind, QSize screen, QSizeF natural, double scale)
{
    const QSizeF s(screen);
    switch (kind) {
    case Kind::Image:
        return centered(fitDown(natural / scale, s), s);
    case Kind::Svg:
    case Kind::Video:
        return centered(fit(natural, s), s);
    case Kind::Pdf:
    case Kind::Office:
        return pageRect(s, natural);
    case Kind::Audio:
    case Kind::FileCard:
    case Kind::FolderCard:
        return cardRectIn(screen, kind);
    case Kind::Text:
    case Kind::None:
        break;
    }
    return QRectF(QPointF(0, 0), s);
}

ZoomRange zoomRange(Kind kind, QSizeF natural, double scale, QSizeF content)
{
    switch (kind) {
    case Kind::Image:
        if (content.isEmpty() || natural.isEmpty() || scale <= 0)
            return {};
        return {1, std::max(1.0, 8 * natural.width() / scale / content.width())};
    case Kind::Svg:
        return {1, 8};
    case Kind::Pdf:
    case Kind::Office:
        return {1, 4};
    case Kind::Text:
        return {1 / (kZoomStep * kZoomStep), 3};
    default:
        return {};
    }
}

double zoomAt(int level, ZoomRange range)
{
    return std::clamp(std::pow(kZoomStep, level), range.min, range.max);
}

} // namespace ql::geometry
