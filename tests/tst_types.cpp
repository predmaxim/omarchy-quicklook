#include <QtTest>
#include "Pools.h"
#include "Types.h"

using namespace ql;

namespace {
const QString nb = QString(QChar(0x00A0));
struct EnvGuard {
    QByteArray lc, msg, lang, tz;
    EnvGuard() : lc(qgetenv("LC_ALL")), msg(qgetenv("LC_MESSAGES")), lang(qgetenv("LANG")), tz(qgetenv("TZ")) {}
    ~EnvGuard() { restore("LC_ALL", lc); restore("LC_MESSAGES", msg); restore("LANG", lang); restore("TZ", tz); }
    static void restore(const char *k, const QByteArray &v) { if (v.isNull() || v.isEmpty()) qunsetenv(k); else qputenv(k, v); }
};
void setEnv(const char *a, const char *b, const char *c)
{
    for (auto [k, v] : {std::pair{"LC_ALL", a}, {"LC_MESSAGES", b}, {"LANG", c}}) {
        if (v) qputenv(k, v); else qunsetenv(k);
    }
}
}

class TstTypes : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void kindNames()
    {
        const QList<std::pair<Kind, QString>> m = {
            {Kind::None, ""}, {Kind::Image, "image"}, {Kind::Svg, "svg"}, {Kind::Pdf, "pdf"}, {Kind::Office, "office"},
            {Kind::Text, "text"}, {Kind::Video, "video"}, {Kind::Audio, "audio"}, {Kind::FileCard, "file"}, {Kind::FolderCard, "folder"}};
        for (const auto &[k, n] : m) QCOMPARE(kindName(k), n);
    }
    void uiRussianEnv()
    {
        EnvGuard g;
        setEnv(nullptr, nullptr, nullptr);          QVERIFY(!uiRussian());
        setEnv(nullptr, nullptr, "ru_RU.UTF-8");    QVERIFY(uiRussian());
        setEnv(nullptr, nullptr, "en_US.UTF-8");    QVERIFY(!uiRussian());
        setEnv("", "", "ru_RU.UTF-8");              QVERIFY(uiRussian());   // empty values are skipped
        setEnv("en_US.UTF-8", nullptr, "ru_RU.UTF-8"); QVERIFY(!uiRussian());
        setEnv("ru_RU.UTF-8", nullptr, "en_US.UTF-8"); QVERIFY(uiRussian());
        setEnv(nullptr, "ru_RU.UTF-8", "en_US.UTF-8"); QVERIFY(uiRussian());
        setEnv(nullptr, "C", "ru_RU.UTF-8");        QVERIFY(!uiRussian());
        setEnv(nullptr, nullptr, "ru_RU.UTF-8");
        QCOMPARE(uiText(Str::EmptyFile), QString::fromUtf8("Пустой файл"));
    }
    void texts()
    {
        struct R { Str id; const char *ru; const char *en; };
        const R rows[] = {
            {Str::CannotOpen, "Не удалось открыть", "Can't open"},
            {Str::PasswordProtected, "Защищён паролем", "Password protected"},
            {Str::ConversionFailed, "Не удалось сконвертировать", "Couldn't convert"},
            {Str::TooLarge, "Слишком большое", "Too large"},
            {Str::NoApp, "Нет программы для этого типа", "No app for this type"},
            {Str::Items, "Элементов: %1", "Items: %1"},
            {Str::EmptyFile, "Пустой файл", "Empty file"}};
        for (const auto &r : rows) {
            QCOMPARE(uiText(r.id, true), QString::fromUtf8(r.ru));
            QCOMPARE(uiText(r.id, false), QString::fromUtf8(r.en));
        }
    }
    void reasons()
    {
        QCOMPARE(reasonText(CardReason::None, true), QString());
        QCOMPARE(reasonText(CardReason::None, false), QString());
        QCOMPARE(reasonText(CardReason::CannotOpen, false), QString("Can't open"));
        QCOMPARE(reasonText(CardReason::PasswordProtected, true), QString::fromUtf8("Защищён паролем"));
        QCOMPARE(reasonText(CardReason::ConversionFailed, false), QString("Couldn't convert"));
        QCOMPARE(reasonText(CardReason::TooLarge, true), QString::fromUtf8("Слишком большое"));
    }
    void items()
    {
        QCOMPARE(itemsText(5, true), QString::fromUtf8("Элементов: 5"));
        QCOMPARE(itemsText(5, false), QString("Items: 5"));
        QCOMPARE(itemsText(1234, true), QString::fromUtf8("Элементов: 1") + nb + "234");
        QCOMPARE(itemsText(1234, false), QString("Items: 1,234"));
        QCOMPARE(itemsText(10000, false), QString("Items: 10,000"));
        QCOMPARE(itemsText(10001, true), QString::fromUtf8("Элементов: 10") + nb + "000+");
        QCOMPARE(itemsText(10001, false), QString("Items: 10,000+"));
    }
    void sizes()
    {
        const QString ru = QString::fromUtf8("Б"), rk = QString::fromUtf8("КБ"), rm = QString::fromUtf8("МБ"), rg = QString::fromUtf8("ГБ");
        QCOMPARE(formatSize(0, true), "0 " + ru);
        QCOMPARE(formatSize(512, true), "512 " + ru);
        QCOMPARE(formatSize(512, false), QString("512 B"));
        QCOMPARE(formatSize(1536, true), "1,5 " + rk);
        QCOMPARE(formatSize(1536, false), QString("1.5 KB"));
        QCOMPARE(formatSize(1288490, true), "1,2 " + rm);
        QCOMPARE(formatSize(1288490, false), QString("1.2 MB"));
        QCOMPARE(formatSize(17511219, true), "16,7 " + rm);
        QCOMPARE(formatSize(17511219, false), QString("16.7 MB"));
        QCOMPARE(formatSize(3221225472, true), "3,0 " + rg);
        QCOMPARE(formatSize(3221225472, false), QString("3.0 GB"));
        QCOMPARE(formatSize(1048575, false), QString("1.0 MB"));   // never "1024.0 KB"
    }
    void dates()
    {
        EnvGuard g;
        qputenv("TZ", "UTC-3");
        const QDateTime t(QDate(2026, 10, 14), QTime(19, 5), QTimeZone::UTC);
        QCOMPARE(formatDate(t, true), QString("14.10.2026 22:05"));
        QCOMPARE(formatDate(t, false), QString("2026-10-14 22:05"));
    }
    void hint()
    {
        QCOMPARE(truncatedHint(2097152, 17511219, true), QString::fromUtf8("Показаны первые 2,0 МБ из 16,7 МБ — Enter откроет"));
        QCOMPARE(truncatedHint(2097152, 17511219, false), QString::fromUtf8("Showing first 2.0 MB of 16.7 MB — Enter opens it"));
    }
    void pools()
    {
        QCOMPARE(pools::probe()->maxThreadCount(), 2);
        QCOMPARE(pools::heavy()->maxThreadCount(), 3);
        QVERIFY(pools::probe() != pools::heavy());
        QCOMPARE(pools::currentGeneration().load(), quint64(0));
        QVERIFY(pools::isCurrent(0));
        pools::currentGeneration().store(7);
        QVERIFY(pools::isCurrent(7));
        QVERIFY(!pools::isCurrent(6));
        pools::currentGeneration().store(0);
    }
};

QTEST_GUILESS_MAIN(TstTypes)
#include "tst_types.moc"
