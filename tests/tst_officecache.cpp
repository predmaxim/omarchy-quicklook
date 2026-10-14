#include "OfficeCache.h"
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace ql;

namespace {
void put(const QString &p, const QByteArray &d)
{
    QFile f(p);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(d);
}
}

class TstOfficeCache : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void key()
    {
        QTemporaryDir td;
        const QString src = td.filePath("a.docx");
        put(src, "one");
        OfficeCache c(td.filePath("cache"));
        const QString k = c.keyFor(src);
        QCOMPARE(k.size(), 64);
        QCOMPARE(c.keyFor(src), k);
        put(src, "three");   // size changes
        QVERIFY(c.keyFor(src) != k);
        const QString k2 = c.keyFor(src);
        QFile f(src);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.setFileTime(QDateTime::currentDateTime().addSecs(-1000), QFileDevice::FileModificationTime));
        f.close();
        QVERIFY(c.keyFor(src) != k2);   // mtime only
    }
    void storeLookup()
    {
        QTemporaryDir td;
        const QString src = td.filePath("a.docx");
        put(src, "doc");
        OfficeCache c(td.filePath("cache"));
        QCOMPARE(c.lookup(src), QString());
        const QString job = td.filePath("job.pdf");
        put(job, "%PDF-1.4");
        const QString out = c.store(src, job);
        QCOMPARE(out, c.dir() + "/" + c.keyFor(src) + ".pdf");
        QVERIFY(!QFile::exists(job));
        QCOMPARE(QFileInfo(out).permissions() & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther | QFile::ExeOwner), QFileDevice::Permissions());
        QVERIFY(QFileInfo(out).permissions().testFlag(QFile::WriteOwner));
        QCOMPARE(QFileInfo(c.dir()).permissions() & (QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther), QFileDevice::Permissions());
        QFile f(out);
        QVERIFY(f.open(QIODevice::ReadWrite));
        const QDateTime old = QDateTime::currentDateTime().addDays(-5);
        QVERIFY(f.setFileTime(old, QFileDevice::FileModificationTime));
        f.close();
        QCOMPARE(c.lookup(src), out);
        QVERIFY(QFileInfo(out).lastModified() > old.addDays(1));
        // empty pdf is not a hit
        QFile e(out); QVERIFY(e.open(QIODevice::WriteOnly | QIODevice::Truncate)); e.close();
        QCOMPARE(c.lookup(src), QString());
    }
    void evictOldestFirst()
    {
        QTemporaryDir td;
        OfficeCache c(td.filePath("cache"));
        const QDateTime now = QDateTime::currentDateTime();
        for (int i = 0; i < 3; ++i) {
            const QString p = c.dir() + "/k" + QString::number(i) + ".pdf";
            put(p, QByteArray(100, 'x'));
            QFile f(p); QVERIFY(f.open(QIODevice::ReadWrite));
            QVERIFY(f.setFileTime(now.addSecs(-300 + i * 100), QFileDevice::FileModificationTime));
        }
        c.evict(250);
        QVERIFY(!QFile::exists(c.dir() + "/k0.pdf"));
        QVERIFY(QFile::exists(c.dir() + "/k1.pdf"));
        QVERIFY(QFile::exists(c.dir() + "/k2.pdf"));
        c.evict(0);
        QVERIFY(QDir(c.dir()).entryList({"*.pdf"}).isEmpty());
    }
    void storeAcrossFilesystems()
    {
        QTemporaryDir other("/dev/shm/qlXXXXXX");
        QTemporaryDir td;
        if (!other.isValid() || QStorageInfo(other.path()) == QStorageInfo(td.path()))
            QSKIP("no second filesystem");
        const QString src = td.filePath("a.docx");
        put(src, "doc");
        OfficeCache c(td.filePath("cache"));
        const QString job = other.filePath("job.pdf");
        put(job, "%PDF-1.4");
        const QString out = c.store(src, job);
        QVERIFY(!out.isEmpty() && QFileInfo(out).size() == 8 && !QFile::exists(job));
    }
};

QTEST_MAIN(TstOfficeCache)
#include "tst_officecache.moc"
