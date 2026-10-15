#pragma once
// Window and content sizes (spec: "Размер окна"). Pure functions, logical px.
#include "Types.h"
#include <QSizeF>

namespace ql::geometry {

inline constexpr int kMinSide = 240;
inline constexpr double kBoxFraction = 0.9;
inline constexpr QSize kFileCard{440, 180};
inline constexpr QSize kAudioCard{520, 180};
inline constexpr int kTextPadding = 48;   // = the margins in TextView.qml and MarkdownView.qml
inline constexpr int kMinColumns = 40;
inline constexpr int kMaxColumns = 120;

QSize box(const Monitor &m);                     // floor(work * kBoxFraction), at least 1x1
QSizeF fit(QSizeF content, QSizeF bounds);       // largest size with content's aspect inside bounds (may enlarge)
QSizeF fitDown(QSizeF content, QSizeF bounds);   // like fit, never enlarges
Layout around(QSizeF content, int minSide);      // window = max(ceil(content), minSide) per side; content centered
Layout imageLayout(QSize pixels, const Monitor &m);     // pixels/scale (1:1 physical), fitDown into box, min side
Layout svgLayout(QSizeF documentSize, const Monitor &m); // min side >= 240 (enlarge), then fit into box if larger
Layout pageLayout(QSizeF pagePt, const Monitor &m);      // fit page aspect into box (enlarge allowed), min side
Layout videoLayout(QSize pixels, const Monitor &m);      // same rules as imageLayout
Layout fallbackLayout(QSizeF aspect, const Monitor &m);  // probe missed its budget: aspect (A4, 4:3, 16:9) fit into box
int textViewportWidth(int columns, double advance);      // ceil(columns * advance) + 1
int textWindowWidth(int columns, double advance);        // textViewportWidth + 2 * kTextPadding
Layout textLayout(int columns, int rows, double advance, int rowHeight, const Monitor &m);
                                                         // width/height clamped to box, min 240; content = whole window
Layout cardLayout(Kind kind);                            // Audio 520x180, FileCard/FolderCard 440x180; content = whole window
QRectF cardRectIn(QSize window, Kind kind);              // card size centered in window, clamped to window
QRectF pageRect(QSizeF viewport, QSizeF pagePt);         // width = min(W, H*w/h), height by aspect, x centered, y = 0
QRectF fullscreenContent(Kind kind, QSize screen, QSizeF natural, double scale);
        // Image: natural px/scale, 1:1 centered if it fits, else fitDown; Svg: fit; Video: fit (enlarge);
        // Pdf/Office: pageRect(screen, natural); Text: whole screen; cards: cardRectIn(screen, kind)

// Zoom (Ctrl +/-/0): level n shows 1.25^n of the default view, clamped to the kind's range.
inline constexpr double kZoomStep = 1.25;
struct ZoomRange {
    double min = 1, max = 1;
};
ZoomRange zoomRange(Kind kind, QSizeF natural, double scale, QSizeF content);
        // Image: [1, 8x of 1:1 (natural px / scale) over the fitted content, at least 1]; Svg: [1, 8];
        // Pdf/Office: [1, 4]; Text (font size): [1.25^-2, 3]; others, or no content yet: [1, 1]
double zoomAt(int level, ZoomRange range);   // clamp(1.25^level, min, max)

} // namespace ql::geometry
