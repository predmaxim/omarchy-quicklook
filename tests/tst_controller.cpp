#include "Controller.h"
#include "Geometry.h"
#include "OfficeCache.h"
#include "PreviewerService.h"
#include "Session.h"
#include "Types.h"
#include <QAbstractItemModel>
#include <QCloseEvent>
#include <QDBusConnection>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QLoggingCategory>
#include <QPainter>
#include <QPdfWriter>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <fcntl.h>
#include <unistd.h>

// Offscreen, software Quick, no D-Bus (a disconnected connection: calls go through handleShowFile/handleClose),
// no media. XDG dirs come from ql_test() (build/test-home): main() refuses to run otherwise.
using namespace ql;
using namespace Qt::StringLiterals;

namespace {
QtMessageHandler g_testLog = nullptr;
// The offscreen plugin warns on every min/max size change; nothing else is dropped.
void dropSizeHintNoise(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (message != QLatin1String("This plugin does not support propagateSizeHints()"))
        g_testLog(type, context, message);
}
struct StderrMute {   // FFmpeg's av_log reports a garbage file straight to stderr
    int saved = dup(2);
    StderrMute() { if (const int n = open("/dev/null", O_WRONLY); n >= 0) { dup2(n, 2); close(n); } }
    ~StderrMute() { dup2(saved, 2); close(saved); }
};
bool writeDetailedPng(const QString &path, QSize size)   // costly to decode: well past the 40 ms grace
{
    QImage img(size, QImage::Format_RGB32);
    QPainter p(&img);
    QLinearGradient g(0, 0, size.width(), size.height());
    g.setColorAt(0, Qt::darkBlue);
    g.setColorAt(1, Qt::yellow);
    p.fillRect(img.rect(), g);
    for (int i = 0; i < size.width(); i += 37)
        p.drawLine(i, 0, size.width() - i, size.height());
    p.end();
    return img.save(path, "PNG");
}
bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}
bool writePng(const QString &path, QSize size)
{
    QImage img(size, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    return img.save(path, "PNG");
}
QQuickItem *itemAt(QQuickItem *list, int index)
{
    QQuickItem *item = nullptr;
    if (list)
        QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, item), Q_ARG(int, index));
    return item;
}
} // namespace

class TstController : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;
    Session m_session;
    std::unique_ptr<PreviewerService> m_service;
    std::unique_ptr<Controller> m_controller;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow *m_window = nullptr;
    QString m_launchLog;

    QString file(const QString &name) const { return m_dir.filePath(name); }
    void request(const QString &path, bool close = true)
    {
        m_service->handleShowFile(QUrl::fromLocalFile(path).toString(), QString(), close, QString());
    }
    quint64 show(const QString &path)   // -> the generation shown, 0 on timeout
    {
        QSignalSpy spy(m_controller.get(), &Controller::shown);
        request(path);
        if (!spy.wait(2000))
            return 0;
        return spy.first().first().toULongLong();
    }

private Q_SLOTS:
    void initTestCase()
    {
        g_testLog = qInstallMessageHandler(dropSizeHintNoise);   // QTest's handler is installed by now
        QVERIFY(m_dir.isValid());
        // Enter: a stub replaces uwsm-app and logs its arguments; a default app for image/png in the test XDG dirs
        const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
        QVERIFY(QDir().mkpath(data + u"/applications"_s) && QDir().mkpath(config));
        QVERIFY(writeFile(data + u"/applications/ql-test-viewer.desktop"_s,
                          "[Desktop Entry]\nType=Application\nName=QL test viewer\nExec=true %f\nMimeType=image/png;\n"));
        QVERIFY(writeFile(config + u"/mimeapps.list"_s, "[Default Applications]\nimage/png=ql-test-viewer.desktop\n"));
        m_launchLog = file(u"launch.log"_s);
        const QString stub = file(u"uwsm-app"_s);
        QVERIFY(writeFile(stub, ("#!/bin/sh\necho \"$*\" > '" + m_launchLog + ".tmp' && mv '" + m_launchLog + ".tmp' '"
                                 + m_launchLog + "'\n").toUtf8()));
        QFile(stub).setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

        m_service = std::make_unique<PreviewerService>(&m_session, QDBusConnection(u"none"_s));
        m_controller = std::make_unique<Controller>(m_service.get(), &m_session, QProcessEnvironment::systemEnvironment());
        m_controller->setLauncherProgram(stub);
        m_engine = std::make_unique<QQmlApplicationEngine>();
        m_controller->registerImageProviders(m_engine.get());
        m_engine->rootContext()->setContextProperty(u"ql"_s, m_controller.get());
        m_engine->load(QUrl(u"qrc:/qml/Main.qml"_s));
        m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().value(0));
        QVERIFY(m_window);
        m_controller->attachWindow(m_window);
        QVERIFY(!m_window->isVisible());

        QVERIFY(writePng(file(u"photo.png"_s), QSize(800, 600)));
        QVERIFY(writePng(file(u"tiny.png"_s), QSize(64, 64)));
        QVERIFY(writeFile(file(u"notes.txt"_s), "alpha\nbeta\ngamma\n"));
        QVERIFY(writeFile(file(u"other.txt"_s), "one\ntwo\n"));
    }
    void cleanupTestCase()
    {
        m_engine.reset();
        m_controller.reset();
        m_service.reset();
    }
    void cleanup()
    {
        if (m_session.visible())
            m_service->handleClose();
        QVERIFY(!m_window->isVisible());
        QCOMPARE(m_controller->kind(), QString());
    }

    void image()
    {
        const quint64 gen = show(file(u"photo.png"_s));
        QVERIFY(gen != 0);
        QCOMPARE(gen, m_session.generation());
        QCOMPARE(m_controller->shownGeneration(), gen);
        QCOMPARE(m_controller->kind(), u"image"_s);
        QVERIFY(m_controller->windowShown());
        const Layout l = m_controller->currentLayout();
        QVERIFY(l.window.isValid());
        QCOMPARE(m_window->minimumSize(), l.window);
        QCOMPARE(m_window->maximumSize(), l.window);
        QCOMPARE(m_window->title(), u"photo.png"_s);
        QCOMPARE(m_controller->contentRect(), l.content);
        QTRY_VERIFY(!m_controller->imageSource().isEmpty());
        auto *main = m_window->findChild<QQuickItem *>(u"mainImage"_s);
        QVERIFY(main);
        QTRY_COMPARE(main->property("status").toInt(), 1);   // Image.Ready
        QCOMPARE(QRectF(main->x(), main->y(), main->width(), main->height()), l.content);
    }
    void smallImageWithinGrace()   // decoded inside the 40 ms grace: the first pass already has it
    {
        QString atShown;
        auto c = connect(m_controller.get(), &Controller::shown, this, [&] { atShown = m_controller->imageSource(); });
        QVERIFY(show(file(u"tiny.png"_s)) != 0);
        disconnect(c);
        QVERIFY(!atShown.isEmpty());
        QCOMPARE(m_controller->currentLayout().window, QSize(240, 240));   // min side, image centered
        QCOMPARE(m_controller->contentRect(), QRectF(88, 88, 64, 64));
    }
    void text()
    {
        QVERIFY(show(file(u"notes.txt"_s)) != 0);
        QCOMPARE(m_controller->kind(), u"text"_s);
        auto *model = qobject_cast<QAbstractItemModel *>(m_controller->textObject());
        QVERIFY(model);
        QCOMPARE(model->rowCount(), 3);
        auto *list = m_window->findChild<QQuickItem *>(u"textList"_s);
        QTRY_VERIFY(itemAt(list, 0));
        QTRY_VERIFY(itemAt(list, 0)->property("text").toString().contains(u"alpha"_s));   // the row, not its index
        QCOMPARE(itemAt(list, 0)->property("width").toReal(), list->width() - 32);
    }
    void pdf()
    {
        const QString path = file(u"doc.pdf"_s);
        {
            QPdfWriter writer(path);
            writer.setPageSize(QPageSize(QPageSize::A4));
            QPainter painter(&writer);
            painter.drawText(QPointF(500, 500), u"one"_s);
            writer.newPage();
            painter.drawText(QPointF(500, 500), u"two"_s);
            writer.newPage();
            painter.drawText(QPointF(500, 500), u"three"_s);
        }
        QVERIFY(show(path) != 0);
        QTRY_COMPARE(m_controller->kind(), u"pdf"_s);
        QCOMPARE(m_controller->pdfObject()->property("count").toInt(), 3);
        const QSize w = m_controller->currentLayout().window;
        QVERIFY(qAbs(double(w.height()) / w.width() - 841.89 / 595.28) < 0.01);
        auto *list = m_window->findChild<QQuickItem *>(u"pdfList"_s);
        QTRY_VERIFY(itemAt(list, 0));
        auto *page = itemAt(list, 0)->findChild<QQuickItem *>(u"pageImage"_s);
        QVERIFY(page);
        QTRY_COMPARE(page->property("status").toInt(), 1);   // rendered by the async provider
    }
    void officeCacheHit()   // converted before: the probe hands it over as kind Pdf with the cached PDF
    {
        const QString doc = file(u"memo.rtf"_s);
        QVERIFY(writeFile(doc, "{\\rtf1 hello}"));
        OfficeCache cache;
        const QString job = QDir(cache.dir()).filePath(u"job.pdf"_s);
        {
            QPdfWriter writer(job);
            QPainter painter(&writer);
            painter.drawText(QPointF(500, 500), u"memo"_s);
        }
        QVERIFY(!cache.store(doc, job).isEmpty());
        QVERIFY(show(doc) != 0);
        QCOMPARE(m_controller->kind(), u"pdf"_s);
        auto *list = m_window->findChild<QQuickItem *>(u"pdfList"_s);
        QTRY_VERIFY(itemAt(list, 0));
        auto *page = itemAt(list, 0)->findChild<QQuickItem *>(u"pageImage"_s);
        QVERIFY(page);
        QTRY_COMPARE(page->property("status").toInt(), 1);   // the cached PDF renders, not the .rtf
    }
    void folder()
    {
        const QString dir = file(u"folder"_s);
        QVERIFY(QDir().mkpath(dir));
        for (const char *n : {"a", "b", "c"})
            QVERIFY(writeFile(dir + u'/' + QLatin1String(n), "x"));
        QVERIFY(show(dir) != 0);
        QCOMPARE(m_controller->kind(), u"folder"_s);
        QCOMPARE(m_controller->currentLayout().window, QSize(440, 180));
        QCOMPARE(m_window->minimumSize(), QSize(440, 180));
        QCOMPARE(m_controller->cardRect(), QRectF(0, 0, 440, 180));
        QCOMPARE(m_controller->cardName(), u"folder"_s);
        QTRY_VERIFY(m_controller->cardDetails().contains(itemsText(3, uiRussian())));
    }
    void binaryCard()
    {
        const QString path = file(u"blob.bin"_s);
        QVERIFY(writeFile(path, QByteArray("\x00\x01\x02\x03 binary", 12)));
        QVERIFY(show(path) != 0);
        QCOMPARE(m_controller->kind(), u"file"_s);
        QCOMPARE(m_controller->cardReason(), QString());
        QVERIFY(!m_controller->cardType().isEmpty());
        QVERIFY(m_controller->cardDetails().startsWith(formatSize(12, uiRussian())));
        auto *card = m_window->findChild<QQuickItem *>(u"fileCard"_s);
        QVERIFY(card && card->isVisible());
    }
    void longNameTwoLines()
    {
        const QString name = u"a-very-long-file-name-that-does-not-fit-in-one-line-of-the-card-at-all-0123456789.bin"_s;
        QVERIFY(writeFile(file(name), QByteArray("\x00\x01", 2)));
        QVERIFY(show(file(name)) != 0);
        const QStringList lines = m_controller->cardName().split(u'\n');
        QCOMPARE(lines.size(), 2);
        QVERIFY(name.startsWith(lines[0]));
        QVERIFY(lines[1].contains(u'…'));
        QVERIFY(lines[1].endsWith(u".bin"_s));
    }
    // 30 calls in one pass (Review Focus 3): one probe for the final address, one "готово к показу"
    void burst()
    {
        QSignalSpy spy(m_controller.get(), &Controller::shown);
        request(file(u"photo.png"_s), true);
        for (int i = 1; i < 29; ++i)
            request(file(i % 2 ? u"tiny.png"_s : u"photo.png"_s), false);
        request(file(u"other.txt"_s), false);
        QVERIFY(spy.wait(2000));
        QTest::qWait(300);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(m_controller->kind(), u"text"_s);
        QCOMPARE(m_window->title(), u"other.txt"_s);
    }
    // Review Focus 2: the file vanishes between ShowFile and the probe
    void vanishedBeforeLoad()
    {
        const QString path = file(u"gone.png"_s);
        QVERIFY(writePng(path, QSize(100, 100)));
        QSignalSpy spy(m_controller.get(), &Controller::shown);
        request(path);
        QVERIFY(QFile::remove(path));
        QVERIFY(spy.wait(2000));
        QCOMPARE(m_controller->kind(), u"file"_s);
        QCOMPARE(m_controller->cardReason(), reasonText(CardReason::CannotOpen, uiRussian()));
        QCOMPARE(m_controller->currentLayout().window, QSize(440, 180));
    }
    void closeEventHides()   // Super+W / "close all windows": our close, the platform window stays
    {
        QVERIFY(show(file(u"notes.txt"_s)) != 0);
        QCloseEvent event;
        QGuiApplication::sendEvent(m_window, &event);
        QVERIFY(!m_session.visible());
        QVERIFY(!m_window->isVisible());
        QVERIFY(m_window->handle() != nullptr);
    }
    void arrowsSelect()
    {
        QVERIFY(show(file(u"notes.txt"_s)) != 0);
        QSignalSpy spy(m_service.get(), &PreviewerService::selectionEvent);
        QTest::keyClick(m_window, Qt::Key_Right);
        QTest::keyClick(m_window, Qt::Key_Up);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(0).at(0).toUInt(), 5u);
        QCOMPARE(spy.at(1).at(0).toUInt(), 2u);
        QCOMPARE(m_controller->kind(), u"text"_s);   // content untouched until Files sends ShowFile
    }
    void enterOpensAndCloses()
    {
        QFile::remove(m_launchLog);
        const QString path = file(u"photo.png"_s);
        QVERIFY(show(path) != 0);
        QTest::keyClick(m_window, Qt::Key_Return);
        QVERIFY(!m_session.visible());
        QTRY_VERIFY(QFile::exists(m_launchLog));   // written atomically by the stub
        QFile log(m_launchLog);
        QVERIFY(log.open(QIODevice::ReadOnly));
        const QString line = QString::fromUtf8(log.readAll()).trimmed();
        QVERIFY2(QRegularExpression(u"^-- \\S+ "_s + QRegularExpression::escape(path) + u'$').match(line).hasMatch(),
                 qPrintable(line));
    }
    void spaceCloses()
    {
        QVERIFY(show(file(u"notes.txt"_s)) != 0);
        QTest::keyClick(m_window, Qt::Key_Space);
        QVERIFY(!m_session.visible());
        QVERIFY(!m_window->isVisible());
    }
    void fullScreenRoundTrip()
    {
        QVERIFY(show(file(u"photo.png"_s)) != 0);
        const Layout l = m_controller->currentLayout();
        QTest::keyClick(m_window, Qt::Key_F);
        QTRY_VERIFY(m_controller->fullScreen());
        QTRY_COMPARE(m_controller->contentRect(),
                     geometry::fullscreenContent(Kind::Image, m_window->size(), QSizeF(800, 600), m_controller->scale()));
        QTest::keyClick(m_window, Qt::Key_F);
        QTRY_VERIFY(!m_controller->fullScreen());
        QCOMPARE(m_window->minimumSize(), l.window);
        QCOMPARE(m_window->maximumSize(), l.window);
        QTRY_COMPARE(m_window->size(), l.window);
        QTRY_COMPARE(m_controller->contentRect(), l.content);
    }
    // Switching resizes in the same pass: at shown(B) min == max == B's window and the rect is B's
    // (shrink both, grow both, and a mixed change through setWindowSize).
    void switchResizes()
    {
        QVERIFY(writePng(file(u"wide.png"_s), QSize(700, 100)));
        QVERIFY(writePng(file(u"tall.png"_s), QSize(200, 500)));
        QVERIFY(show(file(u"photo.png"_s)) != 0);
        const QStringList steps{u"tiny.png"_s, u"photo.png"_s, u"wide.png"_s, u"tall.png"_s};   // shrink, grow, -, mixed
        QSize previous = m_controller->currentLayout().window;
        for (const QString &name : steps) {
            Layout atShown;
            QSize minAt, maxAt;
            QRectF rectAt;
            auto c = connect(m_controller.get(), &Controller::shown, this, [&] {
                atShown = m_controller->currentLayout();
                minAt = m_window->minimumSize();
                maxAt = m_window->maximumSize();
                rectAt = m_controller->contentRect();
            });
            QSignalSpy spy(m_controller.get(), &Controller::shown);
            request(file(name), false);
            QVERIFY(spy.wait(2000));
            disconnect(c);
            QCOMPARE(m_window->title(), name);
            QVERIFY(atShown.window != previous);
            QCOMPARE(minAt, atShown.window);
            QCOMPARE(maxAt, atShown.window);
            QCOMPARE(rectAt, atShown.content);
            QTRY_COMPARE(m_window->size(), atShown.window);
            previous = atShown.window;
        }
    }
    // A held arrow: the 40 ms grace bounds the user's wait, not each generation's, so slow images still show.
    void heldArrowShows()
    {
        QStringList big;
        for (int i = 0; i < 3; ++i) {
            big << file(u"big%1.png"_s.arg(i));
            QVERIFY(writeDetailedPng(big.last(), QSize(4000, 3000)));
        }
        QVERIFY(show(file(u"notes.txt"_s)) != 0);
        const quint64 start = m_session.generation();
        QList<quint64> during;
        auto c = connect(m_controller.get(), &Controller::shown, this, [&](quint64 g) { during << g; });
        for (int i = 0; i < 15; ++i) {
            request(big.at(i % big.size()), false);
            QTest::qWait(30);
        }
        disconnect(c);
        QVERIFY2(during.size() >= 3, qPrintable(QString::number(during.size())));
        QVERIFY(during.first() > start);
    }
    void brokenAudioCard()   // spec "Ошибки и пределы": InvalidMedia -> "Не удалось открыть"
    {
        const QString path = file(u"broken.mp3"_s);
        QVERIFY(writeFile(path, QByteArray(4096, 'z')));
        StderrMute mute;
        QVERIFY(show(path) != 0);
        QCOMPARE(m_controller->kind(), u"audio"_s);
        QTRY_COMPARE(m_controller->cardReason(), reasonText(CardReason::CannotOpen, uiRussian()));
        QCOMPARE(m_controller->kind(), u"file"_s);
        QCOMPARE(m_controller->currentLayout().window, QSize(520, 180));   // the shown window keeps its size
    }
    void hintClearedOnSwitchAndClose()
    {
        QVERIFY(writeFile(file(u"nobody.qlunknown"_s), QByteArray("\x00\x01", 2)));
        QVERIFY(show(file(u"nobody.qlunknown"_s)) != 0);
        QTest::keyClick(m_window, Qt::Key_Return);   // application/octet-stream: no default app
        QVERIFY(m_session.visible());
        QCOMPARE(m_controller->hint(), uiText(Str::NoApp));
        QVERIFY(writeFile(file(u"nobody2.qlunknown"_s), QByteArray("\x00\x02", 2)));
        QVERIFY(show(file(u"nobody2.qlunknown"_s)) != 0);   // a switch (another uri while shown)
        QCOMPARE(m_controller->hint(), QString());   // cleared by the switch, long before its 2 s
        QTRY_COMPARE(m_window->title(), u"nobody2.qlunknown"_s);
        QTest::keyClick(m_window, Qt::Key_Return);
        QCOMPARE(m_controller->hint(), uiText(Str::NoApp));
        m_service->handleClose();
        QCOMPARE(m_controller->hint(), QString());
    }
    // Hyprland "maximized" (Super+Alt+F) or tiling resizes the window without a state Qt can see: the content
    // follows the actual size; back at the file's size, the normal content rect returns.
    void compositorSizeRefits()
    {
        QVERIFY(show(file(u"photo.png"_s)) != 0);
        const Layout l = m_controller->currentLayout();
        QTRY_COMPARE(m_window->size(), l.window);
        m_window->setMinimumSize(QSize(0, 0));   // the compositor does not ask: stand in for its configure
        m_window->setMaximumSize(QSize(16777215, 16777215));   // QWINDOWSIZE_MAX
        const QSize maximized(1200, 700);
        m_window->resize(maximized);
        QTRY_COMPARE(m_controller->contentRect(),
                     geometry::fullscreenContent(Kind::Image, maximized, QSizeF(800, 600), m_controller->scale()));
        QVERIFY(!m_controller->fullScreen());
        m_window->resize(l.window);
        QTRY_COMPARE(m_controller->contentRect(), l.content);
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
    qputenv("PULSE_SERVER", "unix:/nonexistent");   // no audio output is ever created; if one were, it goes nowhere
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
    TstController t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_controller.moc"
