#include "Office.h"
#include <KZip>
#include <QBuffer>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace ql::office;

namespace {
QString fx(const char *n) { return QStringLiteral(QL_SOURCE_DIR "/tests/fixtures/office/") + n; }
QByteArray head(const QString &p) { QFile f(p); return f.open(QIODevice::ReadOnly) ? f.read(4096) : QByteArray(); }
bool near(const QSizeF &a, double w, double h) { return qAbs(a.width() - w) < 0.1 && qAbs(a.height() - h) < 0.1; }

QByteArray sect(int w, int h, const char *orient = nullptr)
{
    return QByteArray("<w:sectPr><w:pgSz w:w=\"") + QByteArray::number(w) + "\" w:h=\"" + QByteArray::number(h) + "\""
        + (orient ? QByteArray(" w:orient=\"") + orient + "\"" : QByteArray()) + "/></w:sectPr>";
}
QByteArray doc(const QByteArray &body)
{
    return "<w:document xmlns:w=\"x\"><w:body>" + body + "</w:body></w:document>";
}
QByteArray sheetXml(bool rows, const QByteArray &pageSetup)
{
    return "<worksheet><sheetData>" + QByteArray(rows ? "<row r=\"1\"/>" : "") + "</sheetData>" + pageSetup + "</worksheet>";
}
}

class TstOffice : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void docx()
    {
        QVERIFY(near(predictDocx(doc(sect(11906, 16838))), 595.3, 841.9));
        QVERIFY(near(predictDocx(doc(sect(16838, 11906, "landscape"))), 841.9, 595.3));
        QVERIFY(near(predictDocx(doc(sect(11906, 16838, "landscape"))), 841.9, 595.3));   // orient forces w > h
        QVERIFY(near(predictDocx(doc("<w:p><w:pPr>" + sect(12240, 15840) + "</w:pPr></w:p>" + sect(11906, 16838))), 612, 792));
        QVERIFY(predictDocx(doc("<w:p/>")).isEmpty());
        QVERIFY(predictDocx("garbage").isEmpty());
    }
    void xlsx()
    {
        const QByteArray wb = "<workbook xmlns:r=\"r\"><sheets>"
            "<sheet name=\"a\" sheetId=\"1\" state=\"hidden\" r:id=\"rId1\"/>"
            "<sheet name=\"b\" sheetId=\"2\" r:id=\"rId2\"/></sheets></workbook>";
        const QByteArray rels = "<Relationships><Relationship Id=\"rId1\" Target=\"worksheets/sheet1.xml\"/>"
                                "<Relationship Id=\"rId2\" Target=\"worksheets/sheet2.xml\"/></Relationships>";
        auto run = [&](const QByteArray &s2, const QByteArray &wbx = QByteArray()) {
            return predictXlsx(wbx.isEmpty() ? wb : wbx, rels, [&](const QString &e) {
                return e == "xl/worksheets/sheet1.xml" ? sheetXml(true, "<pageSetup paperSize=\"1\"/>")
                     : e == "xl/worksheets/sheet2.xml" ? s2 : QByteArray();
            });
        };
        QVERIFY(near(run(sheetXml(true, "<pageSetup paperSize=\"9\" orientation=\"landscape\"/>")), 841.89, 595.28));
        QVERIFY(near(run(sheetXml(true, "<pageSetup orientation=\"portrait\"/>")), 612, 792));
        QVERIFY(near(run(sheetXml(true, "")), 612, 792));
        // empty first visible sheet is skipped
        const QByteArray wb2 = "<workbook xmlns:r=\"r\"><sheets><sheet name=\"a\" r:id=\"rId1\"/><sheet name=\"b\" r:id=\"rId2\"/></sheets></workbook>";
        const QSizeF s = predictXlsx(wb2, rels, [&](const QString &e) {
            return e.endsWith("sheet1.xml") ? sheetXml(false, "<pageSetup paperSize=\"5\"/>") : sheetXml(true, "<pageSetup paperSize=\"9\"/>");
        });
        QVERIFY(near(s, 595.28, 841.89));
    }
    void odf()
    {
        auto st = [](const char *w, const char *h) {
            return QByteArray("<office:document-styles><style:page-layout-properties fo:page-width=\"") + w
                + "\" fo:page-height=\"" + h + "\"/></office:document-styles>";
        };
        QVERIFY(near(predictOdf(st("21cm", "29.7cm")), 595.28, 841.89));
        QVERIFY(near(predictOdf(st("8.5in", "11in")), 612, 792));
        QVERIFY(near(predictOdf(st("210mm", "297mm")), 595.28, 841.89));
        QVERIFY(near(predictOdf(st("612pt", "792pt")), 612, 792));
        QVERIFY(predictOdf(st("1em", "2em")).isEmpty());
        // ODP: slides are the master page's layout, not the first (handout) layout
        const QByteArray odp = "<s xmlns:style=\"s\"><style:page-layout style:name=\"PM0\"><style:page-layout-properties fo:page-width=\"21cm\" fo:page-height=\"29.7cm\"/></style:page-layout>"
            "<style:page-layout style:name=\"PM1\"><style:page-layout-properties fo:page-width=\"28cm\" fo:page-height=\"15.75cm\"/></style:page-layout>"
            "<style:master-page style:name=\"M\" style:page-layout-name=\"PM1\"/></s>";
        QVERIFY(near(predictOdf(odp), 28 / 2.54 * 72, 15.75 / 2.54 * 72));
    }
    void pptx() { QVERIFY(near(predictPptx("<p:presentation xmlns:p=\"p\"><p:sldSz cx=\"12192000\" cy=\"6858000\"/></p:presentation>"), 960, 540)); }
    void paper()
    {
        QCOMPARE(excelPaper(1, false), QSizeF(612, 792));
        QCOMPARE(excelPaper(5, false), QSizeF(612, 1008));
        QVERIFY(near(excelPaper(8, false), 841.89, 1190.55));
        QVERIFY(near(excelPaper(9, false), 595.28, 841.89));
        QVERIFY(near(excelPaper(11, false), 419.53, 595.28));
        QCOMPARE(excelPaper(999, false), QSizeF(612, 792));
        QVERIFY(near(excelPaper(9, true), 841.89, 595.28));
    }
    void encryption()
    {
        QVERIFY(cfbHasEncryptedPackage(fx("enc.docx")));
        QVERIFY(cfbHasEncryptedPackage(fx("enc.xlsx")));
        QVERIFY(!cfbHasEncryptedPackage(fx("legacy.doc")));
        QVERIFY(!cfbHasEncryptedPackage(fx("a.odt")));
        QVERIFY(!cfbHasEncryptedPackage(fx("missing")));
        QVERIFY(odfEncrypted("<manifest:encryption-data/>"));
        QVERIFY(odfEncrypted("<x encrypted-package=\"1\"/>"));
        QVERIFY(!odfEncrypted("<manifest:file-entry/>"));
    }
    void probes()
    {
        for (const char *n : {"enc.docx", "enc.xlsx", "enc.odt"}) {
            const auto h = probe(fx(n), n, head(fx(n)));
            QVERIFY2(h.office && h.encrypted, n);
        }
        QCOMPARE(probe(fx("enc.docx"), "enc.docx", head(fx("enc.docx"))).family, Family::Cfb);
        auto h = probe(fx("a.odt"), "a.odt", head(fx("a.odt")));
        QVERIFY(h.office && !h.encrypted && h.predicted && h.family == Family::Odf);
        QVERIFY(near(h.firstPagePt, 595.28, 841.89));
        h = probe(fx("legacy.doc"), "legacy.doc", head(fx("legacy.doc")));
        QVERIFY(h.office && h.family == Family::Cfb && !h.predicted && !h.encrypted);
        QVERIFY(near(h.firstPagePt, 595.28, 841.89));
        h = probe(fx("legacy.doc"), "x.ppt", head(fx("legacy.doc")));
        QCOMPARE(h.firstPagePt, kSlide43);

        QTemporaryDir td;
        const QString p = td.filePath("m.docx");
        {
            KZip z(p);
            QVERIFY(z.open(QIODevice::WriteOnly));
            z.writeFile("word/document.xml", doc("<w:p/>"));   // no sectPr
            z.writeFile("[Content_Types].xml", "<Types/>");
        }
        h = probe(p, "m.docx", head(p));
        QVERIFY(h.office && h.family == Family::Ooxml && !h.predicted);
        {
            KZip z(p);
            QVERIFY(z.open(QIODevice::WriteOnly));
            z.writeFile("word/document.xml", doc(sect(12240, 15840)));
        }
        h = probe(p, "m.docx", head(p));
        QVERIFY(h.predicted && near(h.firstPagePt, 612, 792));

        const QString ep = td.filePath("b.epub");
        {
            KZip z(ep);
            QVERIFY(z.open(QIODevice::WriteOnly));
            z.setCompression(KZip::NoCompression);
            z.writeFile("mimetype", "application/epub+zip");
            z.writeFile("styles.xml", "<x/>");
        }
        QVERIFY(!probe(ep, "b.epub", head(ep)).office);

        const QString g = td.filePath("garbage.docx");
        QFile f(g); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("this is not an office file at all"); f.close();
        QVERIFY(!probe(g, "garbage.docx", head(g)).office);
        const QString r = td.filePath("a.rtf");
        QFile f2(r); QVERIFY(f2.open(QIODevice::WriteOnly)); f2.write("{\\rtf1 hi}"); f2.close();
        h = probe(r, "a.rtf", head(r));
        QVERIFY(h.office && h.family == Family::Rtf);
    }
};

QTEST_MAIN(TstOffice)
#include "tst_office.moc"
