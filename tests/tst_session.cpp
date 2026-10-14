#include "Session.h"
#include <QSignalSpy>
#include <QTest>

using namespace ql;

class TstSession : public QObject {
    Q_OBJECT
    static QString str(const Session::Decision &d)
    {
        static const char *names[] = {"None", "Open", "Switch", "Close"};
        return QStringLiteral("%1 [%2] g%3%4").arg(QLatin1String(names[int(d.effect)]), d.uri).arg(d.generation).arg(d.takeFocus ? " focus" : "");
    }
    static QString str(const Session &s)
    {
        return QStringLiteral("%1 [%2] g%3").arg(s.visible() ? "shown" : "hidden", s.uri()).arg(s.generation());
    }
private Q_SLOTS:
    void hiddenOpenFocus()
    {
        Session s;
        QCOMPARE(str(s.showFile("file:///a", true)), "Open [file:///a] g1 focus");
        QCOMPARE(str(s), "shown [file:///a] g1");
    }
    void hiddenFollowIgnored()
    {
        Session s;
        QCOMPARE(str(s.showFile("file:///a", false)), "None [] g0");
        QCOMPARE(str(s), "hidden [] g0");
    }
    void sameToggleCloses()
    {
        Session s;
        s.showFile("file:///a", true);
        QCOMPARE(str(s.showFile("file:///a", true)), "Close [] g2");
        QCOMPARE(str(s), "hidden [] g2");
    }
    void sameFollowNone()
    {
        Session s;
        s.showFile("file:///a", true);
        QCOMPARE(str(s.showFile("file:///a", false)), "None [file:///a] g1");
    }
    void otherSwitchesNoFocus()
    {
        Session s;
        s.showFile("file:///a", true);
        QCOMPARE(str(s.showFile("file:///b", false)), "Switch [file:///b] g2");
        QCOMPARE(str(s.showFile("file:///c", true)), "Switch [file:///c] g3");
        QCOMPARE(str(s), "shown [file:///c] g3");
    }
    void closeShownAndHidden()
    {
        Session s;
        QCOMPARE(str(s.close()), "None [] g0");
        s.showFile("file:///a", true);
        QCOMPARE(str(s.close()), "Close [] g2");
        QCOMPARE(str(s.close()), "None [] g2");
    }
    void visibleChangedOnlyOnChange()
    {
        Session s;
        QSignalSpy spy(&s, &Session::visibleChanged);
        s.showFile("a", false);
        s.close();
        QCOMPARE(spy.count(), 0);
        s.showFile("a", true);
        s.showFile("b", false);
        s.showFile("b", false);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), true);
        s.close();
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(0).toBool(), false);
    }
    void slotSeesNewState()
    {
        Session s;
        QString seen;
        connect(&s, &Session::visibleChanged, this, [&] { seen = str(s); });
        s.showFile("a", true);
        QCOMPARE(seen, "shown [a] g1");
        s.close();
        QCOMPARE(seen, "hidden [] g2");
    }
    void batchOpenSwitch()
    {
        Session s;
        QCOMPARE(str(s.showFile("A", true)), "Open [A] g1 focus");
        QCOMPARE(str(s.showFile("B", false)), "Switch [B] g2");
        QCOMPARE(str(s), "shown [B] g2");
    }
    void batchOpenClose()
    {
        Session s;
        s.showFile("A", true);
        QCOMPARE(str(s.showFile("A", true)), "Close [] g2");
        QCOMPARE(str(s), "hidden [] g2");
    }
    void batchOpenThenClose()
    {
        Session s;
        s.showFile("A", true);
        QCOMPARE(str(s.close()), "Close [] g2");
        QCOMPARE(str(s), "hidden [] g2");
    }
};

QTEST_GUILESS_MAIN(TstSession)
#include "tst_session.moc"
