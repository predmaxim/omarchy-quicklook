#include "TextRows.h"
#include "Geometry.h"
#include <QFontMetricsF>
#include <QTextLayout>
#include <QTextOption>
#include <algorithm>
#include <cmath>

namespace ql::text {

namespace {

bool isWide(char32_t c)
{
    return (c >= 0x1100 && c <= 0x115F) || (c >= 0x2E80 && c <= 0xA4CF) || (c >= 0xAC00 && c <= 0xD7A3)
        || (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFE30 && c <= 0xFE4F) || (c >= 0xFF00 && c <= 0xFF60)
        || (c >= 0xFFE0 && c <= 0xFFE6) || (c >= 0x20000 && c <= 0x3FFFD);
}

int columnsOf(char32_t c)
{
    const auto cat = QChar::category(c);
    if (cat == QChar::Mark_NonSpacing || cat == QChar::Mark_Enclosing)
        return 0;
    return isWide(c) ? 2 : 1;
}

// html: &nbsp; and entities for StyledText; otherwise U+00A0 only.
QString escapeImpl(QStringView s, bool html)
{
    QString out;
    out.reserve(s.size() + 16);
    const qsizetype n = s.size();
    for (qsizetype i = 0; i < n;) {
        const QChar c = s[i];
        if (c == u' ') {
            qsizetype j = i;
            while (j < n && s[j] == u' ')
                ++j;
            const qsizetype run = j - i;
            const qsizetype nb = (i == 0) ? run : run - 1;   // a leading run is all non-breaking
            for (qsizetype k = 0; k < nb; ++k) {
                if (html)
                    out += QLatin1String("&nbsp;");
                else
                    out += QChar(0xA0);
            }
            if (nb < run)
                out += u' ';
            i = j;
            continue;
        }
        if (html) {
            if (c == u'&')
                out += QLatin1String("&amp;");
            else if (c == u'<')
                out += QLatin1String("&lt;");
            else if (c == u'>')
                out += QLatin1String("&gt;");
            else if (c.unicode() < 0x20 || c.isSpace())
                // StyledText would turn other whitespace into a plain space (and cut the row at NUL)
                out += QLatin1String("&#") + QString::number(c.unicode()) + u';';
            else
                out += c;
        } else {
            out += c;
        }
        ++i;
    }
    return out;
}

} // namespace

QStringList splitRows(const QString &text)
{
    QStringList rows;
    qsizetype pos = 0;
    const qsizetype n = text.size();
    do {
        qsizetype end = text.indexOf(u'\n', pos);
        const bool last = end < 0;
        if (last)
            end = n;
        qsizetype lineEnd = end;
        if (lineEnd > pos && text[lineEnd - 1] == u'\r')
            --lineEnd;
        if (lineEnd == pos)
            rows << QString();
        for (qsizetype p = pos; p < lineEnd;) {
            qsizetype len = std::min<qsizetype>(kMaxRowChars, lineEnd - p);
            if (p + len < lineEnd && text[p + len - 1].isHighSurrogate())
                --len;   // never split a surrogate pair
            rows << text.mid(p, len);
            p += len;
        }
        pos = end + 1;
        if (last || pos >= n)
            break;   // a final '\n' adds no empty row
    } while (true);
    return rows;
}

bool hasLongLine(const QString &text)
{
    const QStringView head = QStringView(text).left(kMeasureChars);
    qsizetype pos = 0;
    while (pos <= head.size()) {
        qsizetype end = head.indexOf(u'\n', pos);
        if (end < 0)
            end = head.size();
        if (end - pos > kMaxRowChars)
            return true;
        pos = end + 1;
    }
    return false;
}

QString expandTabs(QStringView line)
{
    QString out;
    out.reserve(line.size());
    int col = 0;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar c = line[i];
        if (c == u'\t') {
            const int n = kTabWidth - col % kTabWidth;
            out += QString(n, u' ');
            col += n;
        } else if (c.isHighSurrogate() && i + 1 < line.size() && line[i + 1].isLowSurrogate()) {
            col += columnsOf(QChar::surrogateToUcs4(c, line[i + 1]));
            out += c;
            out += line[++i];
        } else {
            col += columnsOf(c.unicode());
            out += c;
        }
    }
    return out;
}

int displayColumns(QStringView expanded)
{
    int cols = 0;
    for (qsizetype i = 0; i < expanded.size(); ++i) {
        const QChar c = expanded[i];
        if (c.isHighSurrogate() && i + 1 < expanded.size() && expanded[i + 1].isLowSurrogate()) {
            cols += columnsOf(QChar::surrogateToUcs4(c, expanded[i + 1]));
            ++i;
        } else {
            cols += columnsOf(c.unicode());
        }
    }
    return cols;
}

QString escapeRow(QStringView expanded) { return escapeImpl(expanded, true); }
QString layoutString(QStringView expanded) { return escapeImpl(expanded, false); }

QFont monospaceFont()
{
    QFont f(QStringLiteral("monospace"));
    f.setPixelSize(16);
    f.setStyleHint(QFont::Monospace);
    return f;
}

Measure measure(const QString &text, const QFont &font, int maxRows)
{
    const QStringList lines = splitRows(text.left(kMeasureChars));
    QStringList expanded;
    expanded.reserve(lines.size());
    int widest = 0;
    for (const QString &l : lines) {
        expanded << expandTabs(l);
        widest = std::max(widest, displayColumns(expanded.last()));
    }
    const QFontMetricsF fm(font);
    Measure m;
    m.columns = std::clamp(widest, geometry::kMinColumns, geometry::kMaxColumns);
    m.advance = fm.horizontalAdvance(QLatin1Char('M'));
    m.rowHeight = int(std::ceil(fm.lineSpacing()));

    const int width = geometry::textViewportWidth(m.columns, m.advance);
    QTextOption opt;
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    for (const QString &l : expanded) {
        if (displayColumns(l) <= m.columns) {
            ++m.rows;
        } else {
            QTextLayout layout(layoutString(l), font);
            layout.setTextOption(opt);
            layout.beginLayout();
            for (;;) {
                QTextLine line = layout.createLine();
                if (!line.isValid())
                    break;
                line.setLineWidth(width);
                ++m.rows;
            }
            layout.endLayout();
        }
        if (m.rows >= maxRows) {
            m.rows = maxRows;
            break;
        }
    }
    return m;
}

} // namespace ql::text
