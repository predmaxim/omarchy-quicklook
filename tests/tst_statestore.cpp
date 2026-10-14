#include "StateStore.h"
#include <QTemporaryDir>
#include <QTest>

using namespace ql;

class TstStateStore : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void roundTrip()
    {
        QTemporaryDir dir;
        const QString f = dir.filePath(QStringLiteral("sub/state.ini"));
        QVERIFY(!StateStore(f).muted());
        StateStore(f).setMuted(true);
        QVERIFY(StateStore(f).muted());
        StateStore(f).setMuted(false);
        QVERIFY(!StateStore(f).muted());
    }
    void defaultPath()
    {
        QVERIFY(StateStore::defaultPath().endsWith(QStringLiteral("/omarchy-quicklook/state.ini")));
    }
};

QTEST_APPLESS_MAIN(TstStateStore)
#include "tst_statestore.moc"
