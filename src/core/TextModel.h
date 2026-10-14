#pragma once
// Row model with lazy KSyntaxHighlighting (spec: "Текст").
#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/State>
#include <KSyntaxHighlighting/Theme>
#include <QAbstractListModel>
#include <QColor>
#include <QHash>
#include <QStringList>
#include <QTimer>
#include <QVector>

namespace KSyntaxHighlighting { class Repository; }

namespace ql {

struct StyleSpec {
    QColor color;
    bool bold = false;
    bool italic = false;
};
// keyword/control flow: magenta bold; function: blue; string family: green; data type: yellow;
// numbers/constants/preprocessor: orange; built-in/char: cyan; variable: red; alert/error: red bold;
// comment family: muted italic lifted toward foreground until contrast >= 3:1; Normal/others: foreground
StyleSpec styleFor(KSyntaxHighlighting::Theme::TextStyle style, const QHash<QString, QColor> &palette,
                   const QColor &foreground, const QColor &background);
double contrastRatio(const QColor &a, const QColor &b);

class TextModel : public QAbstractListModel, public KSyntaxHighlighting::AbstractHighlighter {
    Q_OBJECT
    Q_INTERFACES(KSyntaxHighlighting::AbstractHighlighter)
public:
    enum Roles { RowRole = Qt::UserRole + 1, DimRole };   // QML role names: "rowText", "dim"

    explicit TextModel(QObject *parent = nullptr);

    static KSyntaxHighlighting::Repository &repository();   // GUI thread, created on first use
    static void warmUp();     // loads Markdown, C++, Python, Bash, JavaScript, JSON definitions

    // rows from text::splitRows; definition invalid -> plain; footer "" -> none (dim last row otherwise)
    void setContent(const QStringList &rows, const KSyntaxHighlighting::Definition &definition,
                    const QString &footer);
    void setColors(const QHash<QString, QColor> &palette, const QColor &foreground, const QColor &background);
    void clear();             // also stops the idle precompute

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;   // RowRole: StyledText (escaped)
    QHash<int, QByteArray> roleNames() const override;

protected:
    void applyFormat(int offset, int length, const KSyntaxHighlighting::Format &format) override;

private:
    QString renderRow(int row);    // computes states up to row (lazy) and builds StyledText
    void precomputeSlice();        // idle: at most 5 ms per slice
    void highlightNext();          // highlight row m_states.size() without collecting spans

    QStringList m_rows;
    QString m_footer;
    bool m_highlight = false;
    QVector<KSyntaxHighlighting::State> m_states;   // state after row i
    QHash<int, QString> m_cache;
    QHash<QString, QColor> m_palette;
    QColor m_foreground, m_background;
    struct Span { int offset; int length; StyleSpec style; };
    QVector<Span> m_spans;
    bool m_collect = false;   // applyFormat records spans only while renderRow highlights its own row
    QTimer m_idle;
};

} // namespace ql
