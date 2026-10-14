#include "MediaController.h"
#include "StateStore.h"
#include <QAudioOutput>
#include <QHash>
#include <QMediaPlayer>
#include <QPointer>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>

namespace ql {

struct MediaController::Private {
    struct Player {
        QMediaPlayer *player = nullptr;   // owns its private QVideoSink
        QVideoFrame lastFrame;            // last valid frame: stays shown at the end of the stream
        bool reported = false;            // loaded() is emitted once per generation
    };
    StateStore *state = nullptr;
    QPointer<QVideoSink> output;
    QHash<quint64, Player> players;
    quint64 active = 0;
    bool audioOnly = false;
    QAudioOutput *audio = nullptr;
    QTimer autoplay;
    QTimer releaseAudio;

    QMediaPlayer *shown() const { return players.value(active).player; }
    void dropAudio()
    {
        if (QMediaPlayer *p = shown())
            p->setAudioOutput(nullptr);
        delete audio;
        audio = nullptr;
    }
    // spec «Видео»/«Аудио»: muted video has no audio output at all; a muted audio file plays into a muted one
    // (without an output it never reaches EndOfMedia)
    void play()
    {
        QMediaPlayer *p = shown();
        if (!p)
            return;
        releaseAudio.stop();
        const bool muted = state->muted();
        if (muted && !audioOnly) {
            dropAudio();
        } else {
            if (!audio)
                audio = new QAudioOutput(p->parent());
            audio->setMuted(muted);
            if (p->audioOutput() != audio) {
                // Qt 6.11 FFmpeg: an output attached to a paused or playing player opens its stream 2-7 s
                // late (video frozen or silent meanwhile); a stopped one opens it at once. Restart in place.
                const qint64 pos = p->position();
                p->stop();
                p->setAudioOutput(audio);
                if (pos > 0)
                    p->setPosition(pos);
            }
        }
        p->play();
    }
};

MediaController::MediaController(StateStore *state, QObject *parent) : QObject(parent), d(std::make_unique<Private>())
{
    d->state = state;
    d->autoplay.setSingleShot(true);
    d->autoplay.setInterval(kAutoplayMs);
    connect(&d->autoplay, &QTimer::timeout, this, [this] { d->play(); });
    d->releaseAudio.setSingleShot(true);
    d->releaseAudio.setInterval(kReleaseAudioAfterPauseMs);
    connect(&d->releaseAudio, &QTimer::timeout, this, [this] {
        if (!playing())
            d->dropAudio();
    });
}

MediaController::~MediaController()
{
    release();
}

void MediaController::setOutputSink(QVideoSink *sink)
{
    d->output = sink;
}

void MediaController::load(quint64 generation, const QUrl &url)
{
    d->autoplay.stop();   // a switch is under way: the shown player must not start now (spec «Сессия»)
    for (auto it = d->players.begin(); it != d->players.end();) {
        if (it.key() == d->active) {
            ++it;
            continue;
        }
        it->player->stop();
        delete it->player;
        it = d->players.erase(it);
    }

    auto *player = new QMediaPlayer(this);
    auto *sink = new QVideoSink(player);
    player->setVideoSink(sink);
    d->players.insert(generation, {player, {}, false});

    connect(player, &QMediaPlayer::mediaStatusChanged, this, [this, generation, player, sink](QMediaPlayer::MediaStatus s) {
        if (generation == d->active)
            Q_EMIT stateChanged();
        if (s != QMediaPlayer::LoadedMedia && s != QMediaPlayer::InvalidMedia)
            return;
        auto it = d->players.find(generation);
        if (it == d->players.end() || it->reported)
            return;
        it->reported = true;
        const bool ok = s == QMediaPlayer::LoadedMedia;
        const bool hasVideo = ok && player->hasVideo();
        const QSize size = hasVideo ? sink->videoSize() : QSize();
        // queued: a handler may load()/release(), which deletes players — a direct emit from inside
        // QFFmpegMediaPlayer::setMediaAsync is a use-after-free
        QMetaObject::invokeMethod(this, [this, generation, ok, hasVideo, size] {
            if (d->players.contains(generation))
                Q_EMIT loaded(generation, ok, hasVideo, size);
        }, Qt::QueuedConnection);
    });
    connect(sink, &QVideoSink::videoFrameChanged, this, [this, generation](const QVideoFrame &frame) {
        auto it = d->players.find(generation);
        if (it == d->players.end() || !frame.isValid())
            return;   // the player's empty end-of-stream frame is never forwarded
        const bool first = !it->lastFrame.isValid();
        it->lastFrame = frame;
        if (generation != d->active)
            return;
        if (d->output)
            d->output->setVideoFrame(frame);
        if (first)
            Q_EMIT stateChanged();
    });
    connect(player, &QMediaPlayer::playbackStateChanged, this, [this, generation](QMediaPlayer::PlaybackState s) {
        if (generation != d->active)
            return;
        if (s != QMediaPlayer::PlayingState)
            d->releaseAudio.start();   // paused or stopped at the end: the output goes after 1 s idle
        Q_EMIT stateChanged();
    });
    connect(player, &QMediaPlayer::durationChanged, this, [this, generation] {
        if (generation == d->active)
            Q_EMIT stateChanged();
    });
    connect(player, &QMediaPlayer::positionChanged, this, [this, generation] {
        if (generation == d->active)
            Q_EMIT positionChanged();
    });

    player->setSource(url);
    player->pause();
}

void MediaController::activate(quint64 generation, bool audioOnly)
{
    if (d->active != generation) {
        if (QMediaPlayer *previous = d->shown()) {
            previous->stop();
            d->dropAudio();   // released on every switch, recreated by the next play()
            previous->deleteLater();
            d->players.remove(d->active);
        }
        d->active = generation;
    }
    d->audioOnly = audioOnly;
    if (d->output)
        d->output->setVideoFrame(d->players.value(generation).lastFrame);   // invalid = explicit clear
    d->autoplay.start();
    Q_EMIT stateChanged();
    Q_EMIT positionChanged();
}

void MediaController::release()
{
    d->dropAudio();
    for (const Private::Player &p : std::as_const(d->players)) {
        p.player->stop();
        delete p.player;
    }
    d->players.clear();
    d->autoplay.stop();
    d->releaseAudio.stop();   // after the stops above, which re-arm it
    d->active = 0;
    if (d->output)
        d->output->setVideoFrame(QVideoFrame());
    Q_EMIT stateChanged();
    Q_EMIT positionChanged();
}

void MediaController::togglePlay()
{
    QMediaPlayer *p = d->shown();
    if (!p)
        return;
    d->autoplay.stop();
    if (p->playbackState() == QMediaPlayer::PlayingState) {
        p->pause();
    } else {
        d->play();
    }
}

void MediaController::seekBy(qint64 deltaMs)
{
    // no clamp: QMediaPlayer bounds the position itself, and a clamp to an unknown (0) duration would block seeking
    if (QMediaPlayer *p = d->shown())
        p->setPosition(p->position() + deltaMs);
}

void MediaController::toggleMute()
{
    const bool muted = !d->state->muted();
    d->state->setMuted(muted);
    if (d->audioOnly) {
        if (d->audio)
            d->audio->setMuted(muted);
    } else if (muted) {
        d->dropAudio();
    } else if (playing()) {
        d->play();   // attaches a new audio output to the running player
    }
    Q_EMIT stateChanged();
}

bool MediaController::playing() const
{
    const QMediaPlayer *p = d->shown();
    return p && p->playbackState() == QMediaPlayer::PlayingState;
}

bool MediaController::muted() const
{
    return d->state->muted();
}

bool MediaController::hasFrame() const
{
    return d->players.value(d->active).lastFrame.isValid();
}

qint64 MediaController::position() const
{
    const QMediaPlayer *p = d->shown();
    return p ? p->position() : 0;
}

qint64 MediaController::duration() const
{
    const QMediaPlayer *p = d->shown();
    return p ? p->duration() : 0;
}

bool MediaController::hasAudioOutput() const
{
    return d->audio;
}

} // namespace ql
