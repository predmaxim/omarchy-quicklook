#include "Pdf.h"
#include "PdfModel.h"
#include "Pools.h"
#include <QElapsedTimer>
#include <QFile>
#include <QPageLayout>
#include <QPainter>
#include <QPdfWriter>
#include <QProcess>
#include <QQuickImageResponse>
#include <QQuickTextureFactory>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace ql;

class TstPdf : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    QString two, garbage, truncated, locked;

    QString p(const char *n) const { return dir.filePath(QString::fromLatin1(n)); }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(dir.isValid());
        two = p("two.pdf");
        {
            QPdfWriter w(two);
            w.setPageSize(QPageSize(QPageSize::A4));
            QPainter pa(&w);
            pa.drawText(100, 100, QStringLiteral("page one"));
            w.setPageLayout(QPageLayout(QPageSize(QPageSize::A4), QPageLayout::Landscape, QMarginsF()));
            w.newPage();
            pa.drawText(100, 100, QStringLiteral("page two"));
        }
        garbage = p("garbage.pdf");
        QFile g(garbage);
        QVERIFY(g.open(QIODevice::WriteOnly));
        QByteArray rnd(4096, 0);
        for (char &c : rnd)
            c = char(QRandomGenerator::global()->generate());
        g.write(rnd);
        g.close();
        QFile t(two);
        QVERIFY(t.open(QIODevice::ReadOnly));
        const QByteArray all = t.readAll();
        truncated = p("truncated.pdf");
        QFile o(truncated);
        QVERIFY(o.open(QIODevice::WriteOnly));
        o.write(all.left(all.size() * 6 / 10));
        o.close();
        locked = p("locked.pdf");
        if (QProcess::execute(QStringLiteral("qpdf"), {QStringLiteral("--encrypt"), QStringLiteral("u"), QStringLiteral("o"),
                                                       QStringLiteral("256"), QStringLiteral("--"), two, locked}) != 0)
            locked.clear();
    }

    void probeGood()
    {
        const auto h = pdf::probe(two);
        QVERIFY(h.ok);
        QVERIFY(!h.locked);
        QCOMPARE(h.pages, 2);
        QVERIFY(qAbs(h.firstPagePt.width() - 595) < 2 && qAbs(h.firstPagePt.height() - 842) < 2);
    }
    void probeGarbage() { QVERIFY(!pdf::probe(garbage).ok); }
    void probeMissing() { QVERIFY(!pdf::probe(p("nope.pdf")).ok); }
    void probeLocked()
    {
        if (locked.isEmpty())
            QSKIP("qpdf missing");
        const auto h = pdf::probe(locked);
        QVERIFY(h.ok && h.locked);
    }
    void probeTruncated()
    {
        QElapsedTimer t;
        t.start();
        pdf::probe(truncated);
        QVERIFY(t.elapsed() < 2000);
    }

    void render()
    {
        QSizeF pt;
        const QImage a = pdf::renderPage(two, 0, 600, nullptr, &pt);
        QCOMPARE(a.width(), 600);
        QCOMPARE(a.pixelColor(5, 5), QColor(Qt::white));
        QVERIFY(qAbs(pt.width() - 595) < 2);
        const QImage b = pdf::renderPage(two, 1, 600, nullptr, nullptr);
        QVERIFY(b.width() > b.height());
        QVERIFY(pdf::renderPage(two, 9, 600, nullptr, nullptr).isNull());
    }
    void renderAborted()
    {
        std::atomic_bool abort{true};
        QVERIFY(pdf::renderPage(two, 0, 600, &abort, nullptr).isNull());
    }

    void model()
    {
        PdfModel m;
        QSignalSpy count(&m, &PdfModel::countChanged);
        m.setDocument(7, two, 2, QSizeF(595, 842));
        QCOMPARE(m.rowCount(), 2);
        QCOMPARE(count.size(), 1);
        for (int i = 0; i < 2; ++i) {
            QVERIFY(qAbs(m.data(m.index(i), PdfModel::AspectRole).toDouble() - 1.414) < 0.01);
            QCOMPARE(m.data(m.index(i), PdfModel::PageRole).toInt(), i);
            QCOMPARE(m.data(m.index(i), PdfModel::SourceRole).toString(), QStringLiteral("image://pdf/7/%1").arg(i));
        }
        QSignalSpy changed(&m, &PdfModel::dataChanged);
        m.setPageSize(6, 1, QSizeF(842, 595));   // stale
        QCOMPARE(changed.size(), 0);
        m.setPageSize(7, 1, QSizeF(842, 595));
        QCOMPARE(changed.size(), 1);
        QVERIFY(qAbs(m.data(m.index(1), PdfModel::AspectRole).toDouble() - 0.707) < 0.01);
        QCOMPARE(PdfModel::documentPath(7), two);
        m.setDocument(8, garbage, 1, {});
        QCOMPARE(PdfModel::documentPath(7), QString());
        QCOMPARE(PdfModel::documentPath(8), garbage);
        m.clear();
        QCOMPARE(m.count(), 0);
    }

    void provider()
    {
        PdfModel m;
        pools::currentGeneration() = 21;
        m.setDocument(21, two, 2, QSizeF(595, 842));
        PdfPageProvider prov(&m);
        QQuickImageResponse *r = prov.requestImageResponse(QStringLiteral("21/1"), QSize(300, 0));
        QSignalSpy fin(r, &QQuickImageResponse::finished);
        QSignalSpy changed(&m, &PdfModel::dataChanged);
        QTRY_COMPARE_WITH_TIMEOUT(fin.size(), 1, 5000);
        std::unique_ptr<QQuickTextureFactory> tf(r->textureFactory());
        QVERIFY(tf);
        QVERIFY(r->errorString().isEmpty());
        QTRY_COMPARE(changed.size(), 1);
        QVERIFY(qAbs(m.data(m.index(1), PdfModel::AspectRole).toDouble() - 0.707) < 0.01);
        r->deleteLater();

        // cancelled immediately: finished still emitted once
        QQuickImageResponse *c = prov.requestImageResponse(QStringLiteral("21/0"), QSize(3000, 0));
        QSignalSpy cf(c, &QQuickImageResponse::finished);
        c->cancel();
        QTRY_COMPARE_WITH_TIMEOUT(cf.size(), 1, 5000);
        QTest::qWait(50);
        QCOMPARE(cf.size(), 1);
        c->deleteLater();

        // stale generation: empty image, finished once
        pools::currentGeneration() = 22;
        QQuickImageResponse *s = prov.requestImageResponse(QStringLiteral("21/0"), QSize(300, 0));
        QSignalSpy sf(s, &QQuickImageResponse::finished);
        QTRY_COMPARE_WITH_TIMEOUT(sf.size(), 1, 5000);
        s->deleteLater();
    }
};

QTEST_MAIN(TstPdf)
#include "tst_pdf.moc"
