#include "MediaController.h"
#include "StateStore.h"
#include <QCoreApplication>
#include <QFile>
#include <QLoggingCategory>
#include <QProcess>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QVideoFrame>
#include <QVideoSink>
#include <fcntl.h>
#include <unistd.h>

// Never creates an audio output: video runs muted (no output at all), audio files are only loaded.
using namespace ql;

namespace {
bool ffmpeg(const QStringList &args)
{
    QProcess p;
    p.start(QStringLiteral("ffmpeg"), QStringList{QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y")} + args);
    return p.waitForFinished(30000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}
QStringList video(const QString &size, const QString &out)
{
    return {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
            QStringLiteral("testsrc=size=%1:rate=25:duration=1").arg(size), QStringLiteral("-c:v"),
            QStringLiteral("libx264"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), out};
}
struct StderrMute {   // FFmpeg's av_log reports the garbage file straight to stderr
    int saved = dup(2);
    StderrMute() { if (const int n = open("/dev/null", O_WRONLY); n >= 0) { dup2(n, 2); close(n); } }
    ~StderrMute() { dup2(saved, 2); close(saved); }
};
using Loaded = std::tuple<quint64, bool, bool, QSize>;
Loaded waitLoaded(MediaController &m, quint64 gen, int timeoutMs = 2000)
{
    QSignalSpy spy(&m, &MediaController::loaded);
    for (QDeadlineTimer t(timeoutMs); !t.hasExpired(); QTest::qWait(5))
        for (const QList<QVariant> &a : spy)
            if (a.at(0).toULongLong() == gen)
                return {gen, a.at(1).toBool(), a.at(2).toBool(), a.at(3).toSize()};
    return {0, false, false, {}};
}
} // namespace

class TstMedia : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    std::unique_ptr<StateStore> state;
    QUrl url(const char *name) const { return QUrl::fromLocalFile(dir.filePath(QString::fromLatin1(name))); }
private Q_SLOTS:
    void initTestCase()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty())
            QSKIP("ffmpeg missing");
        QVERIFY(ffmpeg(video(QStringLiteral("320x240"), dir.filePath(QStringLiteral("land.mp4")))));
        QVERIFY(ffmpeg(video(QStringLiteral("240x320"), dir.filePath(QStringLiteral("port.mp4")))));
        // ffmpeg 9: only an input-side display matrix survives -c copy ('-metadata:s:v rotate=90' is ignored)
        QVERIFY(ffmpeg({QStringLiteral("-display_rotation"), QStringLiteral("90"), QStringLiteral("-i"),
                        dir.filePath(QStringLiteral("land.mp4")), QStringLiteral("-c"), QStringLiteral("copy"),
                        dir.filePath(QStringLiteral("rot.mp4"))}));
        QVERIFY(ffmpeg({QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("sine=duration=1"),
                        QStringLiteral("-c:a"), QStringLiteral("libmp3lame"), dir.filePath(QStringLiteral("tone.mp3"))}));
        QFile g(dir.filePath(QStringLiteral("garbage.mp4")));
        QVERIFY(g.open(QIODevice::WriteOnly));
        QByteArray junk(64 * 1024, Qt::Uninitialized);
        // fixed seed: about 1 in 200 fresh random files probes as a raw stream (seen: VVC) and loads
        QRandomGenerator(15).fillRange(reinterpret_cast<quint32 *>(junk.data()), junk.size() / 4);
        g.write(junk);
        g.close();
        state = std::make_unique<StateStore>(dir.filePath(QStringLiteral("state.ini")));
        state->setMuted(true);
    }
    void init() { QVERIFY(state->muted()); }   // a test that flips mute must restore it: muted = no audio output

    void sizes_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QSize>("size");
        QTest::newRow("landscape") << QStringLiteral("land.mp4") << QSize(320, 240);
        QTest::newRow("portrait") << QStringLiteral("port.mp4") << QSize(240, 320);
        QTest::newRow("rotated") << QStringLiteral("rot.mp4") << QSize(240, 320);
    }
    void sizes()
    {
        QFETCH(QString, file);
        QFETCH(QSize, size);
        MediaController m(state.get());
        m.load(1, QUrl::fromLocalFile(dir.filePath(file)));
        const auto [gen, ok, hasVideo, videoSize] = waitLoaded(m, 1);
        QCOMPARE(gen, 1u);
        QVERIFY(ok);
        QVERIFY(hasVideo);
        QCOMPARE(videoSize, size);
    }
    void audioFileHasNoVideo()
    {
        MediaController m(state.get());
        m.load(1, url("tone.mp3"));
        const auto [gen, ok, hasVideo, videoSize] = waitLoaded(m, 1);
        QCOMPARE(gen, 1u);
        QVERIFY(ok);
        QVERIFY(!hasVideo);
        QVERIFY(!m.hasAudioOutput());
    }
    void garbageFails()
    {
        StderrMute mute;
        MediaController m(state.get());
        m.load(1, url("garbage.mp4"));
        const auto [gen, ok, hasVideo, videoSize] = waitLoaded(m, 1);
        QCOMPARE(gen, 1u);
        QVERIFY(!ok);
        QVERIFY(!hasVideo);
    }
    void handlerMayReplacePlayer()
    {
        // a loaded() handler loads the next file; with a direct emit this is a use-after-free inside
        // QFFmpegMediaPlayer (visible under ASan only)
        StderrMute mute;
        MediaController m(state.get());
        quint64 last = 0;
        connect(&m, &MediaController::loaded, this, [&](quint64 gen) {
            last = gen;
            if (gen < 20)
                m.load(gen + 1, url(gen % 2 ? "garbage.mp4" : "land.mp4"));
        });
        m.load(1, url("land.mp4"));
        QTRY_COMPARE_WITH_TIMEOUT(last, 20u, 10000);
    }
    void autoplayMutedVideo()
    {
        QVideoSink out;
        MediaController m(state.get());
        m.setOutputSink(&out);
        QSignalSpy loadedSpy(&m, &MediaController::loaded);
        m.load(1, url("land.mp4"));
        QVERIFY(std::get<1>(waitLoaded(m, 1)));
        m.activate(1, false);
        QTest::qWait(MediaController::kAutoplayMs / 2);
        QVERIFY(!m.playing());
        QTest::qWait(MediaController::kAutoplayMs + 150);
        QVERIFY(m.playing());
        QVERIFY(m.muted());
        QVERIFY(!m.hasAudioOutput());
        QVERIFY(m.hasFrame());
        QVERIFY(out.videoFrame().isValid());
        QCOMPARE(m.duration(), 1000);
        // end of stream: stopped on the last frame, never an empty one
        QTRY_VERIFY_WITH_TIMEOUT(!m.playing(), 3000);
        QTest::qWait(100);
        QVERIFY(out.videoFrame().isValid());
        QVERIFY(m.hasFrame());
        QVERIFY(!m.hasAudioOutput());
        m.togglePlay();   // replay from the start, then stop: the player passes LoadedMedia again
        QTRY_VERIFY(m.playing());
        m.togglePlay();
        QTest::qWait(100);
        QCOMPARE(loadedSpy.count(), 1);   // loaded() once per generation
    }
    void togglePlayAndSeek()
    {
        MediaController m(state.get());
        m.load(1, url("land.mp4"));
        QVERIFY(std::get<1>(waitLoaded(m, 1)));
        m.activate(1, false);
        m.togglePlay();   // K before the autoplay: plays now
        QVERIFY(m.playing());
        m.togglePlay();   // and paused again: the pending autoplay must not restart it
        QVERIFY(!m.playing());
        QTest::qWait(MediaController::kAutoplayMs + 100);
        QVERIFY(!m.playing());
        QSignalSpy pos(&m, &MediaController::positionChanged);
        m.seekBy(-10000);
        QTRY_COMPARE(m.position(), 0);
        m.seekBy(400);
        QTRY_VERIFY(qAbs(m.position() - 400) < 50);
        QVERIFY(pos.count() > 0);
        m.togglePlay();
        QVERIFY(m.playing());
        QVERIFY(!m.hasAudioOutput());
    }
    void muteToggleWhilePausedCreatesNoOutput()
    {
        MediaController m(state.get());
        m.load(1, url("land.mp4"));
        QVERIFY(std::get<1>(waitLoaded(m, 1)));
        m.activate(1, false);   // paused until the autoplay
        QSignalSpy changed(&m, &MediaController::stateChanged);
        m.toggleMute();
        QVERIFY(!m.muted());
        QVERIFY(!state->muted());
        QVERIFY(!m.hasAudioOutput());
        m.toggleMute();
        QVERIFY(m.muted());
        QVERIFY(changed.count() >= 2);
        m.release();
    }
    void supersededPlayerIsDropped()
    {
        MediaController m(state.get());
        QSignalSpy spy(&m, &MediaController::loaded);
        m.load(1, url("land.mp4"));
        m.load(2, url("port.mp4"));   // 1 superseded before activation: deleted at once, never reports
        QTRY_COMPARE(spy.count(), 1);
        QTest::qWait(200);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toULongLong(), 2u);
    }
    void switchCancelsAutoplay()
    {
        MediaController m(state.get());
        m.load(1, url("land.mp4"));
        QVERIFY(std::get<1>(waitLoaded(m, 1)));
        m.activate(1, false);
        m.load(2, url("port.mp4"));
        QTest::qWait(MediaController::kAutoplayMs + 100);
        QVERIFY(!m.playing());
    }
    void activateSwitchesOutput()
    {
        QVideoSink out;
        MediaController m(state.get());
        m.setOutputSink(&out);
        m.load(1, url("land.mp4"));
        QVERIFY(std::get<1>(waitLoaded(m, 1)));
        m.activate(1, false);
        QTRY_VERIFY(m.playing());
        QVERIFY(out.videoFrame().isValid());
        QCOMPARE(out.videoFrame().size(), QSize(320, 240));
        m.load(2, url("port.mp4"));
        QVERIFY(std::get<1>(waitLoaded(m, 2)));
        QVERIFY(m.playing());   // still the shown player 1 until activate(2)
        m.activate(2, false);
        // player 2's first paused frame (or nothing yet) — never player 1's frame
        QVERIFY(!out.videoFrame().isValid() || out.videoFrame().size() == QSize(240, 320));
        QVERIFY(!m.playing());
        QTRY_VERIFY(out.videoFrame().isValid());
        QCOMPARE(out.videoFrame().size(), QSize(240, 320));
        QTRY_VERIFY(m.playing());
    }
    void releaseClearsEverything()
    {
        QVideoSink out;
        MediaController m(state.get());
        m.setOutputSink(&out);
        m.load(1, url("land.mp4"));
        QVERIFY(std::get<1>(waitLoaded(m, 1)));
        m.activate(1, false);
        QTRY_VERIFY(m.playing());
        QVERIFY(out.videoFrame().isValid());
        m.release();
        QVERIFY(!m.playing());
        QVERIFY(!m.hasFrame());
        QVERIFY(!out.videoFrame().isValid());
        QCOMPARE(m.duration(), 0);
        QTest::qWait(100);   // no late frame of the dropped player reaches the output
        QVERIFY(!out.videoFrame().isValid());
    }
};

int main(int argc, char **argv)
{
    // software decoding: the hardware probe would wake NVIDIA
    qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", ",");
    qputenv("QT_FFMPEG_ENCODING_HW_DEVICE_TYPES", ",");
    QCoreApplication app(argc, argv);
    QLoggingCategory::setFilterRules(QStringLiteral("qt.multimedia.*=false"));   // per-file FFmpeg dumps, decoder notes
    TstMedia t;
    return QTest::qExec(&t, argc, argv);
}
#include "tst_media.moc"
