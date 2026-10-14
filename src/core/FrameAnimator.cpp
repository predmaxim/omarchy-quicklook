#include "FrameAnimator.h"
#include "ImageStore.h"
#include <QImageReader>
#include <QQueue>
#include <QThread>
#include <QTimer>

namespace ql {

namespace {

constexpr int kMaxQueued = 3;
constexpr int kMinDelayMs = 20;

struct Frame {
    QImage image;
    int delay = 0;
};

// Lives in the decoder thread; all calls arrive through queued invocations.
struct Worker : QObject {
    QString path;
    QSize target;
    std::unique_ptr<QImageReader> reader;

    void open()
    {
        reader = std::make_unique<QImageReader>(path);
        reader->setAutoTransform(true);
    }
    Frame next()
    {
        if (!reader || !reader->canRead())
            open();   // loop from the start
        Frame f;
        f.delay = reader->nextImageDelay();
        f.image = reader->read();
        if (f.image.isNull()) {   // broken tail or single image: restart once
            open();
            f.delay = reader->nextImageDelay();
            f.image = reader->read();
        }
        if (!f.image.isNull())
            f.image = f.image.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                          .convertToFormat(QImage::Format_ARGB32_Premultiplied);
        return f;
    }
};

} // namespace

struct FrameAnimator::Private {
    FrameAnimator *q;
    ImageStore *store;
    quint64 generation = 0;
    int token = 0;   // bumped on every start/stop; stale deliveries are dropped
    QThread *thread = nullptr;
    Worker *worker = nullptr;
    QQueue<Frame> queue;
    QTimer timer;
    bool pending = false;   // a decode request is in flight
    bool waiting = false;   // timer elapsed with nothing to show

    void request()
    {
        if (!worker || pending || queue.size() >= kMaxQueued)
            return;
        pending = true;
        QMetaObject::invokeMethod(worker, [this, w = worker, t = token] {
            Frame f = w->next();
            QMetaObject::invokeMethod(q, [this, t, f] { onFrame(t, f); }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
    }
    void onFrame(int t, const Frame &f)
    {
        if (t != token)
            return;
        pending = false;
        if (f.image.isNull())
            return;   // undecodable file: stay on the last frame
        queue.enqueue(f);
        if (waiting)
            show();
        request();
    }
    void show()
    {
        waiting = false;
        const Frame f = queue.dequeue();
        const QString url = store->put(generation, QStringLiteral("frame"), f.image);
        timer.start(qMax(kMinDelayMs, f.delay));
        Q_EMIT q->frameReady(url);
        request();
    }
    void tick()
    {
        if (queue.isEmpty())
            waiting = true;
        else
            show();
    }
};

FrameAnimator::FrameAnimator(ImageStore *store, QObject *parent) : QObject(parent), d(std::make_unique<Private>())
{
    d->q = this;
    d->store = store;
    d->timer.setSingleShot(true);
    connect(&d->timer, &QTimer::timeout, this, [this] { d->tick(); });
}

FrameAnimator::~FrameAnimator()
{
    stop();
}

void FrameAnimator::start(quint64 generation, const QString &path, QSize targetPx)
{
    stop();
    if (!targetPx.isValid())
        return;
    d->generation = generation;
    d->thread = new QThread;
    d->worker = new Worker;
    d->worker->path = path;
    d->worker->target = targetPx;
    d->worker->moveToThread(d->thread);
    connect(d->thread, &QThread::finished, d->worker, &QObject::deleteLater);
    d->thread->start();
    d->waiting = true;
    d->request();
}

void FrameAnimator::stop()
{
    ++d->token;
    d->timer.stop();
    d->queue.clear();
    d->pending = false;
    d->waiting = false;
    d->worker = nullptr;
    if (d->thread) {
        d->thread->quit();
        d->thread->wait();
        delete d->thread;
        d->thread = nullptr;
    }
}

bool FrameAnimator::running() const
{
    return d->thread != nullptr;
}

} // namespace ql
