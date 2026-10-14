#include <QtTest>
#include <QLocalServer>
#include <QLocalSocket>
#include <QThread>
#include <QTemporaryDir>
#include <QDir>
#include <atomic>
#include "Hyprland.h"

using namespace ql;
using namespace ql::hyprland;

namespace {
const QByteArray fixture = R"([
 {"id":0,"name":"eDP-2","description":"x","make":"BOE","model":"y","serial":"","width":2560,"height":1600,
  "refreshRate":165.0,"x":0,"y":0,"activeWorkspace":{"id":1,"name":"1"},"specialWorkspace":{"id":0,"name":""},
  "reserved":[0,45,0,0],"scale":1.25,"transform":0,"focused":true,"dpmsStatus":true,"vrr":false},
 {"id":1,"name":"DP-1","width":1920,"height":1080,"x":2048,"y":0,"reserved":[0,0,0,0],"scale":1.00,
  "transform":1,"focused":false}
])";

// Fake Hyprland: lives in its own thread because request() blocks the caller.
class FakeServer : public QObject
{
    Q_OBJECT
public:
    enum Mode { Answer, Silent, HalfAnswer };
    Mode mode = Answer;
    std::atomic<int> requests{0};
    QLocalServer server{this};
    QList<QLocalSocket *> held;
    bool listenAt(const QString &path)
    {
        connect(&server, &QLocalServer::newConnection, this, &FakeServer::onConnection);
        return server.listen(path);
    }
private Q_SLOTS:
    void onConnection()
    {
        while (auto *s = server.nextPendingConnection()) {
            connect(s, &QLocalSocket::readyRead, this, [this, s] {
                if (s->readAll() != "j/monitors") return;
                requests++;
                if (mode == Silent) { held << s; return; }
                if (mode == HalfAnswer) { s->write(fixture.left(10)); s->flush(); held << s; return; }
                s->write(fixture.left(fixture.size() / 2));
                s->flush();
                QTimer::singleShot(10, s, [s] { s->write(fixture.mid(fixture.size() / 2)); s->disconnectFromServer(); });
            });
        }
    }
};

struct Fake {
    QTemporaryDir dir;
    QThread thread;
    FakeServer *srv = new FakeServer;
    explicit Fake(FakeServer::Mode m = FakeServer::Answer)
    {
        srv->mode = m;
        QDir().mkpath(dir.path() + "/hypr/sig1");
        srv->moveToThread(&thread);
        thread.start();
        bool ok = false;
        QMetaObject::invokeMethod(srv, [this, &ok] { ok = srv->listenAt(dir.path() + "/hypr/sig1/.socket.sock"); },
                                  Qt::BlockingQueuedConnection);
        Q_ASSERT(ok);
        qputenv("XDG_RUNTIME_DIR", dir.path().toUtf8());
        qputenv("HYPRLAND_INSTANCE_SIGNATURE", "sig1");
    }
    ~Fake()
    {
        QMetaObject::invokeMethod(srv, [this] { delete srv; }, Qt::BlockingQueuedConnection);
        thread.quit();
        thread.wait();
    }
};
}

class TstHyprland : public QObject
{
    Q_OBJECT
    QByteArray m_sig, m_xdg;
    bool m_sigSet = false, m_xdgSet = false;
private Q_SLOTS:
    void init()
    {
        m_sigSet = qEnvironmentVariableIsSet("HYPRLAND_INSTANCE_SIGNATURE");
        m_xdgSet = qEnvironmentVariableIsSet("XDG_RUNTIME_DIR");
        m_sig = qgetenv("HYPRLAND_INSTANCE_SIGNATURE");
        m_xdg = qgetenv("XDG_RUNTIME_DIR");
        qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
    }
    void cleanup()
    {
        if (m_sigSet) qputenv("HYPRLAND_INSTANCE_SIGNATURE", m_sig); else qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
        if (m_xdgSet) qputenv("XDG_RUNTIME_DIR", m_xdg); else qunsetenv("XDG_RUNTIME_DIR");
    }

    void parseFixture()
    {
        auto l = parseMonitors(fixture);
        QCOMPARE(l.size(), 2);
        QCOMPARE(l[0].name, QString("eDP-2"));
        QVERIFY(l[0].focused);
        QCOMPARE(l[0].modePx, QSize(2560, 1600));
        QCOMPARE(l[0].scale, 1.25);
        QCOMPARE(l[0].transform, 0);
        QCOMPARE(l[0].reserved[1], 45);
        QCOMPARE(l[0].reserved[0] + l[0].reserved[2] + l[0].reserved[3], 0);
        QCOMPARE(l[1].name, QString("DP-1"));
        QVERIFY(!l[1].focused);
        QCOMPARE(l[1].transform, 1);
    }
    void parseReservedOrder()
    {
        auto l = parseMonitors(R"([{"name":"a","reserved":[1,2,3,4]}])");
        QCOMPARE(l.size(), 1);
        QCOMPARE(l[0].reserved[0], 1);
        QCOMPARE(l[0].reserved[1], 2);
        QCOMPARE(l[0].reserved[2], 3);
        QCOMPARE(l[0].reserved[3], 4);
        QCOMPARE(l[0].scale, 1.0);
        QCOMPARE(l[0].transform, 0);
        QVERIFY(!l[0].focused);
    }
    void parseInvalid()
    {
        QVERIFY(parseMonitors("not json").isEmpty());
        QVERIFY(parseMonitors("").isEmpty());
        QVERIFY(parseMonitors("{}").isEmpty());
    }
    void toMonitorValues()
    {
        auto l = parseMonitors(fixture);
        Monitor a = toMonitor(l[0]);
        QCOMPARE(a.name, QString("eDP-2"));
        QCOMPARE(a.work, QSize(2048, 1235));
        QCOMPARE(a.scale, 1.25);
        Monitor b = toMonitor(l[1]);
        QCOMPARE(b.work, QSize(1080, 1920));
        HyprMonitor h; h.modePx = {1000, 500}; h.scale = 0; h.reserved[0] = 10; h.transform = 3;
        QCOMPARE(toMonitor(h).work, QSize(490, 1000)); // scale <= 0 treated as 1, swapped, left reserve
    }
    void socketPathEmptyWithoutSignature()
    {
        QVERIFY(socketPath().isEmpty());
        QVERIFY(request("j/monitors").isEmpty());
    }
    void noSocketGivesFallback()
    {
        QTemporaryDir d;
        QDir().mkpath(d.path() + "/hypr/nosock");
        qputenv("XDG_RUNTIME_DIR", d.path().toUtf8());
        qputenv("HYPRLAND_INSTANCE_SIGNATURE", "nosock");
        QVERIFY(!focusedMonitor());
        Monitor fb{"fb", {800, 600}, 1.0};
        Monitor m = currentMonitor("DP-1", fb);
        QCOMPARE(m.name, fb.name);
        QCOMPARE(m.work, fb.work);
    }
    void fakeServer()
    {
        Fake f;
        QCOMPARE(socketPath(), f.dir.path() + "/hypr/sig1/.socket.sock");
        auto fm = focusedMonitor();
        QVERIFY(fm);
        QCOMPARE(fm->name, QString("eDP-2"));
        QCOMPARE(fm->work, QSize(2048, 1235));
        auto dp = monitorNamed("DP-1");
        QVERIFY(dp);
        QCOMPARE(dp->work, QSize(1080, 1920));
        QVERIFY(!monitorNamed("nope"));
        Monitor fb{"fb", {800, 600}, 1.0};
        QCOMPARE(currentMonitor("DP-1", fb).name, QString("DP-1"));
        QCOMPARE(currentMonitor("HDMI-9", fb).name, QString("eDP-2"));
        QCOMPARE(currentMonitor("", fb).name, QString("eDP-2"));
        QCOMPARE(f.srv->requests.load(), 6);
    }
    void requestFromWorkerThread()
    {
        Fake f;
        QByteArray got;
        std::unique_ptr<QThread> t(QThread::create([&] { got = request("j/monitors"); }));
        t->start();
        QVERIFY(t->wait(5000));
        QCOMPARE(got, fixture);
    }
    void silentServerTimesOut_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("silent") << int(FakeServer::Silent);
        QTest::newRow("half") << int(FakeServer::HalfAnswer);
    }
    void silentServerTimesOut()
    {
        QFETCH(int, mode);
        Fake f{FakeServer::Mode(mode)};
        QElapsedTimer t; t.start();
        QVERIFY(request("j/monitors", 150).isEmpty());
        QVERIFY2(t.elapsed() >= 100 && t.elapsed() < 1000, qPrintable(QString::number(t.elapsed())));
    }
    void screenMonitorUsesPrimaryScreen()
    {
        Monitor m = screenMonitor();
        QVERIFY(m.work.isValid());
        QCOMPARE(m.scale, 1.0);
    }
};

QTEST_MAIN(TstHyprland)
#include "tst_hyprland.moc"
