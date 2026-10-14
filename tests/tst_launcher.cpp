#include "Launcher.h"
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

using namespace ql;

class TstLauncher : public QObject {
    Q_OBJECT
    static QByteArray waitRead(const QString &f)
    {
        for (int i = 0; i < 100; ++i) {
            QFile file(f);
            if (file.open(QIODevice::ReadOnly)) {
                const QByteArray d = file.readAll();
                if (d.contains("END\n"))
                    return d;
            }
            QThread::msleep(50);
        }
        return {};
    }
    static QString stub(const QString &dir, const QString &out)
    {
        const QString p = dir + QStringLiteral("/stub.sh");
        QFile f(p);
        f.open(QIODevice::WriteOnly);
        f.write(("#!/bin/sh\n{ echo \"ARGS:$*\"; echo \"PWD:$(pwd)\"; env; echo END; } > '" + out + ".tmp'\nmv '" + out + ".tmp' '" + out + "'\n").toUtf8());
        f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
        return p;
    }
private Q_SLOTS:
    void launchesWithSnapshotEnv()
    {
        QProcessEnvironment snap = QProcessEnvironment::systemEnvironment();
        snap.insert(QStringLiteral("QL_MARK"), QStringLiteral("1"));
        qputenv("QSG_RENDER_LOOP", "basic");
        qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", ",");
        QTemporaryDir dir;
        const QString out = dir.filePath(QStringLiteral("out.txt")), pic = dir.filePath(QStringLiteral("pic.png"));
        Launcher l(snap);
        l.setProgram(stub(dir.path(), out));
        l.setDesktopIdOverride(QStringLiteral("imv.desktop"));
        QCOMPARE(l.open(pic, QStringLiteral("image/png")), Launcher::Result::Launched);
        const QByteArray d = waitRead(out);
        QVERIFY2(!d.isEmpty(), "stub did not finish");
        QVERIFY(d.contains(("ARGS:-- imv.desktop " + pic + "\n").toUtf8()));
        QVERIFY(d.contains(("PWD:" + dir.path() + "\n").toUtf8()));
        QVERIFY(d.contains("\nQL_MARK=1\n"));
        QVERIFY(!d.contains("QSG_RENDER_LOOP"));
        QVERIFY(!d.contains("QT_FFMPEG_"));
    }
    void folderCwdIsItself()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkdir(QStringLiteral("d"));
        const QString out = dir.filePath(QStringLiteral("out.txt")), sub = dir.filePath(QStringLiteral("d"));
        Launcher l(QProcessEnvironment::systemEnvironment());
        l.setProgram(stub(dir.path(), out));
        l.setDesktopIdOverride(QStringLiteral("x.desktop"));
        QCOMPARE(l.open(sub, QStringLiteral("inode/directory")), Launcher::Result::Launched);
        QVERIFY(waitRead(out).contains(("PWD:" + sub + "\n").toUtf8()));
    }
    void noApp()
    {
        Launcher l(QProcessEnvironment::systemEnvironment());
        l.setDesktopIdOverride(QString());
        QCOMPARE(l.open(QStringLiteral("/tmp/x"), QStringLiteral("application/x-ql-unknown")), Launcher::Result::NoApp);
    }
    void failed()
    {
        Launcher l(QProcessEnvironment::systemEnvironment());
        l.setProgram(QStringLiteral("/nonexistent/prog"));
        l.setDesktopIdOverride(QStringLiteral("x.desktop"));
        QCOMPARE(l.open(QStringLiteral("/tmp/x"), QStringLiteral("text/plain")), Launcher::Result::Failed);
    }
};

QTEST_MAIN(TstLauncher)
#include "tst_launcher.moc"
