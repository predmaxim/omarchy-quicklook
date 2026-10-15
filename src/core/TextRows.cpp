#include "TextRows.h"
#include "Geometry.h"
#include <QFontMetricsF>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QTextOption>
#include <QTextTable>
#include <algorithm>
#include <cmath>

namespace ql::text {

namespace {

// Fixes before Qt's Markdown import, fenced code left alone:
// - GitHub lets a table interrupt a paragraph (a "**[details]**" line right above it), Qt's md4c does not and shows
//   the rows as text: a blank line goes in before such a header row;
// - <br> (the only line break a table cell has) is inserted by Qt as HTML that wrecks the table and empties
//   everything after it: U+2028 instead, a line break inside the block;
// - Qt merges an empty cell into the one on its left (GitHub keeps it empty): &nbsp; in it.
QString prepareMarkdown(const QString &markdown)
{
    static const QRegularExpression br(QStringLiteral("\\s*<br\\s*/?>\\s*"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression emptyCell(QStringLiteral("(?<=\\|)\\s*(?=\\|)"));
    static const QRegularExpression delimiter(QStringLiteral("^ {0,3}\\|?\\s*:?-+:?\\s*(\\|\\s*:?-+:?\\s*)*\\|?\\s*$"));
    static const QRegularExpression fence(QStringLiteral("^ {0,3}(```|~~~)"));
    const QStringList lines = markdown.split(u'\n');
    bool inFence = false, inTable = false;
    QStringList out;
    out.reserve(lines.size());
    for (qsizetype i = 0; i < lines.size(); ++i) {
        QString l = lines.at(i);
        if (fence.match(l).hasMatch()) {
            inFence = !inFence;
            inTable = false;
        } else if (!inFence && i + 1 < lines.size() && l.contains(u'|') && lines.at(i + 1).contains(u'|')
                   && delimiter.match(lines.at(i + 1)).hasMatch()) {
            if (i > 0 && !lines.at(i - 1).trimmed().isEmpty())
                out << QString();
            inTable = true;
        } else if (inTable && (l.trimmed().isEmpty() || !l.contains(u'|'))) {
            inTable = false;
        }
        if (!inFence)
            l.replace(br, QString(QChar::LineSeparator));
        if (inTable && !delimiter.match(l).hasMatch())
            l.replace(emptyCell, QStringLiteral(" &nbsp; "));
        out << l;
    }
    return out.join(u'\n');
}

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

QString markdownHtml(const QString &markdown)
{
    QTextDocument doc;   // no layout is ever requested: edits cost no relayout
    // 0x4000 is md4c's MD_FLAG_UNDERLINE: without it _x_ is italic, as on GitHub
    doc.setMarkdown(prepareMarkdown(markdown),
                    QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub & ~0x4000));
    QTextCursor edit(&doc);
    edit.beginEditBlock();
    struct Image { int position; int length; QString alt; QTextCharFormat format; };
    QList<Image> images;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        QTextCursor c(b);
        QTextBlockFormat f = b.blockFormat();
        if (QTextTable *t = c.currentTable()) {
            f.setLineHeight(100, QTextBlockFormat::ProportionalHeight);
            QTextTableFormat tf = t->format();
            if (tf.cellPadding() != kCellPadding) {
                tf.setCellPadding(kCellPadding);
                t->setFormat(tf);
            }
        } else {
            f.setLineHeight(kLineHeight * 100, QTextBlockFormat::ProportionalHeight);
        }
        c.setBlockFormat(f);
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (!frag.charFormat().isImageFormat())
                continue;
            QTextCharFormat text = frag.charFormat();   // the surrounding style (Qt drops a link around an image)
            text.setObjectType(QTextFormat::NoObject);
            text.clearProperty(QTextFormat::ImageName);
            images.append({frag.position(), frag.length(), frag.charFormat().property(QTextFormat::ImageAltText).toString(), text});
        }
    }
    for (auto i = images.crbegin(); i != images.crend(); ++i) {   // from the end: positions stay valid
        QTextCursor c(&doc);
        c.setPosition(i->position);
        c.setPosition(i->position + i->length, QTextCursor::KeepAnchor);
        c.insertText(i->alt, i->format);
    }
    edit.endEditBlock();
    static const QRegularExpression body(QStringLiteral("<body style=\"[^\"]*\">"));
    return doc.toHtml().replace(body, QStringLiteral("<body>"));
}

int rowHeight(const QFont &font)
{
    return int(std::ceil(std::ceil(QFontMetricsF(font).lineSpacing()) * kLineHeight));
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
    m.rowHeight = rowHeight(font);

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
