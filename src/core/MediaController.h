#pragma once
// Video/audio playback (spec: "Видео", "Аудио").
#include <QObject>
#include <QSize>
#include <QUrl>
#include <memory>

class QVideoSink;

namespace ql {

class StateStore;

class MediaController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool playing READ playing NOTIFY stateChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY stateChanged)
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY stateChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY stateChanged)
public:
    static constexpr int kAutoplayMs = 300;
    static constexpr int kReleaseAudioAfterPauseMs = 1000;

    explicit MediaController(StateStore *state, QObject *parent = nullptr);
    ~MediaController() override;

    void setOutputSink(QVideoSink *sink);    // QML VideoOutput.videoSink: frames of the active player are forwarded here
    void load(quint64 generation, const QUrl &url);
        // new QMediaPlayer + private QVideoSink, setSource + pause(); emits loaded() (queued); players superseded
        // before activation are deleted at once — call once per generation
    void activate(quint64 generation, bool audioOnly);
        // becomes the shown player: forward frames (never invalid ones), (re)arm the autoplay timer;
        // the previously shown player is deleted after this call (deleteLater)
    void release();                          // close: stop, drop all players and the audio output, clear the output
    void togglePlay();
    void seekBy(qint64 deltaMs);
    void toggleMute();                       // remembered in StateStore; video: creates/removes the audio output

    bool playing() const;
    bool muted() const;
    bool hasFrame() const;
    qint64 position() const;
    qint64 duration() const;
    bool hasAudioOutput() const;             // tests

Q_SIGNALS:
    void loaded(quint64 generation, bool ok, bool hasVideo, QSize videoSize);
    void stateChanged();
    void positionChanged();

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace ql
