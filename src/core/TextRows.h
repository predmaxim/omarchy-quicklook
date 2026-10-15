#pragma once
// Rows, escaping and size prediction for the text viewer (spec: "Текст", "Размер окна").
#include <QFont>
#include <QString>
#include <QStringList>
#include <QStringView>

namespace ql::text {

inline constexpr int kTabWidth = 4;
inline constexpr int kMaxRowChars = 2000;
inline constexpr int kMeasureChars = 64 * 1024;
inline constexpr double kLineHeight = 1.7;   // = lineHeight in TextView.qml; Markdown: set by markdownHtml
inline constexpr int kCellPadding = 10;
// ponytail: bigger Markdown shows raw rows; QQuickText lays 0.85 MB with tables out in ~4 s on the GUI thread.
// Raise it only with a layout off the GUI thread.
inline constexpr int kMaxMarkdownChars = 128 * 1024;

QStringList splitRows(const QString &text);   // split on '\n' (drop trailing '\r'), cut rows longer than kMaxRowChars
bool hasLongLine(const QString &text);        // a line longer than kMaxRowChars within the first kMeasureChars
QString expandTabs(QStringView line);         // tabs -> spaces up to the next multiple of kTabWidth
int displayColumns(QStringView expanded);     // East Asian wide/fullwidth count 2
QString escapeRow(QStringView expanded);      // & < > escaped; leading run -> all &nbsp;;
                                              // inner run of N spaces -> (N-1) x &nbsp; + one ' '; single space stays
QString layoutString(QStringView expanded);   // same space rule with U+00A0 instead of &nbsp; (for QTextLayout)

struct Measure {
    int columns = 0;      // clamped to geometry::kMinColumns..kMaxColumns
    int rows = 0;         // wrapped rows of the first kMeasureChars at textViewportWidth(columns), capped by maxRows
    double advance = 0;   // QFontMetricsF(font).horizontalAdvance('M')
    int rowHeight = 0;    // rowHeight(font)
};
Measure measure(const QString &text, const QFont &font, int maxRows);
// Markdown -> HTML for a QML Text.RichText, built here because QQuickText's own Markdown both hangs (lineHeight
// re-lays the document out per block) and crashes (resourceRequestFinished after an image download). Paragraphs
// get kLineHeight, table cells line height 1 and kCellPadding, images become their alt text (nothing to fetch),
// <body> loses its style so the QML font (zoom) applies.
QString markdownHtml(const QString &markdown);
int rowHeight(const QFont &font);   // ceil(ceil(lineSpacing) * kLineHeight): a QQuickText row at lineHeight kLineHeight
QFont monospaceFont();    // family "monospace", pixel size 16

} // namespace ql::text
