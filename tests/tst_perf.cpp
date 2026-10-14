#include "Controller.h"
#include "PreviewerService.h"
#include "Session.h"
#include "StateStore.h"
#include <KZip>
#include <QDBusConnection>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QLoggingCategory>
#include <QPainter>
#include <QPdfWriter>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrl>
#include <algorithm>

// Label "perf", run by hand: ctest --test-dir build -L perf --output-on-failure -V
// spec "Проверка": ShowFile -> "готово к показу" < 100 ms on every type (office cache miss included: the window
// with the placeholder), GUI-thread pause <= 16 ms while flipping through images.
// Offscreen, software Quick, no D-Bus, muted, no audio server reachable; media is closed before autoplay.
using namespace ql;
using namespace Qt::StringLiterals;

namespace {
constexpr int kRuns = 5;
constexpr qint64 kShowBudgetMs = 100;
constexpr qint64 kMaxGapMs = 16;

QtMessageHandler g_testLog = nullptr;
void dropSizeHintNoise(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (message != QLatin1String("This plugin does not support propagateSizeHints()"))
        g_testLog(type, context, message);
}
bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}
bool writeImage(const QString &path, QSize size, const char *format)
{
    QImage img(size, QImage::Format_RGB32);
    QPainter p(&img);   // a gradient with detail: realistic decode cost, unlike a flat fill
    QLinearGradient g(0, 0, size.width(), size.height());
    g.setColorAt(0, Qt::darkBlue);
    g.setColorAt(1, Qt::yellow);
    p.fillRect(img.rect(), g);
    for (int i = 0; i < size.width(); i += 37)
        p.drawLine(i, 0, size.width() - i, size.height());
    p.end();
    return img.save(path, format, 90);
}
bool ffmpeg(const QStringList &args)
{
    QProcess p;
    p.start(u"ffmpeg"_s, QStringList{u"-v"_s, u"error"_s, u"-y"_s} + args);
    return p.waitForFinished(60000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}
} // namespace

class TstPerf : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;
    Session m_session;
    std::unique_ptr<PreviewerService> m_service;
    std::unique_ptr<Controller> m_controller;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow *m_window = nullptr;

    QString file(const QString &name) const { return m_dir.filePath(name); }
    qint64 showOnce(const QString &path)   // ms from ShowFile to shown(), -1 on timeout; closed again afterwards
    {
        QElapsedTimer clock;
        qint64 elapsed = -1;
        auto c = connect(m_controller.get(), &Controller::shown, this, [&] { elapsed = clock.elapsed(); });
        clock.start();
        m_service->handleShowFile(QUrl::fromLocalFile(path).toString(), QString(), true, QString());
        while (elapsed < 0 && clock.elapsed() < 2000)
            QTest::qWait(1);
        disconnect(c);
        m_service->handleClose();   // before autoplay (300 ms): no player ever runs
        QTest::qWait(30);
        return elapsed;
    }
    void measure(const QString &path, const QString &expectedKind)
    {
        QList<qint64> times;
        QString kind;
        for (int i = 0; i < kRuns; ++i) {
            auto c = connect(m_controller.get(), &Controller::shown, this, [&] { kind = m_controller->kind(); });
            times << showOnce(path);
            disconnect(c);
            QCOMPARE(kind, expectedKind);
        }
        std::sort(times.begin(), times.end());
        qInfo("%-6s median %lld ms, max %lld ms (%s)", qPrintable(expectedKind), times.at(kRuns / 2), times.last(),
              qPrintable(QFileInfo(path).fileName()));
        QVERIFY(times.first() >= 0);
        QVERIFY2(times.last() < kShowBudgetMs, qPrintable(u"%1 ms"_s.arg(times.last())));
    }

private Q_SLOTS:
    void initTestCase()
    {
        g_testLog = qInstallMessageHandler(dropSizeHintNoise);
        QVERIFY(m_dir.isValid());
        StateStore().setMuted(true);   // video gets no audio output at all
        // office cache miss: the conversion itself is not measured, a stand-in soffice fails at once
        const QString bin = file(u"bin"_s);
        QVERIFY(QDir().mkpath(bin));
        QVERIFY(writeFile(bin + u"/soffice"_s, "#!/bin/sh\nexit 1\n"));
        QFile(bin + u"/soffice"_s).setPermissions(QFile::ReadOwner | QFile::ExeOwner);
        qputenv("PATH", (bin + u':').toLocal8Bit() + qgetenv("PATH"));

        QVERIFY(writeImage(file(u"big.png"_s), QSize(4000, 3000), "PNG"));
        QVERIFY(writeImage(file(u"big.jpg"_s), QSize(4000, 3000), "JPEG"));
        QVERIFY(writeImage(file(u"hd.png"_s), QSize(1920, 1080), "PNG"));
        QVERIFY(writeFile(file(u"pic.svg"_s), "<svg xmlns='http://www.w3.org/2000/svg' width='800' height='600'>"
                                             "<circle cx='400' cy='300' r='250' fill='teal'/></svg>"));
        QByteArray text;
        for (int i = 0; text.size() < 1024 * 1024; ++i)
            text += "line " + QByteArray::number(i) + ": the quick brown fox jumps over the lazy dog\n";
        QVERIFY(writeFile(file(u"big.txt"_s), text));
        {
            QPdfWriter writer(file(u"ten.pdf"_s));
            writer.setPageSize(QPageSize(QPageSize::A4));
            QPainter painter(&writer);
            for (int i = 0; i < 10; ++i) {
                if (i)
                    writer.newPage();
                painter.drawText(QPointF(500, 500), u"page %1"_s.arg(i + 1));
            }
        }
        QVERIFY(QDir().mkpath(file(u"folder"_s)));
        for (int i = 0; i < 50; ++i)
            QVERIFY(writeFile(file(u"folder/f%1"_s.arg(i)), "x"));
        {
            KZip zip(file(u"memo.docx"_s));
            QVERIFY(zip.open(QIODevice::WriteOnly));
            zip.writeFile(u"[Content_Types].xml"_s, QByteArray("<Types/>"));
            zip.writeFile(u"word/document.xml"_s,
                          QByteArray("<w:document xmlns:w='x'><w:body><w:sectPr><w:pgSz w:w='11906' w:h='16838'/>"
                                     "</w:sectPr></w:body></w:document>"));
            zip.close();
        }

        m_service = std::make_unique<PreviewerService>(&m_session, QDBusConnection(u"none"_s));
        m_controller = std::make_unique<Controller>(m_service.get(), &m_session, QProcessEnvironment::systemEnvironment());
        m_engine = std::make_unique<QQmlApplicationEngine>();
        m_controller->registerImageProviders(m_engine.get());
        m_engine->rootContext()->setContextProperty(u"ql"_s, m_controller.get());
        m_engine->load(QUrl(u"qrc:/qml/Main.qml"_s));
        m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().value(0));
        QVERIFY(m_window);
        m_controller->attachWindow(m_window);
        m_controller->warmUp();   // as main() does in idle after start
    }
    void cleanupTestCase()
    {
        m_engine.reset();
        m_controller.reset();
        m_service.reset();
    }

    void showFile_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("kind");
        QTest::newRow("png 4000x3000") << u"big.png"_s << u"image"_s;
        QTest::newRow("jpeg 4000x3000") << u"big.jpg"_s << u"image"_s;
        QTest::newRow("svg") << u"pic.svg"_s << u"svg"_s;
        QTest::newRow("text 1 MiB") << u"big.txt"_s << u"text"_s;
        QTest::newRow("pdf 10 pages") << u"ten.pdf"_s << u"pdf"_s;
        QTest::newRow("folder") << u"folder"_s << u"folder"_s;
        QTest::newRow("docx cache miss") << u"memo.docx"_s << u"office"_s;
    }
    void showFile()
    {
        QFETCH(QString, name);
        QFETCH(QString, kind);
        measure(file(name), kind);
    }
    void showMedia_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QStringList>("args");
        QTest::newRow("mp4 1080p") << u"clip.mp4"_s << u"video"_s
            << QStringList{u"-f"_s, u"lavfi"_s, u"-i"_s, u"testsrc=size=1920x1080:rate=25:duration=2"_s,
                           u"-c:v"_s, u"libx264"_s, u"-pix_fmt"_s, u"yuv420p"_s};
        QTest::newRow("wav") << u"tone.wav"_s << u"audio"_s
            << QStringList{u"-f"_s, u"lavfi"_s, u"-i"_s, u"anullsrc=r=44100:cl=stereo"_s, u"-t"_s, u"2"_s};
    }
    void showMedia()
    {
        if (QStandardPaths::findExecutable(u"ffmpeg"_s).isEmpty())
            QSKIP("ffmpeg missing");
        QFETCH(QString, name);
        QFETCH(QString, kind);
        QFETCH(QStringList, args);
        QVERIFY(ffmpeg(args << file(name)));
        measure(file(name), kind);
    }
    // Flipping through images (a held arrow): the GUI thread never stalls for more than a frame.
    void burstGaps()
    {
        const QStringList images{file(u"big.png"_s), file(u"big.jpg"_s), file(u"hd.png"_s)};
        QElapsedTimer clock;
        qint64 last = 0, maxGap = 0;
        QTimer tick;
        tick.setTimerType(Qt::PreciseTimer);
        tick.setInterval(1);
        connect(&tick, &QTimer::timeout, this, [&] {
            const qint64 now = clock.elapsed();
            maxGap = std::max(maxGap, now - last);
            last = now;
        });
        QSignalSpy shown(m_controller.get(), &Controller::shown);
        clock.start();
        tick.start();
        for (int i = 0; i < 30; ++i) {
            m_service->handleShowFile(QUrl::fromLocalFile(images.at(i % images.size())).toString(), QString(), i == 0,
                                      QString());
            QTest::qWait(60);   // every file gets shown (the 40 ms grace passes): each switch is a full pass
        }
        QTest::qWait(500);   // the last file's decode lands
        tick.stop();
        m_service->handleClose();
        qInfo("burst: 30 images, %lld shown, max GUI gap %lld ms", qint64(shown.count()), maxGap);
        QVERIFY(shown.count() >= 25);   // the grace bounds the wait since the first unshown switch: about every one
        QVERIFY2(maxGap <= kMaxGapMs, qPrintable(u"%1 ms"_s.arg(maxGap)));
    }
};

int main(int argc, char **argv)
{
    qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", ",");   // software decoding: never wake NVIDIA
    qputenv("QT_FFMPEG_ENCODING_HW_DEVICE_TYPES", ",");
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QPA_OFFSCREEN_NO_GLX", "1");   // the offscreen plugin would open GLX on $DISPLAY
    qputenv("QT_QUICK_BACKEND", "software");
    qputenv("GIO_USE_VFS", "local");   // GVfs would connect to the session bus on the first GIO call (task 6)
    qputenv("PULSE_SERVER", "unix:/nonexistent");   // belt and braces: no audio output is ever created
    qputenv("PIPEWIRE_REMOTE", "/nonexistent");
    qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
    for (const char *name : {"XDG_CACHE_HOME", "XDG_STATE_HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME"}) {
        if (!qgetenv(name).contains("/test-home/")) {
            fprintf(stderr, "%s must point into build/test-home: run through ctest\n", name);
            return 2;
        }
    }
    QGuiApplication app(argc, argv);
    QLoggingCategory::setFilterRules(u"qt.multimedia.*=false"_s);
    QIcon::setThemeSearchPaths({u"/usr/share/icons"_s});
    TstPerf t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_perf.moc"
