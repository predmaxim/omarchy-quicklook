#include <QtTest>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QWindow>
#include "Keys.h"

using namespace ql;

class TstKeys : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void mapKeyTable_data()
    {
        QTest::addColumn<int>("key");
        QTest::addColumn<uint>("scan");
        QTest::addColumn<int>("mods");
        QTest::addColumn<int>("expected");
        auto row = [](const char *n, int k, uint s, int m, KeyAction a) {
            QTest::newRow(n) << k << s << m << int(a);
        };
        const int none = Qt::NoModifier;
        row("space", Qt::Key_Space, 65, none, KeyAction::Close);
        row("shift+space", Qt::Key_Space, 65, Qt::ShiftModifier, KeyAction::Close);
        row("escape", Qt::Key_Escape, 9, none, KeyAction::Close);
        row("Q", Qt::Key_Q, 24, none, KeyAction::Close);
        row("return", Qt::Key_Return, 36, none, KeyAction::Open);
        row("enter", Qt::Key_Enter, 104, Qt::KeypadModifier, KeyAction::Open);
        row("left", Qt::Key_Left, 113, none, KeyAction::Left);
        row("right", Qt::Key_Right, 114, none, KeyAction::Right);
        row("up", Qt::Key_Up, 111, none, KeyAction::Up);
        row("down", Qt::Key_Down, 116, none, KeyAction::Down);
        row("pgup", Qt::Key_PageUp, 112, none, KeyAction::PageUp);
        row("pgdn", Qt::Key_PageDown, 117, none, KeyAction::PageDown);
        row("home", Qt::Key_Home, 110, none, KeyAction::Home);
        row("end", Qt::Key_End, 115, none, KeyAction::End);
        row("F", Qt::Key_F, 41, none, KeyAction::FullScreen);
        row("F11", Qt::Key_F11, 95, none, KeyAction::FullScreen);
        row("K", Qt::Key_K, 45, none, KeyAction::PlayPause);
        row("P", Qt::Key_P, 33, none, KeyAction::PlayPause);
        row("J", Qt::Key_J, 44, none, KeyAction::SeekBack);
        row("L", Qt::Key_L, 46, none, KeyAction::SeekForward);
        row("M", Qt::Key_M, 58, none, KeyAction::Mute);
        row("latin wins over scan", Qt::Key_A, 24, none, KeyAction::None);
        row("ru Q (Y)", u'Й', 24, none, KeyAction::Close);
        row("ru F (A)", u'А', 41, none, KeyAction::FullScreen);
        row("ru K (L)", u'Л', 45, none, KeyAction::PlayPause);
        row("ru P (Z)", u'З', 33, none, KeyAction::PlayPause);
        row("ru J (O)", u'О', 44, none, KeyAction::SeekBack);
        row("ru L (D)", u'Д', 46, none, KeyAction::SeekForward);
        row("ru M (soft)", u'Ь', 58, none, KeyAction::Mute);
        row("ru other scan", u'Ы', 28, none, KeyAction::None);
        row("punctuation not by scan", Qt::Key_Apostrophe, 24, none, KeyAction::None);
        row("ctrl+Q", Qt::Key_Q, 24, Qt::ControlModifier, KeyAction::None);
        row("alt+space", Qt::Key_Space, 65, Qt::AltModifier, KeyAction::None);
        row("meta+left", Qt::Key_Left, 113, Qt::MetaModifier, KeyAction::None);
        row("ctrl+ru Q", u'Й', 24, Qt::ControlModifier, KeyAction::None);
        row("unknown", Qt::Key_F5, 71, none, KeyAction::None);
    }
    void mapKeyTable()
    {
        QFETCH(int, key); QFETCH(uint, scan); QFETCH(int, mods); QFETCH(int, expected);
        QCOMPARE(int(mapKey(key, scan, Qt::KeyboardModifiers(mods))), expected);
    }
    void directions()
    {
        QCOMPARE(selectionDirection(KeyAction::Up), 2u);
        QCOMPARE(selectionDirection(KeyAction::Down), 3u);
        QCOMPARE(selectionDirection(KeyAction::Left), 4u);
        QCOMPARE(selectionDirection(KeyAction::Right), 5u);
        QCOMPARE(selectionDirection(KeyAction::Close), 0u);
        QCOMPARE(selectionDirection(KeyAction::None), 0u);
    }
    void routerKeys()
    {
        QWindow w; w.create();
        KeyRouter r; w.installEventFilter(&r);
        QList<KeyAction> got;
        connect(&r, &KeyRouter::action, this, [&](KeyAction a) { got << a; });
        auto send = [&](int k, quint32 scan, bool rep) {
            QKeyEvent e(QEvent::KeyPress, k, Qt::NoModifier, scan, 0, 0, QString(), rep);
            return r.eventFilter(&w, &e);   // true = consumed
        };
        QVERIFY(send(Qt::Key_Right, 114, false));
        QCOMPARE(got, QList<KeyAction>{KeyAction::Right});
        got.clear();
        QKeyEvent viaWindow(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);   // real delivery path
        QCoreApplication::sendEvent(&w, &viaWindow);
        QCOMPARE(got, QList<KeyAction>{KeyAction::Down});
        got.clear();
        QVERIFY(send(Qt::Key_Space, 65, true));   // consumed, no action
        QVERIFY(got.isEmpty());
        QVERIFY(send(Qt::Key_M, 58, true));
        QVERIFY(got.isEmpty());
        QVERIFY(send(Qt::Key_Left, 113, true));   // arrows repeat
        QCOMPARE(got, QList<KeyAction>{KeyAction::Left});
        got.clear();
        QVERIFY(!send(Qt::Key_A, 38, false));
        QVERIFY(got.isEmpty());
    }
    void routerKeepsWindowOnClose()
    {
        QWindow w; w.create();
        KeyRouter r; w.installEventFilter(&r);
        int n = 0;
        connect(&r, &KeyRouter::closeRequested, this, [&] { ++n; });
        QCloseEvent e;
        QCoreApplication::sendEvent(&w, &e);
        QCOMPARE(n, 1);
        QVERIFY(w.handle() != nullptr);
        QVERIFY(w.close());   // accepted: Quit is not vetoed
        QCOMPARE(n, 2);
        QVERIFY(w.handle() != nullptr);
    }
};

QTEST_MAIN(TstKeys)
#include "tst_keys.moc"
