#include "TextModel.h"
#include "TextRows.h"
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/Repository>
#include <QElapsedTimer>
#include <QtGlobal>
#include <cmath>

namespace ql {

using KSyntaxHighlighting::Theme;

namespace {

double luminance(const QColor &c)
{
    auto lin = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * lin(c.redF()) + 0.7152 * lin(c.greenF()) + 0.0722 * lin(c.blueF());
}

// Source-char -> escaped-offset map, recovered from escapeRow's own output: every source char
// is one entity (&amp; &lt; &gt; &nbsp; &#N;) or one raw UTF-16 unit.
QList<int> escapedOffsets(const QString &escaped, int sourceLen)
{
    QList<int> map;
    map.reserve(sourceLen + 1);
    for (int i = 0; i < escaped.size();) {
        map << i;
        if (escaped[i] == u'&')
            i = int(escaped.indexOf(u';', i)) + 1;
        else
            ++i;
    }
    map << int(escaped.size());
    Q_ASSERT(map.size() == sourceLen + 1);
    return map;
}

} // namespace

double contrastRatio(const QColor &a, const QColor &b)
{
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

StyleSpec styleFor(Theme::TextStyle style, const QHash<QString, QColor> &palette, const QColor &foreground,
                   const QColor &background)
{
    auto pick = [&](const char *key) { return palette.value(QLatin1String(key), foreground); };
    switch (style) {
    case Theme::Keyword:
    case Theme::ControlFlow:
        return {pick("magenta"), true, false};
    case Theme::Function:
        return {pick("blue")};
    case Theme::String:
    case Theme::VerbatimString:
    case Theme::SpecialString:
        return {pick("green")};
    case Theme::DataType:
        return {pick("yellow")};
    case Theme::DecVal:
    case Theme::BaseN:
    case Theme::Float:
    case Theme::Constant:
    case Theme::Preprocessor:
        return {pick("orange")};
    case Theme::BuiltIn:
    case Theme::Char:
        return {pick("cyan")};
    case Theme::Variable:
        return {pick("red")};
    case Theme::Alert:
    case Theme::Error:
        return {pick("red"), true, false};
    case Theme::Comment:
    case Theme::Documentation:
    case Theme::Annotation:
    case Theme::CommentVar: {
        const QColor muted = pick("muted");
        QColor c = muted;
        for (int i = 1; i <= 20 && contrastRatio(c, background) < 3.0; ++i) {
            const double t = i / 20.0;
            c = QColor::fromRgbF(muted.redF() + (foreground.redF() - muted.redF()) * t,
                                 muted.greenF() + (foreground.greenF() - muted.greenF()) * t,
                                 muted.blueF() + (foreground.blueF() - muted.blueF()) * t);
        }
        return {c, false, true};
    }
    default:
        return {foreground};
    }
}

TextModel::TextModel(QObject *parent) : QAbstractListModel(parent)
{
    m_idle.setSingleShot(true);
    m_idle.setInterval(0);
    connect(&m_idle, &QTimer::timeout, this, [this] { precomputeSlice(); });
}

KSyntaxHighlighting::Repository &TextModel::repository()
{
    static KSyntaxHighlighting::Repository repo;
    return repo;
}

void TextModel::warmUp()
{
    for (const char *name : {"Markdown", "C++", "Python", "Bash", "JavaScript", "JSON"}) {
        const auto def = repository().definitionForName(QLatin1String(name));
        if (def.isValid())
            def.keywordLists();   // forces the definition (and its includes) to load
    }
}

void TextModel::setContent(const QStringList &rows, const KSyntaxHighlighting::Definition &definition,
                           const QString &footer)
{
    m_idle.stop();
    beginResetModel();
    m_rows = rows;
    m_footer = footer;
    m_highlight = definition.isValid();
    setDefinition(definition);
    m_states.clear();
    m_cache.clear();
    endResetModel();
    if (m_highlight && !m_rows.isEmpty())
        m_idle.start();
}

void TextModel::setColors(const QHash<QString, QColor> &palette, const QColor &foreground, const QColor &background)
{
    m_palette = palette;
    m_foreground = foreground;
    m_background = background;
    m_cache.clear();
    if (rowCount() > 0)
        Q_EMIT dataChanged(index(0), index(rowCount() - 1), {RowRole});
}

void TextModel::clear() { setContent({}, {}, {}); }

int TextModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size()) + (m_footer.isEmpty() ? 0 : 1);
}

QVariant TextModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount())
        return {};
    const bool footer = index.row() >= m_rows.size();
    if (role == DimRole)
        return footer;
    if (role != RowRole)
        return {};
    return const_cast<TextModel *>(this)->renderRow(index.row());
}

QHash<int, QByteArray> TextModel::roleNames() const
{
    return {{RowRole, "rowText"}, {DimRole, "dim"}};
}

void TextModel::applyFormat(int offset, int length, const KSyntaxHighlighting::Format &format)
{
    if (!m_collect || length <= 0)
        return;
    m_spans.push_back({offset, length, styleFor(format.textStyle(), m_palette, m_foreground, m_background)});
}

void TextModel::highlightNext()
{
    const int i = int(m_states.size());
    m_states.push_back(highlightLine(text::expandTabs(m_rows[i]), i > 0 ? m_states[i - 1] : KSyntaxHighlighting::State()));
}

QString TextModel::renderRow(int row)
{
    if (const auto it = m_cache.constFind(row); it != m_cache.constEnd())
        return *it;

    QString result;
    if (row >= m_rows.size()) {
        result = text::escapeRow(m_footer);
    } else if (!m_highlight) {
        result = text::escapeRow(text::expandTabs(m_rows[row]));
    } else {
        while (m_states.size() < row)
            highlightNext();
        const QString expanded = text::expandTabs(m_rows[row]);
        m_spans.clear();
        m_collect = true;
        const auto state = highlightLine(expanded, row > 0 ? m_states[row - 1] : KSyntaxHighlighting::State());
        m_collect = false;
        if (m_states.size() == row)
            m_states.push_back(state);

        const QString escaped = text::escapeRow(expanded);
        const QList<int> map = escapedOffsets(escaped, int(expanded.size()));
        int done = 0;   // escaped offset already copied
        for (const Span &s : std::as_const(m_spans)) {
            const int a = map[qBound(0, s.offset, int(expanded.size()))];
            const int b = map[qBound(0, s.offset + s.length, int(expanded.size()))];
            const bool colored = s.style.color.isValid() && s.style.color != m_foreground;
            if (a < done || b <= a || !(colored || s.style.bold || s.style.italic))
                continue;
            result += QStringView(escaped).mid(done, a - done);
            QString close;
            if (colored) {
                result += QLatin1String("<font color=\"") + s.style.color.name() + QLatin1String("\">");
                close = QLatin1String("</font>");
            }
            if (s.style.bold) {
                result += QLatin1String("<b>");
                close.prepend(QLatin1String("</b>"));
            }
            if (s.style.italic) {
                result += QLatin1String("<i>");
                close.prepend(QLatin1String("</i>"));
            }
            result += QStringView(escaped).mid(a, b - a) + close;
            done = b;
        }
        result += QStringView(escaped).mid(done);
    }
    m_cache.insert(row, result);
    return result;
}

void TextModel::precomputeSlice()
{
    QElapsedTimer t;
    t.start();
    while (m_states.size() < m_rows.size() && t.elapsed() < 5)
        highlightNext();
    if (m_states.size() < m_rows.size())
        m_idle.start();
}

} // namespace ql
