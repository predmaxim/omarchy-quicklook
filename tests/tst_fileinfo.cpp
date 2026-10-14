#include <QtTest>
#include <QCryptographicHash>
#include <QDir>
#include <QImage>
#include <QMimeDatabase>
#include <QTemporaryDir>
#include "FileInfo.h"
#include "Thumbnails.h"

using namespace ql;

class TstFileInfo : public QObject
{
    Q_OBJECT
    QTemporaryDir dir;

    QString make(const QString &name, const QByteArray &data)
    {
        const QString p = dir.filePath(name);
        QFile f(p);
        if (!f.open(QIODevice::WriteOnly))
            qFatal("cannot create %s", qPrintable(p));
        f.write(data);
        return p;
    }
    Kind kindOf(const QString &name, const QByteArray &data) { return classify(infoForPath(make(name, data))); }
    QMimeType mime(const char *n) { return QMimeDatabase().mimeTypeForName(QString::fromLatin1(n)); }

    void putThumb(const QString &sizeDir, const QString &uri, const QString &mtime)
    {
        const QString p = thumbnailPath(uri, sizeDir);
        QDir().mkpath(QFileInfo(p).absolutePath());
        QImage img(sizeDir == "x-large" ? 8 : 4, 4, QImage::Format_RGB32);
        img.fill(Qt::red);
        img.setText("Thumb::MTime", mtime);
        QVERIFY(img.save(p, "PNG"));
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(dir.isValid());
        qputenv("GIO_USE_VFS", "local"); // never reach gvfs / the session bus
        QVERIFY2(!qEnvironmentVariableIsEmpty("XDG_CACHE_HOME"), "run via ctest: XDG_CACHE_HOME must be a test dir");
    }

    void resolveTrickyNames_data()
    {
        QTest::addColumn<QString>("name");
        for (const char *n : {"a b.txt", "hash#1.txt", "pct%20.txt", "q?.txt"})
            QTest::newRow(n) << QString::fromUtf8(n);
        QTest::newRow("cyrillic") << QString::fromUtf8("кириллица.txt");
    }
    void resolveTrickyNames()
    {
        QFETCH(QString, name);
        const QString p = make(name, "hello world");
        const FileInfo i = resolveFile(QUrl::fromLocalFile(p).toString(QUrl::FullyEncoded));
        QCOMPARE(i.path, p);
        QVERIFY(i.exists);
        QVERIFY(!i.isDir);
        QCOMPARE(i.size, 11);
        QCOMPARE(i.head, QByteArray("hello world"));
        QCOMPARE(i.displayName, name);
        QVERIFY(i.modified.isValid());
        QVERIFY(i.canonicalUri.startsWith("file:///"));
        QCOMPARE(classify(i), Kind::Text);
    }
    void plainPathWithoutScheme()
    {
        const QString p = make("plain.txt", "x");
        QCOMPARE(resolveFile(p).path, p);
    }
    void directoryAndMissing()
    {
        const FileInfo d = infoForPath(dir.path());
        QVERIFY(d.isDir);
        QCOMPARE(d.mimeName, QString("inode/directory"));
        QCOMPARE(classify(d), Kind::FolderCard);
        const FileInfo m = infoForPath(dir.filePath("missing.txt"));
        QVERIFY(!m.exists);
        QCOMPARE(m.displayName, QString("missing.txt"));
        QCOMPARE(classify(m), Kind::FileCard);
        QVERIFY(!resolveFile(QString()).exists);
    }
    void classifyChain()
    {
        QCOMPARE(kindOf("empty.txt", ""), Kind::FileCard);
        QCOMPARE(kindOf("x.pdf", "%PDF-1.4\n1 0 obj\n"), Kind::Pdf);
        QCOMPARE(kindOf("x.docx", QByteArray("PK\x03\x04rest", 8)), Kind::Office);
        QCOMPARE(kindOf("y.docx", "hello"), Kind::Text);
        QCOMPARE(kindOf("x.png", "%!PS-Adobe-3.0 EPSF-3.0\n"), Kind::Image);
        QCOMPARE(kindOf("script", "#!/bin/sh\necho"), Kind::Text);
        QCOMPARE(kindOf("blob", QByteArray("ab\0cd\1\2\3", 8)), Kind::FileCard);
        QCOMPARE(kindOf("u16.txt", QByteArray("\xff\xfeh\0i\0", 6)), Kind::Text);
        QCOMPARE(kindOf("x.svg", "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"4\" height=\"4\"/>"), Kind::Svg);
        QCOMPARE(kindOf("x.mp4", QByteArray("\0\0\0\x18" "ftypmp42\0\0\0\0mp42isom", 24)), Kind::Video);
        QCOMPARE(kindOf("x.mp3", "ID3\x03\0\0\0\0\0\0"), Kind::Audio);
    }
    void looksLikeTextCases()
    {
        QVERIFY(looksLikeText(mime("text/plain"), "abc"));
        QVERIFY(!looksLikeText(mime("text/plain"), QByteArray("a\0b", 3)));
        QVERIFY(looksLikeText(mime("application/octet-stream"), "plain bytes"));
        QVERIFY(!looksLikeText(mime("application/zip"), "plain bytes"));
        QVERIFY(looksLikeText(mime("application/json"), "{}"));  // inherits text/plain
        QVERIFY(looksLikeText(mime("application/zip"), QByteArray("\xef\xbb\xbfhi", 5))); // BOM wins
        QVERIFY(!looksLikeText(mime("application/octet-stream"), QByteArray("\0\0", 2)));
    }
    void officeCases()
    {
        QVERIFY(hasOfficeSignature(QByteArray("PK\x03\x04", 4)));
        QVERIFY(hasOfficeSignature(QByteArray("\xd0\xcf\x11\xe0", 4)));
        QVERIFY(hasOfficeSignature("{\\rtf1"));
        QVERIFY(!hasOfficeSignature("hello"));
        QVERIFY(!hasOfficeSignature(QByteArray()));
        QMimeType none;
        for (const char *n : {"a.DOCX", "a.xlsx", "a.pptx", "a.odt", "a.ods", "a.odp", "a.doc", "a.xls", "a.ppt", "a.rtf"})
            QVERIFY2(isOfficeType(n, none), n);
        QVERIFY(!isOfficeType("a.txt", mime("text/plain")));
        QVERIFY(isOfficeType("noext", mime("application/vnd.oasis.opendocument.text")));
        QVERIFY(isOfficeType("noext", mime("application/rtf")));
    }

    void thumbnails()
    {
        const QString uri = "file:///tmp/ql-thumb-test/a%20b.txt";
        const QDateTime mt = QDateTime::fromSecsSinceEpoch(1700000000);
        QCOMPARE(thumbnailPath(uri, "large"),
                 qEnvironmentVariable("XDG_CACHE_HOME") + "/thumbnails/large/"
                     + QString::fromLatin1(QCryptographicHash::hash(uri.toUtf8(), QCryptographicHash::Md5).toHex()) + ".png");
        QVERIFY(loadThumbnail(uri, mt).isNull());
        putThumb("large", uri, "1700000001");
        QVERIFY(loadThumbnail(uri, mt).isNull()); // wrong MTime
        putThumb("large", uri, "1700000000");
        QCOMPARE(loadThumbnail(uri, mt).width(), 4);
        putThumb("x-large", uri, "1700000000");
        QCOMPARE(loadThumbnail(uri, mt).width(), 8); // x-large preferred
        putThumb("x-large", uri, "5");
        QCOMPARE(loadThumbnail(uri, mt).width(), 4); // falls back past a stale one
    }
    void cleanupTestCase()
    {
        const QString uri = "file:///tmp/ql-thumb-test/a%20b.txt";
        for (const char *d : {"x-large", "large", "normal"})
            QFile::remove(thumbnailPath(uri, d));
    }
};

QTEST_GUILESS_MAIN(TstFileInfo)
#include "tst_fileinfo.moc"
