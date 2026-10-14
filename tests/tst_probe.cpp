#include "Geometry.h"
#include "OfficeCache.h"
#include "Probe.h"
#include "Thumbnails.h"
#include <KZip>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QPdfWriter>
#include <QPainter>
#include <QProcess>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

using namespace ql;

class TstProbe : public QObject {
    Q_OBJECT
    QTemporaryDir dir;

    QString p(const char *n) const { return dir.filePath(QString::fromLatin1(n)); }
    static void put(const QString &path, const QByteArray &data)
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(data);
    }
    static ProbeRequest req(const QString &path)
    {
        ProbeRequest r;
        r.uri = QUrl::fromLocalFile(path).toString();
        r.fallbackMonitor = Monitor{QStringLiteral("T"), QSize(2560, 1440), 1.0};
        return r;
    }
    QString makeDocx(const char *name)
    {
        const QString path = p(name);
        KZip z(path);
        if (!z.open(QIODevice::WriteOnly))
            return {};
        z.writeFile(QStringLiteral("[Content_Types].xml"), QByteArray("<Types/>"));
        z.writeFile(QStringLiteral("word/document.xml"),
                    QByteArray("<w:document xmlns:w='x'><w:body><w:sectPr><w:pgSz w:w='11906' w:h='16838'/></w:sectPr></w:body></w:document>"));
        z.close();
        return path;
    }
    static void a4Pdf(const QString &path)
    {
        QPdfWriter w(path);
        w.setPageSize(QPageSize(QPageSize::A4));
        QPainter pa(&w);
        pa.drawText(100, 100, QStringLiteral("hi"));
    }

private Q_SLOTS:
    void initTestCase()
    {
        qputenv("GIO_USE_VFS", "local");   // gvfs would join the user's session bus on the first GIO call
        qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
        QImageReader::setAllocationLimit(1024);   // as main() does
        QVERIFY(dir.isValid());
    }

    void imageWithThumbnail()
    {
        const QString f = p("a.png");
        QImage img(1920, 1080, QImage::Format_RGB32);
        img.fill(Qt::red);
        QVERIFY(img.save(f));
        ProbeResult none = runProbe(req(f));
        QCOMPARE(none.kind, Kind::Image);
        QCOMPARE(none.layout.window, geometry::imageLayout(QSize(1920, 1080), none.monitor).window);
        QVERIFY(none.layout.window == QSize(1920, 1080));
        QVERIFY(none.placeholder.isNull());
        QVERIFY(none.detailed);

        QImage th(128, 72, QImage::Format_RGB32);
        th.fill(Qt::blue);
        th.setText(QStringLiteral("Thumb::MTime"), QString::number(none.file.modified.toSecsSinceEpoch()));
        const QString tp = thumbnailPath(none.file.canonicalUri, QStringLiteral("large"));
        QVERIFY(QDir().mkpath(QFileInfo(tp).path()));
        QVERIFY(th.save(tp, "PNG"));
        const ProbeResult withTh = runProbe(req(f));
        QVERIFY(!withTh.placeholder.isNull());
        const QSize lim = (withTh.layout.content.size() * withTh.monitor.scale).toSize();
        QVERIFY(withTh.placeholder.width() <= lim.width() && withTh.placeholder.height() <= lim.height());
        QCOMPARE(withTh.placeholder.size(), QSize(128, 72));   // smaller than content: not enlarged
        // a large thumbnail is shrunk into the content rect
        QImage big(4000, 2250, QImage::Format_RGB32);
        big.fill(Qt::green);
        big.setText(QStringLiteral("Thumb::MTime"), QString::number(none.file.modified.toSecsSinceEpoch()));
        QVERIFY(big.save(tp, "PNG"));
        const ProbeResult shrunk = runProbe(req(f));
        QVERIFY(shrunk.placeholder.width() <= lim.width() && shrunk.placeholder.height() <= lim.height());
        QVERIFY(shrunk.placeholder.width() < 4000);
    }
    void vanished()
    {
        const QString f = p("gone.txt");
        put(f, "x");
        const ProbeRequest r = req(f);
        QFile::remove(f);
        const ProbeResult res = runProbe(r);
        QCOMPARE(res.kind, Kind::FileCard);
        QCOMPARE(res.reason, CardReason::CannotOpen);
        QCOMPARE(res.layout.window, QSize(440, 180));
    }
    void textGoneBetweenStages()
    {
        const QString f = p("race.txt");
        put(f, "abc\n");
        ProbeResult r = classifyStage(req(f));
        QCOMPARE(r.kind, Kind::Text);
        QVERIFY(!r.detailed);
        QFile::remove(f);
        detailStage(r);
        QCOMPARE(r.kind, Kind::FileCard);
        QCOMPARE(r.reason, CardReason::CannotOpen);
    }
    void brokenImage()
    {
        const QString f = p("fake.png");
        put(f, "%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 10 10\n");
        const ProbeResult r = runProbe(req(f));
        QCOMPARE(r.kind, Kind::FileCard);
        QCOMPARE(r.reason, CardReason::CannotOpen);
        const QString g = p("trunc.png");
        put(g, QByteArray("\x89PNG\r\n\x1a\n", 8) + "junk");
        const ProbeResult t = runProbe(req(g));
        QCOMPARE(t.kind, Kind::FileCard);
        QCOMPARE(t.reason, CardReason::CannotOpen);
    }
    static QByteArray pngChunk(const QByteArray &type, const QByteArray &data)
    {
        quint32 c = 0xffffffffu;
        for (uchar b : type + data) {
            c ^= b;
            for (int k = 0; k < 8; ++k)
                c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
        }
        QByteArray out;
        QDataStream ds(&out, QIODevice::WriteOnly);   // big endian
        ds << quint32(data.size());
        out += type + data;
        ds.device()->seek(out.size());
        ds << quint32(~c);
        return out;
    }
    void tooLarge()
    {
        QByteArray ihdr;
        QDataStream ds(&ihdr, QIODevice::WriteOnly);
        ds << quint32(12000) << quint32(12000);
        ihdr += QByteArray("\x08\x02\x00\x00\x00", 5);
        const QString f = p("huge.png");
        put(f, QByteArray("\x89PNG\r\n\x1a\n", 8) + pngChunk("IHDR", ihdr) + pngChunk("IDAT", {}) + pngChunk("IEND", {}));
        const ProbeResult r = runProbe(req(f));
        QCOMPARE(r.kind, Kind::FileCard);
        QCOMPARE(r.reason, CardReason::TooLarge);
    }
    void svg()
    {
        const QString f = p("a.svg");
        put(f, "<svg xmlns='http://www.w3.org/2000/svg' width='400' height='300'><rect width='400' height='300'/></svg>");
        const ProbeResult r = runProbe(req(f));
        QCOMPARE(r.kind, Kind::Svg);
        QCOMPARE(r.layout.window, geometry::svgLayout(QSizeF(400, 300), r.monitor).window);
        const QString g = p("bad.svg");
        put(g, "<svg xmlns='http://www.w3.org/2000/svg'><rect");
        const ProbeResult b = runProbe(req(g));
        QCOMPARE(b.kind, Kind::FileCard);
        QCOMPARE(b.reason, CardReason::CannotOpen);
    }
    void pdf()
    {
        const QString f = p("a.pdf");
        a4Pdf(f);
        const ProbeResult r = runProbe(req(f));
        QCOMPARE(r.kind, Kind::Pdf);
        QCOMPARE(r.layout.window, geometry::pageLayout(r.pdf.firstPagePt, r.monitor).window);
        QVERIFY(r.layout.window.isValid());
    }
    void lockedPdf()
    {
        const QString f = p("a.pdf");
        a4Pdf(f);
        const QString l = p("locked.pdf");
        if (QProcess::execute(QStringLiteral("qpdf"), {QStringLiteral("--encrypt"), QStringLiteral("u"), QStringLiteral("o"),
                                                       QStringLiteral("256"), QStringLiteral("--"), f, l}) != 0)
            QSKIP("qpdf missing");
        const ProbeResult r = runProbe(req(l));
        QCOMPARE(r.kind, Kind::FileCard);
        QCOMPARE(r.reason, CardReason::PasswordProtected);
    }
    void text()
    {
        const QString f = p("a.txt");
        put(f, "one\ntwo\nthree\n");
        const ProbeResult r = runProbe(req(f));
        QCOMPARE(r.kind, Kind::Text);
        QVERIFY(r.highlight);
        QVERIFY(r.layout.window.height() >= geometry::kMinSide);
        QCOMPARE(r.layout.window.height(), geometry::textLayout(r.textMeasure.columns, r.textMeasure.rows,
                                                                 r.textMeasure.advance, r.textMeasure.rowHeight, r.monitor).window.height());
        const QString g = p("long.txt");
        put(g, QByteArray(3000, 'x') + "\n");
        QVERIFY(!runProbe(req(g)).highlight);
        const QString h = p("tall.txt");
        QByteArray many;
        for (int i = 0; i < 5000; ++i)
            many += "line\n";
        put(h, many);
        const ProbeResult t = runProbe(req(h));
        const int boxH = geometry::box(t.monitor).height();   // rows are capped so that rows*rowHeight + 32 fits
        QVERIFY(t.layout.window.height() <= boxH && t.layout.window.height() > boxH - t.textMeasure.rowHeight);
    }
    void office()
    {
        const QString f = makeDocx("a.docx");
        QVERIFY(!f.isEmpty());
        ProbeResult r = runProbe(req(f));
        QCOMPARE(r.kind, Kind::Office);
        QVERIFY(r.officeCachedPdf.isEmpty());
        QCOMPARE(r.layout.window, geometry::pageLayout(office::kA4, r.monitor).window);

        const QString job = p("job.pdf");
        a4Pdf(job);
        QVERIFY(!OfficeCache().store(f, job).isEmpty());
        r = runProbe(req(f));
        QCOMPARE(r.kind, Kind::Pdf);
        QVERIFY(!r.officeCachedPdf.isEmpty());
        QCOMPARE(fallbackFor(r).window, geometry::fallbackLayout(office::kA4, r.monitor).window);
    }
    void audio()
    {
        const QString f = p("a.mp3");
        if (QProcess::execute(QStringLiteral("ffmpeg"), {QStringLiteral("-v"), QStringLiteral("quiet"), QStringLiteral("-f"),
                QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("anullsrc=r=44100:cl=mono"), QStringLiteral("-t"),
                QStringLiteral("1"), QStringLiteral("-metadata"), QStringLiteral("title=Song"), QStringLiteral("-y"), f}) != 0)
            QSKIP("ffmpeg missing");
        const ProbeResult r = runProbe(req(f));
        QCOMPARE(r.kind, Kind::Audio);
        QCOMPARE(r.layout.window, QSize(520, 180));
        QCOMPARE(r.audio.title, QStringLiteral("Song"));
    }
    void folder()
    {
        const ProbeResult r = runProbe(req(dir.path()));
        QCOMPARE(r.kind, Kind::FolderCard);
        QCOMPARE(r.layout.window, QSize(440, 180));
    }
    void fallbacks()
    {
        ProbeResult r;
        r.monitor = Monitor{QStringLiteral("T"), QSize(2560, 1440), 1.0};
        r.kind = Kind::Svg;
        QCOMPARE(fallbackFor(r).window, geometry::fallbackLayout(QSizeF(4, 3), r.monitor).window);
        r.kind = Kind::Pdf;
        r.file.displayName = QStringLiteral("x.pdf");
        QCOMPARE(fallbackFor(r).window, geometry::fallbackLayout(office::kA4, r.monitor).window);
        r.kind = Kind::Office;
        r.file.displayName = QStringLiteral("x.PPTX");
        QCOMPARE(fallbackFor(r).window, geometry::fallbackLayout(QSizeF(4, 3), r.monitor).window);
        r.kind = Kind::Video;
        QCOMPARE(fallbackFor(r).window, geometry::fallbackLayout(QSizeF(16, 9), r.monitor).window);
        r.kind = Kind::Text;
        r.layout = geometry::cardLayout(Kind::Audio);
        QCOMPARE(fallbackFor(r).window, QSize(520, 180));
    }
};

QTEST_MAIN(TstProbe)
#include "tst_probe.moc"
