#include <QtTest>
#include <KSyntaxHighlighting/Repository>
#include "TextModel.h"
#include "TextRows.h"

using namespace ql;

class TstTextModel : public QObject
{
    Q_OBJECT
    static void colors(TextModel &m)
    {
        m.setColors({{"magenta", "#cc66cc"}, {"blue", "#6699ff"}, {"green", "#66cc66"}, {"yellow", "#cccc66"},
                     {"orange", "#ff9944"}, {"cyan", "#66cccc"}, {"red", "#ff6666"}, {"muted", "#555555"}},
                    "#dddddd", "#101010");
    }
    static QStringList all(TextModel &m, bool reverse)
    {
        QStringList r(m.rowCount());
        for (int k = 0; k < m.rowCount(); ++k) {
            const int i = reverse ? m.rowCount() - 1 - k : k;
            r[i] = m.data(m.index(i), TextModel::RowRole).toString();
        }
        return r;
    }
private Q_SLOTS:
    void plain()
    {
        TextModel m;
        m.setContent({"a  b", "<c>"}, {}, "");
        QCOMPARE(m.rowCount(), 2);
        QCOMPARE(m.data(m.index(0), TextModel::RowRole).toString(), QStringLiteral("a&nbsp; b"));
        QCOMPARE(m.data(m.index(1), TextModel::RowRole).toString(), QStringLiteral("&lt;c&gt;"));
        QCOMPARE(m.roleNames().value(TextModel::RowRole), QByteArray("rowText"));
        QCOMPARE(m.roleNames().value(TextModel::DimRole), QByteArray("dim"));
    }
    void highlighted()
    {
        TextModel m;
        colors(m);
        m.setContent({"int main() { return 0; }"}, TextModel::repository().definitionForName("C++"), "");
        const QString t = m.data(m.index(0), TextModel::RowRole).toString();
        QVERIFY(t.contains("<font color="));
        QVERIFY(t.contains("<b>"));
        QVERIFY(m.data(m.index(0), TextModel::DimRole).isValid());
        QVERIFY(!m.data(m.index(0), TextModel::DimRole).toBool());
    }
    void tagsStrippedEqualsEscape()
    {
        TextModel m;
        colors(m);
        const QString row = "\tint  x = 1;  // <a & b>  ";
        m.setContent({row}, TextModel::repository().definitionForName("C++"), "");
        QString t = m.data(m.index(0), TextModel::RowRole).toString();
        t.remove(QRegularExpression("</?(font|b|i)[^>]*>"));
        QCOMPARE(t, text::escapeRow(text::expandTabs(row)));
    }
    void footer()
    {
        TextModel m;
        m.setContent({"a"}, {}, "Shown <1> of 2");
        QCOMPARE(m.rowCount(), 2);
        QVERIFY(m.data(m.index(1), TextModel::DimRole).toBool());
        QCOMPARE(m.data(m.index(1), TextModel::RowRole).toString(), text::escapeRow(u"Shown <1> of 2"));
        QVERIFY(!m.data(m.index(0), TextModel::DimRole).toBool());
    }
    void order()
    {
        QStringList rows{"/* start", " * middle", " */ int x;", "\"str\";", "#include <a>"};
        for (int i = 0; i < 50; ++i)
            rows << "int f" + QString::number(i) + "(void) { return 1; }";
        TextModel a, b;
        colors(a);
        colors(b);
        const auto def = TextModel::repository().definitionForName("C++");
        a.setContent(rows, def, "");
        b.setContent(rows, def, "");
        const QStringList fwd = all(a, false), rev = all(b, true);
        QCOMPARE(rev, fwd);
        QVERIFY(fwd[1].contains("<i>"));   // comment continues across rows
    }
    void idlePrecompute()
    {
        TextModel m;
        colors(m);
        QStringList rows;
        for (int i = 0; i < 2000; ++i)
            rows << "int x = 1;";
        m.setContent(rows, TextModel::repository().definitionForName("C++"), "");
        QTest::qWait(300);
        QCOMPARE(m.rowCount(), 2000);
        m.clear();
        QCOMPARE(m.rowCount(), 0);
    }
    void style()
    {
        const QHash<QString, QColor> p{{"muted", "#333333"}, {"foreground", "#dddddd"}};
        const QColor fg("#dddddd"), bg("#101010");
        const auto c = styleFor(KSyntaxHighlighting::Theme::Comment, p, fg, bg);
        QVERIFY(c.italic);
        QVERIFY(contrastRatio(c.color, bg) >= 3.0);
        QVERIFY(styleFor(KSyntaxHighlighting::Theme::Keyword, p, fg, bg).bold);
        QCOMPARE(styleFor(KSyntaxHighlighting::Theme::Normal, p, fg, bg).color, fg);
        QCOMPARE(contrastRatio(Qt::black, Qt::white), 21.0);
    }
    void warm() { TextModel::warmUp(); }
};

QTEST_MAIN(TstTextModel)
#include "tst_textmodel.moc"
