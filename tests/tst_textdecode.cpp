#include <QtTest>
#include <QStringEncoder>
#include <QTemporaryDir>
#include "TextDecode.h"

using namespace ql::text;

namespace {
const QString kRu = QStringLiteral("Привет, мир! Это проверка определения кодировки текста в быстром просмотре файлов. "
                                   "Съешь же ещё этих мягких французских булок, да выпей чаю.\n");
const QString kKo = QStringLiteral("안녕하세요. 이것은 인코딩 감지 테스트입니다. 한국어 텍스트가 올바르게 표시되어야 합니다.\n");

QByteArray enc(const char *name, const QString &s)
{
    QStringEncoder e(name);
    return QByteArray(e.encode(s));
}
QByteArray rep(const QByteArray &b, int n)
{
    QByteArray r;
    for (int i = 0; i < n; ++i)
        r += b;
    return r;
}
} // namespace

class TstTextDecode : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void detect_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<QByteArray>("expected");
        const QByteArray bom8("\xEF\xBB\xBF");
        QTest::newRow("utf8-ru") << kRu.toUtf8() << QByteArray("UTF-8");
        QTest::newRow("utf8-bom") << bom8 + kRu.toUtf8() << QByteArray("UTF-8");
        QTest::newRow("ascii") << QByteArray("plain ascii text\nsecond line\n") << QByteArray("UTF-8");
        QTest::newRow("utf16le-bom") << QByteArray("\xFF\xFE") + enc("UTF-16LE", kRu) << QByteArray("UTF-16LE");
        QTest::newRow("utf16be-bom") << QByteArray("\xFE\xFF") + enc("UTF-16BE", kRu) << QByteArray("UTF-16BE");
        QTest::newRow("cp1251") << enc("windows-1251", kRu) << QByteArray("windows-1251");
        QTest::newRow("koi8r") << enc("KOI8-R", kRu + kRu) << QByteArray("KOI8-R");
        QTest::newRow("ibm866") << enc("IBM866", kRu + kRu) << QByteArray("IBM866");
        QTest::newRow("euckr") << enc("EUC-KR", kKo + kKo) << QByteArray("windows-949");
        QTest::newRow("latin1") << enc("ISO-8859-1", QStringLiteral("Größe und Maße: schöne Grüße aus Düsseldorf, äöüß ÄÖÜ. Café, naïve, façade, résumé.\n")) << QByteArray("ISO-8859-1");
    }
    void detect()
    {
        QFETCH(QByteArray, bytes);
        QFETCH(QByteArray, expected);
        const QByteArray got = detectEncoding(bytes);
        if (expected == "ISO-8859-1") // short Latin text: uchardet may pick a near relative
            QVERIFY2(got == "windows-1252" || got.startsWith("ISO-8859-") || got.startsWith("WINDOWS-125"), got.constData());
        else
            QCOMPARE(got.toLower(), expected.toLower());
    }

    void decodeRoundTrip_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<QString>("text");
        QTest::newRow("utf8") << kRu.toUtf8() << kRu;
        QTest::newRow("utf8-bom-stripped") << QByteArray("\xEF\xBB\xBF") + kRu.toUtf8() << kRu;
        QTest::newRow("utf16le") << QByteArray("\xFF\xFE") + enc("UTF-16LE", kRu) << kRu;
        QTest::newRow("utf16be") << QByteArray("\xFE\xFF") + enc("UTF-16BE", kRu) << kRu;
        QTest::newRow("cp1251") << enc("windows-1251", kRu) << kRu;
        QTest::newRow("cp1251-short") << enc("windows-1251", QStringLiteral("Привет, как дела у вас?")) << QStringLiteral("Привет, как дела у вас?");
        QTest::newRow("koi8r") << enc("KOI8-R", kRu + kRu) << kRu + kRu;
        QTest::newRow("ibm866") << enc("IBM866", kRu + kRu) << kRu + kRu;
        QTest::newRow("euckr") << enc("EUC-KR", kKo + kKo) << kKo + kKo;
        QTest::newRow("ascii") << QByteArray("hello\n") << QStringLiteral("hello\n");
    }
    void decodeRoundTrip()
    {
        QFETCH(QByteArray, bytes);
        QFETCH(QString, text);
        const Decoded d = decode(bytes, bytes.size());
        QCOMPARE(d.text, text);
        QVERIFY(!d.truncated);
        QCOMPARE(d.shownBytes, bytes.size());
        QCOMPARE(d.totalBytes, bytes.size());
    }

    void cp1251ShortLine()
    {
        const QByteArray b = enc("windows-1251", QStringLiteral("Привет, как дела у вас?"));
        QCOMPARE(b.size(), 23);
        QCOMPARE(decode(b, 23).text, QStringLiteral("Привет, как дела у вас?"));
    }
    void emptyInput()
    {
        const Decoded d = decode({}, 0);
        QVERIFY(d.text.isEmpty());
        QVERIFY(!d.truncated);
    }
    void asciiHeadThenLegacyText()
    {
        const QByteArray b = QByteArray(20000, 'a') + enc("windows-1251", kRu);
        const Decoded d = decode(b, b.size());
        QVERIFY(!d.text.contains(QChar(0xFFFD)));
        QVERIFY(d.text.endsWith(kRu));
    }
    void multibyteSplitByWindow()
    {
        const QByteArray b = QByteArray(kDetectBytes - 1, 'a') + QStringLiteral("Привет\n").toUtf8();
        QCOMPARE(detectEncoding(b), QByteArray("UTF-8"));
    }
    void binaryAlwaysUsableEncoding()
    {
        QByteArray b(4096, '\0');
        b[10] = char(0xFF);
        b[11] = char(0xFE);
        const Decoded d = decode(b, b.size());
        QVERIFY(QStringDecoder(d.encoding.constData()).isValid());
    }

    void mapNames()
    {
        QCOMPARE(mapCharsetName("UHC"), QByteArray("windows-949"));
        QCOMPARE(mapCharsetName("ISO-8859-16"), QByteArray("windows-1252"));
        QCOMPARE(mapCharsetName("VISCII"), QByteArray("windows-1252"));
        QCOMPARE(mapCharsetName("X-ISO-10646-UCS-4-3412"), QByteArray("windows-1252"));
        QCOMPARE(mapCharsetName("WINDOWS-1251"), QByteArray("WINDOWS-1251"));
    }

    void truncatesAtLastNewline()
    {
        const QByteArray line = QByteArray(79, 'x') + '\n';
        const QByteArray big = rep(line, 3 * 1024 * 1024 / 80);
        const Decoded d = decode(big, big.size());
        QVERIFY(d.truncated);
        QVERIFY(d.shownBytes < kMaxBytes);
        QCOMPARE(d.shownBytes % 80, 0);
        QVERIFY(d.text.endsWith(QLatin1Char('\n')));
        QCOMPARE(d.totalBytes, big.size());
    }
    void truncatedWithoutNewlineKeepsAll()
    {
        const QByteArray big(3 * 1024 * 1024, 'x');
        const Decoded d = decode(big, big.size());
        QVERIFY(d.truncated);
        QCOMPARE(d.shownBytes, kMaxBytes);
    }

    void readFileCases()
    {
        QTemporaryDir dir;
        auto make = [&](const char *n, const QByteArray &b) {
            QFile f(dir.filePath(QString::fromLatin1(n)));
            if (!f.open(QIODevice::WriteOnly))
                qFatal("cannot create");
            f.write(b);
            return f.fileName();
        };
        const QByteArray ru = enc("windows-1251", kRu);
        const Decoded d = readFile(make("a.txt", ru));
        QCOMPARE(d.text, kRu);
        QCOMPARE(d.totalBytes, ru.size());
        QVERIFY(!d.truncated);

        const QByteArray line = QByteArray(79, 'y') + '\n';
        const QByteArray big = rep(line, 3 * 1024 * 1024 / 80);
        const Decoded t = readFile(make("big.txt", big));
        QVERIFY(t.truncated);
        QCOMPARE(t.totalBytes, big.size());
        QVERIFY(t.shownBytes < kMaxBytes);

        const Decoded none = readFile(dir.filePath(QStringLiteral("missing")));
        QVERIFY(none.encoding.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstTextDecode)
#include "tst_textdecode.moc"
