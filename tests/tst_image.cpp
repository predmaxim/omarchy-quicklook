#include "FrameAnimator.h"
#include "Image.h"
#include "ImageStore.h"
#include "Pools.h"
#include <QBuffer>
#include <QElapsedTimer>
#include <QFile>
#include <QImageReader>
#include <QPainter>
#include <QPdfWriter>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <fcntl.h>
#include <unistd.h>

using namespace ql;

namespace {
quint32 crc32(const QByteArray &d)
{
    quint32 c = 0xffffffffu;
    for (uchar b : d) {
        c ^= b;
        for (int k = 0; k < 8; ++k)
            c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
    }
    return ~c;
}
QByteArray be32(quint32 v)
{
    QByteArray a(4, 0);
    for (int i = 0; i < 4; ++i)
        a[i] = char(v >> (24 - 8 * i));
    return a;
}
QByteArray chunk(const char *type, const QByteArray &data)
{
    const QByteArray td = QByteArray(type) + data;
    return be32(quint32(data.size())) + td + be32(crc32(td));
}
void write(const QString &p, const QByteArray &d)
{
    QFile f(p);
    QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(p));
    f.write(d);
}
QByteArray read(const QString &p)
{
    QFile f(p);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
struct StderrMute {
    int saved = dup(2);
    StderrMute() { if (const int n = open("/dev/null", O_WRONLY); n >= 0) { dup2(n, 2); close(n); } }
    ~StderrMute() { dup2(saved, 2); close(saved); }
};
const QByteArray kEps = "%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 10 10\n0 0 moveto 10 10 lineto stroke\nshowpage\n";
} // namespace

class TstImage : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;
    QString p(const QString &n) const { return m_dir.filePath(n); }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QImage big(4000, 3000, QImage::Format_RGB32);
        big.fill(Qt::blue);
        QVERIFY(big.save(p("big.jpg"), "JPEG"));
        QImage small(64, 48, QImage::Format_ARGB32);
        small.fill(QColor(255, 0, 0, 128));
        QVERIFY(small.save(p("small.png")));

        // rot.jpg: 400x300 with EXIF Orientation 6, EXIF segment inserted by hand
        QImage rot(400, 300, QImage::Format_RGB32);
        rot.fill(Qt::red);
        QBuffer buf;
        buf.open(QIODevice::WriteOnly);
        QVERIFY(rot.save(&buf, "JPEG"));
        QByteArray exif("Exif\0\0", 6);
        exif += QByteArray("MM\0*\0\0\0\x08", 8) + QByteArray("\0\x01", 2)
            + QByteArray("\x01\x12\0\x03\0\0\0\x01\0\x06\0\0", 12) + QByteArray(4, 0);
        const QByteArray seg = QByteArray("\xff\xe1", 2) + QByteArray(1, char((exif.size() + 2) >> 8))
            + QByteArray(1, char((exif.size() + 2) & 0xff)) + exif;
        write(p("rot.jpg"), buf.data().left(2) + seg + buf.data().mid(2));

        const QByteArray bigData = read(p("big.jpg"));
        write(p("trunc.jpg"), bigData.left(bigData.size() / 2));

        write(p("fake.png"), kEps);
        {
            QFile pdf(p("doc.png"));
            QVERIFY(pdf.open(QIODevice::WriteOnly));
            QPdfWriter w(&pdf);
            QPainter pt(&w);
            pt.drawText(100, 100, QStringLiteral("hi"));
        }
        // IHDR 12000x12000, no pixel data
        const QByteArray ihdr = be32(12000) + be32(12000) + QByteArray("\x08\x02\0\0\0", 5);
        write(p("huge.png"), QByteArray("\x89PNG\r\n\x1a\n", 8) + chunk("IHDR", ihdr) + chunk("IDAT", {}) + chunk("IEND", {}));

        write(p("a.svg"), "<svg xmlns='http://www.w3.org/2000/svg' width='200' height='100'><rect width='200' height='100' fill='red'/></svg>");
        write(p("b.svg"), "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 50 25'><rect width='50' height='25' fill='red'/></svg>");

        QDir(m_dir.path()).mkdir("bin");
        // writes a valid 1x1 ppm to -sOutputFile so the control read ends without library noise
        write(p("bin/gs"), "#!/bin/sh\nfor a in \"$@\"; do case \"$a\" in -sOutputFile=*) printf 'P6\\n1 1\\n255\\n\\0\\0\\0' > \"${a#-sOutputFile=}\";; esac; done\ntouch '" + p("gs-ran").toUtf8() + "'\n");
        QFile::setPermissions(p("bin/gs"), QFile::permissions(p("bin/gs")) | QFileDevice::ExeOwner);
        qputenv("PATH", (p("bin") + ":" + qEnvironmentVariable("PATH")).toUtf8());

        if (QStandardPaths::findExecutable(QStringLiteral("magick")).isEmpty())
            return;
        QProcess::execute(QStringLiteral("magick"), {"-delay", "10", "-size", "32x32", "xc:red", "xc:blue", "xc:green", p("anim.gif")});
        QProcess::execute(QStringLiteral("magick"), {"-size", "64x48", "xc:green", p("x.heic")});

    }

    void probeAndDecodeJpeg()
    {
        const auto h = image::probe(p("big.jpg"));
        QVERIFY(h.ok);
        QCOMPARE(h.displayPx, QSize(4000, 3000));
        QVERIFY(h.nativeScaled);
        QVERIFY(!h.rotate90);
        QVERIFY(!image::tooLarge(h));
        const QImage img = image::decode(p("big.jpg"), h, QSize(800, 600));
        QVERIFY(!img.isNull());
        QVERIFY(img.width() <= 800 && img.height() <= 600);
        QCOMPARE(img.format(), QImage::Format_RGB32);
    }
    void smallPngKeepsAlpha()
    {
        const auto h = image::probe(p("small.png"));
        QVERIFY(h.ok && !h.nativeScaled && !h.animated);
        const QImage img = image::decode(p("small.png"), h, QSize(800, 600));
        QCOMPARE(img.format(), QImage::Format_ARGB32_Premultiplied);
    }
    void exifRotation()
    {
        const auto h = image::probe(p("rot.jpg"));
        QVERIFY(h.ok);
        QCOMPARE(h.displayPx, QSize(300, 400));
        QVERIFY(h.rotate90);
        const QImage img = image::decode(p("rot.jpg"), h, QSize(200, 200));
        QVERIFY(!img.isNull());
        QVERIFY(img.width() < img.height());
        QVERIFY(img.width() <= 200 && img.height() <= 200);
    }
    void animatedGif()
    {
        if (!QFile::exists(p("anim.gif")))
            QSKIP("magick missing");
        QVERIFY(image::probe(p("anim.gif")).animated);
    }
    void heic()
    {
        if (!QFile::exists(p("x.heic")))
            QSKIP("magick cannot write heic");
        QCOMPARE(image::heifSize(p("x.heic")), QSize(64, 48));
        const auto h = image::probe(p("x.heic"));
        if (!h.ok)
            QSKIP("no kimageformats heif plugin");
        QCOMPARE(h.displayPx, QSize(64, 48));
        QVERIFY(!h.rotate90);
        // 64x48 from magick is a 64x64 crop that kimg_heif mis-reads as 48x64; use an uncropped file for decode
        QProcess::execute(QStringLiteral("magick"), {"-size", "64x64", "xc:green", p("sq.heic")});
        const auto sq = image::probe(p("sq.heic"));
        QVERIFY(sq.ok);
        QCOMPARE(image::decode(p("sq.heic"), sq, QSize(32, 32)).size(), QSize(32, 32));
        QCOMPARE(image::decode(p("sq.heic"), sq, QSize(800, 600)).size(), QSize(64, 64));   // never enlarged
    }
    void epsNeverRunsGhostscript()
    {
        QFile::remove(p("gs-ran"));
        const auto h = image::probe(p("fake.png"));
        QVERIFY(!h.ok);
        QVERIFY(image::decode(p("fake.png"), h, QSize(100, 100)).isNull());
        image::Header forced;
        forced.ok = true;
        QVERIFY(image::decode(p("fake.png"), forced, QSize(100, 100)).isNull());
        QVERIFY(!QFile::exists(p("gs-ran")));
        // control: a plain reader does reach the stub
        QImageReader ctl(p("fake.png"));
        ctl.read();
        if (!QFile::exists(p("gs-ran")))
            QSKIP("eps plugin absent: guard untested against the control");
    }
    void pdfAsPng()
    {
        QVERIFY(!image::probe(p("doc.png")).ok);
    }
    void truncatedJpeg()
    {
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Corrupt JPEG data")));
        const auto h = image::probe(p("trunc.jpg"));
        QVERIFY(h.ok);
        QElapsedTimer t;
        t.start();
        const QImage img = image::decode(p("trunc.jpg"), h, QSize(800, 600));
        QVERIFY(t.elapsed() < 2000);
        if (!img.isNull())
            QVERIFY(img.width() <= 800 && img.height() <= 600);
    }
    void tooLarge()
    {
        const auto h = image::probe(p("huge.png"));
        QVERIFY(h.ok);
        QCOMPARE(h.displayPx, QSize(12000, 12000));
        QVERIFY(image::tooLarge(h));
    }
    void stalenessSkipsWork()
    {
        pools::currentGeneration().store(5);
        const image::Header h = image::probe(p("big.jpg"));
        QVERIFY(h.ok);
        // stale generation -> null even for a real file; a stale call on a nonexistent path is null too
        QVERIFY(image::decodeIfCurrent(4, p("big.jpg"), h, QSize(800, 600)).isNull());
        QVERIFY(image::decodeIfCurrent(4, p("nope.jpg"), h, QSize(800, 600)).isNull());
        QVERIFY(!image::decodeIfCurrent(5, p("big.jpg"), h, QSize(800, 600)).isNull());
    }
    void svg()
    {
        QCOMPARE(image::svgSize(p("a.svg")), QSizeF(200, 100));
        QCOMPARE(image::svgSize(p("b.svg")), QSizeF(50, 25));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Cannot open file")));
        QVERIFY(image::svgSize(p("nope.svg")).isEmpty());
        const QImage img = image::renderSvg(p("a.svg"), QSize(400, 300));
        QCOMPARE(img.size(), QSize(400, 300));
        QCOMPARE(img.format(), QImage::Format_ARGB32_Premultiplied);
        QCOMPARE(img.pixelColor(200, 150), QColor(Qt::red));
        QCOMPARE(img.pixelColor(200, 5).alpha(), 0);   // letterbox stays transparent
    }
    void cover()
    {
        QBuffer b;
        b.open(QIODevice::WriteOnly);
        QImage(64, 48, QImage::Format_RGB32).save(&b, "PNG");
        QVERIFY(!image::decodeCover(b.data(), "image/png", QSize(32, 32)).isNull());
        QVERIFY(image::decodeCover(b.data(), "image/gif", QSize(32, 32)).isNull());
        const StderrMute mute;   // libpng prints "Not a PNG file" straight to stderr
        QVERIFY(image::decodeCover(kEps, "image/png", QSize(32, 32)).isNull());
    }
    void store()
    {
        ImageStore s;
        QImage a(10, 5, QImage::Format_RGB32);
        a.fill(Qt::green);
        const QString url = s.put(3, "main", a);
        QVERIFY(url.startsWith("image://ql/3/main/"));
        const QString id = url.mid(QString("image://ql/").size());
        QSize sz;
        const QImage r1 = s.requestImage(id, &sz, {});
        QCOMPARE(sz, QSize(10, 5));
        const QImage r2 = s.requestImage(id, &sz, {});
        QCOMPARE(r1.cacheKey(), r2.cacheKey());
        QCOMPARE(r1.cacheKey(), a.cacheKey());
        QVERIFY(s.requestImage("9/none/1", &sz, {}).isNull());
        s.put(4, "main", a);
        QCOMPARE(s.count(), 2);
        s.dropOlderThan(4);
        QCOMPARE(s.count(), 1);
        QVERIFY(s.requestImage(id, &sz, {}).isNull());
    }
    void animator()
    {
        if (!QFile::exists(p("anim.gif")))
            QSKIP("magick missing");
        ImageStore s;
        FrameAnimator a(&s);
        QSignalSpy spy(&a, &FrameAnimator::frameReady);
        a.start(1, p("anim.gif"), QSize(64, 64));
        QVERIFY(a.running());
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 2, 2000);
        QVERIFY(spy.first().first().toString().startsWith("image://ql/1/frame/"));
        QVERIFY(s.count() <= 2);   // frames replace each other
        a.stop();
        QVERIFY(!a.running());
        const int n = spy.count();
        QTest::qWait(300);
        QCOMPARE(spy.count(), n);
    }
};

QTEST_MAIN(TstImage)
#include "tst_image.moc"
