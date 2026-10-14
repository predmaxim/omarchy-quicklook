#include "OfficeCache.h"
#include "OfficeConverter.h"
#include <KZip>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>
#include <csignal>
#include <unistd.h>

using namespace ql;

namespace {
const QString kScripting = QStringLiteral("/org.openoffice.Office.Common/Security/Scripting");

QByteArray readAll(const QString &p)
{
    QFile f(p);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

void put(const QString &p, const QByteArray &d)
{
    QFile f(p);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(d);
}

bool alive(qint64 pid)
{
    if (::kill(pid_t(pid), 0) != 0)
        return false;
    const QByteArray stat = readAll(QStringLiteral("/proc/%1/stat").arg(pid));
    const int rp = stat.lastIndexOf(')');
    return rp < 0 || stat.mid(rp + 2, 1) != "Z";   // a zombie is dead for our purposes
}

QStringList pidsOf(const QString &log, const QString &kind)
{
    QStringList out;
    for (const QByteArray &line : readAll(log).split('\n'))
        if (line.startsWith(kind.toUtf8() + ' '))
            out << QString::fromUtf8(line.split(' ').value(1));
    return out;
}

bool hasItem(const QByteArray &xcu, const char *name, const char *value)
{
    for (const QByteArray &line : xcu.split('\n'))
        if (line.contains(kScripting.toUtf8()) && line.contains(QByteArray("oor:name=\"") + name + '"')
            && line.contains(QByteArray("<value>") + value + "</value>"))
            return true;
    return false;
}

// LibreOffice reads the items in order, the later one wins
QByteArray lastValue(const QByteArray &xcu, const char *name)
{
    QByteArray v;
    for (const QByteArray &line : xcu.split('\n'))
        if (line.contains(kScripting.toUtf8()) && line.contains(QByteArray("oor:name=\"") + name + '"')) {
            const qsizetype a = line.indexOf("<value>") + 7, b = line.indexOf("</value>");
            v = line.mid(a, b - a);
        }
    return v;
}

QString userInstallation(const QString &profile)
{
    return "-env:UserInstallation=" + QUrl::fromLocalFile(profile).toString(QUrl::FullyEncoded);
}

// A .docx whose only picture is linked (not embedded) from http://127.0.0.1:<port>/x.png
QString makeLinkedDocx(const QString &path, quint16 port)
{
    KZip z(path);
    if (!z.open(QIODevice::WriteOnly))
        return {};
    z.writeFile(QStringLiteral("[Content_Types].xml"), QByteArray(R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/></Types>)"));
    z.writeFile(QStringLiteral("_rels/.rels"), QByteArray(R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/></Relationships>)"));
    z.writeFile(QStringLiteral("word/_rels/document.xml.rels"), QByteArray(R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="http://127.0.0.1:PORT/x.png" TargetMode="External"/></Relationships>)").replace("PORT", QByteArray::number(port)));
    z.writeFile(QStringLiteral("word/document.xml"), QByteArray(R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" xmlns:wp="http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing" xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" xmlns:pic="http://schemas.openxmlformats.org/drawingml/2006/picture"><w:body><w:p><w:r><w:t>linked</w:t></w:r><w:r><w:drawing><wp:inline><wp:extent cx="914400" cy="914400"/><wp:docPr id="1" name="p"/><a:graphic><a:graphicData uri="http://schemas.openxmlformats.org/drawingml/2006/picture"><pic:pic><pic:nvPicPr><pic:cNvPr id="1" name="p"/><pic:cNvPicPr/></pic:nvPicPr><pic:blipFill><a:blip r:link="rId1"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill><pic:spPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="914400" cy="914400"/></a:xfrm><a:prstGeom prst="rect"/></pic:spPr></pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing></w:r></w:p></w:body></w:document>)"));
    return z.close() ? path : QString();
}
}

class TstOfficeConverter : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    QString m_stub = QStringLiteral(QL_SOURCE_DIR "/tests/fixtures/office/fake-soffice");
    QString m_log;
    QProcessEnvironment m_env;
    int m_logN = 0;

    static bool guarded()
    {
        // never let killOrphans/profile writes/removals near the user's real cache (or anywhere else)
        const QString cache = qEnvironmentVariable("XDG_CACHE_HOME");
        const QString base = cache + "/omarchy-quicklook/";
        return cache.contains(QLatin1String("/test-home/"))
            && QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) == cache
            && OfficeConverter::profileDir().startsWith(base) && OfficeConverter::tmpDir().startsWith(base);
    }
    QString file(const QString &name)
    {
        const QString p = m_tmp.filePath(name);
        put(p, "doc " + name.toUtf8());
        return p;
    }
    void freshLog()
    {
        m_log = m_tmp.filePath(QStringLiteral("stub-%1.log").arg(++m_logN));
        m_env.insert(QStringLiteral("STUB_LOG"), m_log);
    }
    static bool haveSoffice() { return !QStandardPaths::findExecutable(QStringLiteral("soffice")).isEmpty(); }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY2(guarded(), "run via ctest: XDG_CACHE_HOME must be the test home");
        QVERIFY(m_tmp.isValid());
        QVERIFY(QFileInfo(m_stub).isExecutable());
        m_env = QProcessEnvironment::systemEnvironment();
        OfficeConverter::killOrphans(OfficeConverter::profileDir());
        QDir(OfficeConverter::profileDir()).removeRecursively();
    }
    void init() { freshLog(); }
    void cleanupTestCase()
    {
        if (!guarded())   // QtTest runs this even after initTestCase failed
            return;
        OfficeConverter::killOrphans(OfficeConverter::profileDir());
        OfficeConverter::killOrphans(controlProfile());
        const QString prefix = QStringLiteral("OSL_PIPE_%1_qltest_").arg(::getuid());
        for (const QString &n : QDir(QStringLiteral("/tmp")).entryList({prefix + "*"}, QDir::System | QDir::Files))
            QFile::remove("/tmp/" + n);
    }

    void paths()
    {
        const QString base = qEnvironmentVariable("XDG_CACHE_HOME") + "/omarchy-quicklook";
        QCOMPARE(OfficeConverter::profileDir(), base + "/lo-profile");
        QCOMPARE(OfficeConverter::tmpDir(), base + "/lo-tmp");
    }

    void securityEmpty()
    {
        QTemporaryDir p;
        QVERIFY(OfficeConverter::ensureProfileSecurity(p.path()));
        const QByteArray xcu = readAll(p.filePath("user/registrymodifications.xcu"));
        QVERIFY(xcu.startsWith("<?xml"));
        QVERIFY(xcu.contains("<oor:items") && xcu.trimmed().endsWith("</oor:items>"));
        QVERIFY(hasItem(xcu, "MacroSecurityLevel", "3"));
        QVERIFY(hasItem(xcu, "BlockUntrustedRefererLinks", "true"));
        QCOMPARE(xcu.count("oor:op=\"fuse\""), 2);
        // idempotent
        QVERIFY(OfficeConverter::ensureProfileSecurity(p.path()));
        QCOMPARE(readAll(p.filePath("user/registrymodifications.xcu")), xcu);
    }

    void securityMerge()
    {
        QTemporaryDir p;
        QVERIFY(QDir().mkpath(p.filePath("user")));
        const QByteArray other = R"(<item oor:path="/org.openoffice.Office.Common/Misc"><prop oor:name="FirstRun" oor:op="fuse"><value>false</value></prop></item>)";
        const QByteArray macro = R"(<item oor:path="/org.openoffice.Office.Common/Security/Scripting"><prop oor:name="MacroSecurityLevel" oor:op="fuse"><value>2</value></prop></item>)";
        const QByteArray blockOn = R"(<item oor:path="/org.openoffice.Office.Common/Security/Scripting"><prop oor:name="BlockUntrustedRefererLinks" oor:op="fuse"><value>true</value></prop></item>)";
        const QByteArray blockOff = R"(<item oor:path="/org.openoffice.Office.Common/Security/Scripting"><prop oor:name="BlockUntrustedRefererLinks" oor:op="fuse"><value>false</value></prop></item>)";
        const QByteArray head = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<oor:items xmlns:oor=\"http://openoffice.org/2001/registry\" "
                                "xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\n";
        const QString path = p.filePath("user/registrymodifications.xcu");

        // wrong values (true then false: false wins) -> corrected items appended, the rest kept as is
        put(path, head + other + "\n" + macro + "\n" + blockOn + "\n" + blockOff + "\n</oor:items>\n");
        QVERIFY(OfficeConverter::ensureProfileSecurity(p.path()));
        QByteArray xcu = readAll(path);
        QVERIFY(xcu.startsWith(head + other + "\n" + macro + "\n" + blockOn + "\n" + blockOff + "\n"));
        QCOMPARE(lastValue(xcu, "MacroSecurityLevel"), QByteArray("3"));
        QCOMPARE(lastValue(xcu, "BlockUntrustedRefererLinks"), QByteArray("true"));
        QCOMPARE(xcu.count("<item "), 6);
        QVERIFY(xcu.trimmed().endsWith("</oor:items>"));
        QVERIFY(OfficeConverter::ensureProfileSecurity(p.path()));
        QCOMPARE(readAll(path), xcu);   // idempotent

        // one key already right, the other missing -> only the missing one is added
        const QByteArray macro3 = QByteArray(macro).replace("<value>2<", "<value>3<");
        put(path, head + other + "\n" + macro3 + "\n</oor:items>\n");
        QVERIFY(OfficeConverter::ensureProfileSecurity(p.path()));
        xcu = readAll(path);
        QVERIFY(xcu.startsWith(head + other + "\n" + macro3 + "\n"));
        QCOMPARE(xcu.count("MacroSecurityLevel"), 1);
        QCOMPARE(lastValue(xcu, "BlockUntrustedRefererLinks"), QByteArray("true"));
        QCOMPARE(xcu.count("<item "), 3);
    }

    void killOrphansOnlyOurs()
    {
        QProcess ours, other;
        QList<pid_t> groups;   // the stubs and their sleep children, killed and reaped even if a check fails
        const auto reap = qScopeGuard([&] {
            for (pid_t g : groups)
                ::kill(-g, SIGKILL);
            ours.waitForFinished(3000);
            other.waitForFinished(3000);
        });
        auto start = [&](QProcess &pr, const QString &profile) {
            QProcessEnvironment env = m_env;
            env.insert(QStringLiteral("TMPDIR"), m_tmp.path());
            pr.setProcessEnvironment(env);
            pr.setChildProcessModifier([] { ::setpgid(0, 0); });
            pr.start(m_stub, {QStringLiteral("--headless"), userInstallation(profile)});
            if (pr.waitForStarted() && pr.processId() > 0)   // kill(-0) would hit the test itself
                groups << pid_t(pr.processId());
        };
        start(ours, OfficeConverter::profileDir());
        start(other, OfficeConverter::profileDir() + "-other");
        QCOMPARE(groups.size(), 2);
        QTRY_COMPARE(pidsOf(m_log, "instance").size(), 2);
        QCOMPARE(OfficeConverter::killOrphans(OfficeConverter::profileDir()), 1);
        QVERIFY(ours.waitForFinished(3000));
        QCOMPARE(ours.exitStatus(), QProcess::CrashExit);
        QCOMPARE(other.state(), QProcess::Running);
    }

    void queueKeepsNewest()
    {
        OfficeCache cache(m_tmp.filePath("cache-q"));
        auto c = std::make_unique<OfficeConverter>(&cache, m_env);   // destroyed even if a check fails
        QSignalSpy ok(c.get(), &OfficeConverter::converted), bad(c.get(), &OfficeConverter::failed);
        c->setProgram(m_stub);
        const QString a = file("a.odt"), b = file("b.odt"), cc = file("c.odt");
        c->convert(1, a);
        c->convert(2, b);
        c->convert(3, cc);
        QCOMPARE(ok.size() + bad.size(), 0);   // never synchronously
        QTRY_COMPARE_WITH_TIMEOUT(ok.size(), 2, 5000);
        QCOMPARE(ok.at(0).at(0).toULongLong(), 1u);
        QCOMPARE(ok.at(1).at(0).toULongLong(), 3u);
        QCOMPARE(ok.at(0).at(1).toString(), cache.lookup(a));
        QCOMPARE(ok.at(1).at(1).toString(), cache.lookup(cc));
        QCOMPARE(readAll(cache.lookup(a)), QByteArray("%PDF-1.4 stub\n"));
        QVERIFY(cache.lookup(b).isEmpty());
        QCOMPARE(bad.size(), 0);
        QCOMPARE(pidsOf(m_log, "instance").size(), 1);   // one warm instance for both jobs
        QCOMPARE(pidsOf(m_log, "convert").size(), 2);
        // instance env: TMPDIR is ours; the anchor is opened from our cache
        QVERIFY(readAll(m_log).contains("instance " + pidsOf(m_log, "instance").value(0).toUtf8() + ' '
                                        + OfficeConverter::tmpDir().toUtf8() + '\n'));
        QVERIFY(QFileInfo(qEnvironmentVariable("XDG_CACHE_HOME") + "/omarchy-quicklook/anchor.odt").size() > 0);
        QVERIFY(QFile::exists(OfficeConverter::profileDir() + "/user/registrymodifications.xcu"));
        const qint64 inst = pidsOf(m_log, "instance").value(0).toLongLong();
        c.reset();   // kills the warm instance group
        QTRY_VERIFY(!alive(inst));
        for (const QString &s : pidsOf(m_log, "sleep"))
            QTRY_VERIFY(!alive(s.toLongLong()));
    }

    void timeoutKillsGroupAndRecovers()
    {
        OfficeCache cache(m_tmp.filePath("cache-t"));
        OfficeConverter c(&cache, m_env);
        QSignalSpy ok(&c, &OfficeConverter::converted), bad(&c, &OfficeConverter::failed);
        c.setProgram(m_stub);
        c.setTimeouts(500, 60000);
        QElapsedTimer t;
        t.start();
        c.convert(7, file("slow.odt"));
        QTRY_COMPARE_WITH_TIMEOUT(bad.size(), 1, 3000);
        QVERIFY2(t.elapsed() < 1200, qPrintable(QString::number(t.elapsed())));
        QCOMPARE(bad.at(0).at(0).toULongLong(), 7u);
        QCOMPARE(ok.size(), 0);
        // the job, its child sleep, the instance and its child sleep are all gone
        QStringList pids = pidsOf(m_log, "instance") + pidsOf(m_log, "convert") + pidsOf(m_log, "sleep");
        QCOMPARE(pids.size(), 4);
        for (const QString &p : pids)
            QTRY_VERIFY(!alive(p.toLongLong()));
        // the next job starts a new instance and succeeds
        c.setTimeouts(30000, 60000);
        c.convert(8, file("next.odt"));
        QTRY_COMPARE_WITH_TIMEOUT(ok.size(), 1, 5000);
        QCOMPARE(ok.at(0).at(0).toULongLong(), 8u);
        QCOMPARE(pidsOf(m_log, "instance").size(), 2);
    }

    void idleShutdown()
    {
        OfficeCache cache(m_tmp.filePath("cache-i"));
        OfficeConverter c(&cache, m_env);
        QSignalSpy ok(&c, &OfficeConverter::converted);
        c.setProgram(m_stub);
        c.setTimeouts(30000, 300);
        c.convert(1, file("idle.odt"));
        QTRY_COMPARE_WITH_TIMEOUT(ok.size(), 1, 5000);
        const QString inst = pidsOf(m_log, "instance").value(0);
        QVERIFY(QFile::exists(OfficeConverter::tmpDir() + "/instance-marker"));
        QTRY_VERIFY_WITH_TIMEOUT(pidsOf(m_log, "term").contains(inst), 2000);   // SIGTERM first
        QVERIFY(alive(inst.toLongLong()));                                     // the stub ignores it
        QTRY_VERIFY_WITH_TIMEOUT(!alive(inst.toLongLong()), 5000);             // SIGKILL after the grace
        QTRY_VERIFY(QDir(OfficeConverter::tmpDir()).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System));
        for (const QString &s : pidsOf(m_log, "sleep"))
            QTRY_VERIFY(!alive(s.toLongLong()));
    }

    void missingProgramFails()
    {
        OfficeCache cache(m_tmp.filePath("cache-m"));
        OfficeConverter c(&cache, m_env);
        QSignalSpy bad(&c, &OfficeConverter::failed);
        c.setProgram(m_tmp.filePath("no-such-soffice"));
        c.convert(5, file("m.odt"));
        QCOMPARE(bad.size(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(bad.size(), 1, 5000);
        QCOMPARE(bad.at(0).at(0).toULongLong(), 5u);
    }

    void realLibreOffice()
    {
        if (!haveSoffice())
            QSKIP("soffice not installed");
        OfficeCache cache(m_tmp.filePath("cache-lo"));
        OfficeConverter c(&cache, m_env);
        QSignalSpy ok(&c, &OfficeConverter::converted), bad(&c, &OfficeConverter::failed);
        c.convert(1, QStringLiteral(QL_SOURCE_DIR "/tests/fixtures/office/a.odt"));
        QTRY_COMPARE_WITH_TIMEOUT(ok.size() + bad.size(), 1, 60000);
        QCOMPARE(ok.size(), 1);
        const QString pdf = ok.at(0).at(1).toString();
        QVERIFY(pdf.startsWith(cache.dir() + "/"));
        QVERIFY(readAll(pdf).startsWith("%PDF"));
        c.convert(2, QStringLiteral(QL_SOURCE_DIR "/tests/fixtures/office/legacy.doc"));
        QTRY_COMPARE_WITH_TIMEOUT(ok.size() + bad.size(), 2, 60000);
        QCOMPARE(ok.size(), 2);
        QCOMPARE(ok.at(1).at(0).toULongLong(), 2u);
        QVERIFY(readAll(ok.at(1).at(1).toString()).startsWith("%PDF"));
    }

    void externalLinkControl()
    {
        // proves the next test can see a fetch: plain soffice on a throwaway profile with the block turned off
        // (the converter itself would correct that profile) does GET the linked picture
        if (!haveSoffice())
            QSKIP("soffice not installed");
        const int fetches = fetchesDuringConversion(true);
        QVERIFY2(fetches > 0, qPrintable(QString::number(fetches)));
    }

    void externalLinkBlocked()
    {
        if (!haveSoffice())
            QSKIP("soffice not installed");
        QCOMPARE(fetchesDuringConversion(false), 0);
    }

private:
    static QString controlProfile() { return OfficeConverter::profileDir() + "-control"; }

    // incoming connections, -1 if the setup or the conversion failed
    int fetchesDuringConversion(bool control)
    {
        QTcpServer server;
        if (!server.listen(QHostAddress::LocalHost, 0))
            return -1;
        int connections = 0;
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            while (QTcpSocket *s = server.nextPendingConnection()) {
                ++connections;
                s->abort();
                s->deleteLater();
            }
        });
        const QString docx = makeLinkedDocx(m_tmp.filePath(QStringLiteral("linked-%1.docx").arg(server.serverPort())), server.serverPort());
        if (docx.isEmpty())
            return -1;
        if (control) {
            const QString xcu = controlProfile() + "/user/registrymodifications.xcu";
            QDir(controlProfile()).removeRecursively();
            if (!OfficeConverter::ensureProfileSecurity(controlProfile()))
                return -1;
            QByteArray x = readAll(xcu);
            x.replace("</oor:items>", R"(<item oor:path="/org.openoffice.Office.Common/Security/Scripting"><prop oor:name="BlockUntrustedRefererLinks" oor:op="fuse"><value>false</value></prop></item>
</oor:items>)");
            put(xcu, x);
            const QString out = m_tmp.filePath(QStringLiteral("control-out"));
            QProcess lo;
            lo.setProcessEnvironment(m_env);
            lo.setChildProcessModifier([] { ::setpgid(0, 0); });
            lo.setStandardOutputFile(QProcess::nullDevice());
            lo.setStandardErrorFile(QProcess::nullDevice());
            const auto reap = qScopeGuard([&lo] {   // soffice.bin too, even on a timeout
                if (const pid_t pid = pid_t(lo.processId()); pid > 0)
                    ::kill(-pid, SIGKILL);
                lo.waitForFinished(3000);
            });
            lo.start(QStringLiteral("soffice"), {QStringLiteral("--headless"), QStringLiteral("--norestore"), userInstallation(controlProfile()),
                                                 QStringLiteral("--convert-to"), QStringLiteral("pdf"), QStringLiteral("--outdir"), out, docx});
            if (!QTest::qWaitFor([&] { return lo.state() == QProcess::NotRunning; }, 60000)
                || QFileInfo(out + "/" + QFileInfo(docx).completeBaseName() + ".pdf").size() <= 0)
                return -1;
        } else {
            OfficeCache cache(m_tmp.filePath("cache-link"));
            OfficeConverter c(&cache, m_env);
            QSignalSpy ok(&c, &OfficeConverter::converted), bad(&c, &OfficeConverter::failed);
            c.convert(1, docx);
            if (!QTest::qWaitFor([&] { return ok.size() + bad.size() > 0; }, 60000) || ok.isEmpty())
                return -1;
        }
        QTest::qWait(200);   // a late fetch would still land
        return connections;
    }
};

QTEST_GUILESS_MAIN(TstOfficeConverter)
#include "tst_officeconverter.moc"
