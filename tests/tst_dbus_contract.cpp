#include "PreviewerService.h"
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QDir>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <csignal>
#include <sys/prctl.h>

using namespace ql;
using PS = PreviewerService;

// Receiver for signals seen on the client connection.
class Sink : public QObject {
    Q_OBJECT
public Q_SLOTS:
    void props(const QDBusMessage &m) { propsMsgs << m; }
    void sel(const QDBusMessage &m) { selMsgs << m; }
    void sentinel() { ++sentinels; }
public:
    QList<QDBusMessage> propsMsgs, selMsgs;
    int sentinels = 0;
};

class TstDbusContract : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;
    QProcess m_bus;
    QString m_addr;
    Session m_session;
    PS *m_svc = nullptr;
    Sink m_sink;

    QDBusConnection cli() { return QDBusConnection(QStringLiteral("cli")); }
    QDBusMessage call(const QString &iface, const QString &method, const QList<QVariant> &args = {},
                      const QString &conn = QStringLiteral("cli"), const QString &path = PS::kPath)
    {
        auto m = QDBusMessage::createMethodCall(PS::kService, path, iface, method);
        m.setArguments(args);
        return QDBusConnection(conn).call(m, QDBus::BlockWithGui, 5000);
    }
    void show(const QString &uri, bool close, const QString &h = {}, const QString &conn = QStringLiteral("cli"))
    {
        auto r = call(PS::kInterface, "ShowFile", {uri, h, close, QString()}, conn);
        QCOMPARE(r.type(), QDBusMessage::ReplyMessage);
    }
    // Round-trip through the service's bus: everything sent before has been delivered to cli.
    void flush()
    {
        const int n = m_sink.sentinels;
        QDBusConnection svc(QStringLiteral("svc"));
        svc.send(QDBusMessage::createSignal("/sentinel", "ql.Sentinel", "Ping"));
        QTRY_VERIFY(m_sink.sentinels > n);
    }
    QVariant prop(const char *name)
    {
        auto r = call("org.freedesktop.DBus.Properties", "Get", {PS::kInterface, QString::fromLatin1(name)});
        return r.arguments().value(0).value<QDBusVariant>().variant();
    }

private Q_SLOTS:
    void initTestCase()
    {
        // No service dirs: a call to an unowned name must never auto-start anything (e.g. real Sushi).
        QFile f(m_dir.filePath("bus.conf"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("<busconfig><type>session</type><listen>unix:tmpdir=/tmp</listen>"
                "<policy context=\"default\"><allow send_destination=\"*\" eavesdrop=\"true\"/>"
                "<allow receive_sender=\"*\"/><allow own=\"*\"/><allow user=\"*\"/></policy></busconfig>");
        f.close();
        m_bus.setProgram("dbus-daemon");
        m_bus.setArguments({"--config-file=" + f.fileName(), "--nofork", "--print-address=1", "--nopidfile"});
        m_bus.setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGKILL); });
        m_bus.start();
        QVERIFY(m_bus.waitForStarted());
        QVERIFY(m_bus.waitForReadyRead(5000));
        m_addr = QString::fromLocal8Bit(m_bus.readLine()).trimmed();
        QVERIFY(!m_addr.isEmpty());

        auto svc = QDBusConnection::connectToBus(m_addr, "svc");
        auto cl = QDBusConnection::connectToBus(m_addr, "cli");
        QVERIFY(svc.isConnected() && cl.isConnected());
        m_svc = new PS(&m_session, svc, this);
        QVERIFY(m_svc->registerOnBus());

        QVERIFY(cl.connect(PS::kService, PS::kPath, "org.freedesktop.DBus.Properties", "PropertiesChanged", &m_sink,
                           SLOT(props(QDBusMessage))));
        QVERIFY(cl.connect(PS::kService, PS::kPath, PS::kInterface, "SelectionEvent", &m_sink, SLOT(sel(QDBusMessage))));
        QVERIFY(cl.connect({}, "/sentinel", "ql.Sentinel", "Ping", &m_sink, SLOT(sentinel())));
    }
    void cleanupTestCase()
    {
        m_bus.kill();
        m_bus.waitForFinished();
    }

    void nameTakenOnSecondService()
    {
        Session s2;
        PS other(&s2, QDBusConnection::connectToBus(m_addr, "svc2"));
        QVERIFY(!other.registerOnBus());
    }
    void introspection()
    {
        auto xml = call("org.freedesktop.DBus.Introspectable", "Introspect").arguments().value(0).toString();
        QVERIFY(xml.contains("org.gnome.NautilusPreviewer2"));
        QRegularExpression re(R"(<method name="ShowFile">\s*<arg direction="in" type="s" name="uri"/>\s*<arg direction="in" type="s" name="windowHandle"/>\s*<arg direction="in" type="b" name="closeIfAlreadyShown"/>\s*<arg direction="in" type="s" name="activationToken"/>)");
        QVERIFY2(re.match(xml).hasMatch(), qPrintable(xml));
        QVERIFY(xml.contains("<method name=\"Close\"/>") || xml.contains("<method name=\"Close\">"));
        QVERIFY(xml.contains("name=\"Visible\""));
        QVERIFY(xml.contains("name=\"ParentHandle\""));
        QVERIFY(xml.contains("<signal name=\"SelectionEvent\">"));
        QRegularExpression sig(R"(<signal name="SelectionEvent">\s*<arg type="u")");
        QVERIFY2(sig.match(xml).hasMatch(), qPrintable(xml));
    }
    void showOpensAndNotifies()
    {
        QSignalSpy decided(m_svc, &PS::decided);
        m_sink.propsMsgs.clear();
        show("file:///a", true);
        flush();
        QCOMPARE(decided.size(), 1);
        QCOMPARE(m_sink.propsMsgs.size(), 1);
        auto a = m_sink.propsMsgs[0].arguments();
        QCOMPARE(a[0].toString(), "org.gnome.NautilusPreviewer2");
        QVariantMap changed;
        a[1].value<QDBusArgument>() >> changed;
        QCOMPARE(changed, (QVariantMap{{"Visible", true}}));
        QVERIFY(a[2].toStringList().isEmpty());
        QCOMPARE(prop("Visible").toBool(), true);
        auto all = call("org.freedesktop.DBus.Properties", "GetAll", {PS::kInterface}).arguments()[0];
        QVariantMap m;
        all.value<QDBusArgument>() >> m;
        QVERIFY(m.contains("Visible") && m.contains("ParentHandle"));
    }
    void parentHandle()
    {
        show("file:///a", false, "wayland:abc");
        QCOMPARE(prop("ParentHandle").toString(), "wayland:abc");
        show("file:///a", false, "wayland:(null)");
        QCOMPARE(prop("ParentHandle").toString(), "");
        show("file:///a", false, "x11:12");
        QCOMPARE(prop("ParentHandle").toString(), "");
    }
    void selectionEvent()
    {
        m_sink.selMsgs.clear();
        QSignalSpy mirror(m_svc, &PS::selectionEvent);
        m_svc->emitSelectionEvent(0);
        m_svc->emitSelectionEvent(1);
        m_svc->emitSelectionEvent(6);
        m_svc->emitSelectionEvent(5);
        flush();
        QCOMPARE(m_sink.selMsgs.size(), 1);
        QCOMPARE(m_sink.selMsgs[0].signature(), "u");
        QCOMPARE(m_sink.selMsgs[0].arguments()[0].toUInt(), 5u);
        QCOMPARE(mirror.size(), 1);
    }
    void sameUriClosesThenFollowIgnored()
    {
        QSignalSpy decided(m_svc, &PS::decided);
        show("file:///a", true);
        QCOMPARE(decided.size(), 1);
        QCOMPARE(prop("Visible").toBool(), false);
        show("file:///b", false);   // hidden follow update
        QCOMPARE(decided.size(), 1);
        QCOMPARE(prop("Visible").toBool(), false);
        QCOMPARE(call(PS::kInterface, "Close").type(), QDBusMessage::ReplyMessage);
        QCOMPARE(decided.size(), 1);
    }
    void senderVanishing()
    {
        QSignalSpy decided(m_svc, &PS::decided);
        // Scoped: a live QDBusConnection copy would keep the bus connection open.
        {
            QVERIFY(QDBusConnection::connectToBus(m_addr, "tmp").isConnected());
            show("file:///t", true, {}, "tmp");
        }
        QCOMPARE(prop("Visible").toBool(), true);
        decided.clear();
        QDBusConnection::disconnectFromBus("tmp");
        QTRY_COMPARE_WITH_TIMEOUT(decided.size(), 1, 2000);
        QCOMPARE(decided[0][0].value<Session::Decision>().effect, Session::Effect::Close);
        QCOMPARE(prop("Visible").toBool(), false);
    }
};

QTEST_MAIN(TstDbusContract)
#include "tst_dbus_contract.moc"
