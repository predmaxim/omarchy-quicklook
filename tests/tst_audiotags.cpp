#include "AudioTags.h"
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <taglib/fileref.h>
#include <taglib/tdebuglistener.h>
#include <taglib/tpropertymap.h>

using namespace ql;

namespace {
bool ffmpeg(const QStringList &args)
{
    QProcess p;
    p.start(QStringLiteral("ffmpeg"), QStringList{QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y")} + args);
    return p.waitForFinished(30000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}
struct Silent : TagLib::DebugListener {
    void printMessage(const TagLib::String &) override {}
};
} // namespace

class TstAudioTags : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    QString mp3, opus;
private Q_SLOTS:
    void initTestCase()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty())
            QSKIP("ffmpeg missing");
        const QString tone = dir.filePath(QStringLiteral("tone.mp3"));
        const QString cover = dir.filePath(QStringLiteral("cover.png"));
        mp3 = dir.filePath(QStringLiteral("tagged.mp3"));
        opus = dir.filePath(QStringLiteral("tagged.opus"));
        const QStringList sine{QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("sine=duration=1")};
        const QStringList meta{QStringLiteral("-metadata"), QStringLiteral("title=Тест"), QStringLiteral("-metadata"),
                               QStringLiteral("artist=A"), QStringLiteral("-metadata"), QStringLiteral("album=Альбом")};
        QVERIFY(ffmpeg(sine + meta + QStringList{QStringLiteral("-c:a"), QStringLiteral("libmp3lame"), tone}));
        QVERIFY(ffmpeg({QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("color=red:s=16x16"),
                        QStringLiteral("-frames:v"), QStringLiteral("1"), cover}));
        QVERIFY(ffmpeg({QStringLiteral("-i"), tone, QStringLiteral("-i"), cover, QStringLiteral("-map"), QStringLiteral("0"),
                        QStringLiteral("-map"), QStringLiteral("1"), QStringLiteral("-c"), QStringLiteral("copy"),
                        QStringLiteral("-id3v2_version"), QStringLiteral("3"), QStringLiteral("-metadata:s:v"),
                        QStringLiteral("comment=Cover (front)"), mp3}));
        QVERIFY(ffmpeg(sine + meta + QStringList{QStringLiteral("-c:a"), QStringLiteral("libopus"), opus}));
        // ffmpeg cannot write two ARTIST values: add the second with TagLib
        TagLib::FileRef f(QFile::encodeName(mp3).constData());
        QVERIFY(!f.isNull());
        TagLib::PropertyMap props = f.properties();
        props["ARTIST"] = TagLib::StringList(TagLib::String("A")).append(TagLib::String("B"));
        f.setProperties(props);
        QVERIFY(f.save());
    }
    void mp3Tags()
    {
        const AudioTags t = readAudioTags(mp3);
        QCOMPARE(t.title, QStringLiteral("Тест"));
        QCOMPARE(t.artist, QStringLiteral("A, B"));
        QCOMPARE(t.album, QStringLiteral("Альбом"));
        QVERIFY2(qAbs(t.durationMs - 1000) < 100, qPrintable(QString::number(t.durationMs)));
    }
    void mp3Cover()
    {
        const AudioTags t = readAudioTags(mp3);
        QCOMPARE(t.coverMime, QByteArray("image/png"));
        QVERIFY(t.cover.startsWith("\x89PNG"));
    }
    void opusTags()
    {
        const AudioTags t = readAudioTags(opus);
        QCOMPARE(t.title, QStringLiteral("Тест"));
        QCOMPARE(t.artist, QStringLiteral("A"));
        QCOMPARE(t.album, QStringLiteral("Альбом"));
        QVERIFY2(qAbs(t.durationMs - 1000) < 100, qPrintable(QString::number(t.durationMs)));
        QVERIFY(t.cover.isEmpty());
        QVERIFY(t.coverMime.isEmpty());
    }
    void unreadable()
    {
        const QString junk = dir.filePath(QStringLiteral("junk.mp3"));
        QFile f(junk);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not audio");
        f.close();
        Silent silent;   // TagLib reports the unparsable file on stderr
        TagLib::setDebugListener(&silent);
        const AudioTags t = readAudioTags(junk);
        QVERIFY(t.title.isEmpty() && t.artist.isEmpty() && t.durationMs == 0 && t.cover.isEmpty());
        QCOMPARE(readAudioTags(dir.filePath(QStringLiteral("missing.mp3"))).durationMs, 0);
        TagLib::setDebugListener(nullptr);
    }
};

QTEST_GUILESS_MAIN(TstAudioTags)
#include "tst_audiotags.moc"
