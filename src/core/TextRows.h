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
    int rowHeight = 0;    // ceil(QFontMetricsF(font).lineSpacing())
};
Measure measure(const QString &text, const QFont &font, int maxRows);
QFont monospaceFont();    // family "monospace", pixel size 16

} // namespace ql::text
