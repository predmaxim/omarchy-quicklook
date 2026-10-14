#include <QtTest>
#include <QFontMetricsF>
#include <QTextLayout>
#include <QTextOption>
#include "Geometry.h"
#include "TextRows.h"

using namespace ql::text;

class TstTextRows : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void escape_data()
    {
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("out");
        QTest::newRow("leading") << "    abc" << "&nbsp;&nbsp;&nbsp;&nbsp;abc";
        QTest::newRow("leading1") << " a" << "&nbsp;a";
        QTest::newRow("inner") << "a    b" << "a&nbsp;&nbsp;&nbsp; b";
        QTest::newRow("single") << "a b" << "a b";
        QTest::newRow("trailing") << "a  " << "a&nbsp; ";
        QTest::newRow("markup") << "<a & b>" << "&lt;a &amp; b&gt;";
        QTest::newRow("ideographic space") << QString(QChar(0x3000)) + "x" << "&#12288;x";
        QTest::newRow("nul") << QString(QChar(0)) + "x" << "&#0;x";
    }
    void escape()
    {
        QFETCH(QString, in);
        QFETCH(QString, out);
        QCOMPARE(escapeRow(in), out);
    }
    void tabs()
    {
        QCOMPARE(expandTabs(u"\tx"), QStringLiteral("    x"));
        QCOMPARE(expandTabs(u"ab\tc"), QStringLiteral("ab  c"));
    }
    void layout()
    {
        QCOMPARE(layoutString(u"a    b"), QStringLiteral("a") + QString(3, QChar(0xA0)) + " b");
        QCOMPARE(layoutString(u"  a"), QString(2, QChar(0xA0)) + "a");
    }
    void columns()
    {
        QCOMPARE(displayColumns(u"漢字"), 4);
        QCOMPARE(displayColumns(u"abc"), 3);
        QCOMPARE(displayColumns(u"é"), 1);
    }
    void split()
    {
        QCOMPARE(splitRows(QString(4500, 'x')).size(), 3);
        QCOMPARE(splitRows("a\r\nb"), (QStringList{"a", "b"}));
        QCOMPARE(splitRows("a\n"), (QStringList{"a"}));
        QCOMPARE(splitRows(""), (QStringList{""}));
        QString s = QString(1999, 'x') + QString::fromUcs4(U"\U0001F600");
        const QStringList r = splitRows(s);
        QCOMPARE(r.size(), 2);
        QCOMPARE(r.join(""), s);
    }
    void longLine()
    {
        QVERIFY(!hasLongLine("a\nb"));
        QVERIFY(hasLongLine("a\n" + QString(2001, 'x')));
        QVERIFY(!hasLongLine(QString(2000, 'x')));
        QVERIFY(!hasLongLine(QString(kMeasureChars + 10, '\n') + QString(3000, 'x')));
    }
    void measureShort()
    {
        const auto m = measure("a\nb\nc", monospaceFont(), 1000);
        QCOMPARE(m.columns, ql::geometry::kMinColumns);
        QCOMPARE(m.rows, 3);
        QCOMPARE(m.rowHeight, int(std::ceil(QFontMetricsF(monospaceFont()).lineSpacing())));
    }
    void measureWide()
    {
        const QFont f = monospaceFont();
        auto m = measure(QString(100, 'x'), f, 1000);
        QCOMPARE(m.columns, 100);
        QCOMPARE(m.rows, 1);
        QTextLayout l(layoutString(QString(100, 'x')), f);
        QTextOption o;
        o.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        l.setTextOption(o);
        l.beginLayout();
        int n = 0;
        for (;;) {
            QTextLine ln = l.createLine();
            if (!ln.isValid())
                break;
            ln.setLineWidth(ql::geometry::textViewportWidth(100, m.advance));
            ++n;
        }
        l.endLayout();
        QCOMPARE(n, 1);
        m = measure(QString(130, 'x'), f, 1000);
        QCOMPARE(m.columns, 120);
        QCOMPARE(m.rows, 2);
        QCOMPARE(measure("a\nb\nc\nd", f, 2).rows, 2);
    }
};

QTEST_MAIN(TstTextRows)
#include "tst_textrows.moc"
