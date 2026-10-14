#include "PdfModel.h"
#include "Pdf.h"
#include "Pools.h"
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QQuickImageResponse>
#include <QQuickTextureFactory>
#include <QCoreApplication>
#include <QPointer>
#include <QRunnable>
#include <memory>
#include <atomic>

namespace ql {

namespace {
QMutex g_mutex;
QHash<quint64, QString> g_paths;
constexpr QSizeF kA4{595, 842};
}

PdfModel::PdfModel(QObject *parent) : QAbstractListModel(parent) {}

void PdfModel::setDocument(quint64 generation, const QString &path, int pages, QSizeF firstPagePt)
{
    if (firstPagePt.isEmpty())
        firstPagePt = kA4;
    beginResetModel();
    m_generation = generation;
    m_pages = std::max(pages, 0);
    m_sizes = QVector<QSizeF>(m_pages, firstPagePt);
    {
        QMutexLocker lock(&g_mutex);
        for (auto it = g_paths.begin(); it != g_paths.end();)
            it = it.key() < generation ? g_paths.erase(it) : std::next(it);
        g_paths.insert(generation, path);
    }
    endResetModel();
    Q_EMIT countChanged();
}

void PdfModel::clear()
{
    beginResetModel();
    {
        QMutexLocker lock(&g_mutex);
        g_paths.remove(m_generation);
    }
    m_generation = 0;
    m_pages = 0;
    m_sizes.clear();
    endResetModel();
    Q_EMIT countChanged();
}

void PdfModel::setPageSize(quint64 generation, int page, QSizeF pagePt)
{
    if (generation != m_generation || page < 0 || page >= m_pages || pagePt.isEmpty() || m_sizes[page] == pagePt)
        return;
    m_sizes[page] = pagePt;
    const QModelIndex i = index(page);
    Q_EMIT dataChanged(i, i, {AspectRole});
}

QSizeF PdfModel::pageSize(int page) const
{
    return page >= 0 && page < m_pages ? m_sizes[page] : QSizeF();
}

QString PdfModel::documentPath(quint64 generation)
{
    QMutexLocker lock(&g_mutex);
    return g_paths.value(generation);
}

int PdfModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_pages; }

QVariant PdfModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_pages)
        return {};
    switch (role) {
    case PageRole: return index.row();
    case AspectRole: return m_sizes[index.row()].height() / m_sizes[index.row()].width();
    case SourceRole: return QStringLiteral("image://pdf/%1/%2").arg(m_generation).arg(index.row());
    }
    return {};
}

QHash<int, QByteArray> PdfModel::roleNames() const
{
    return {{PageRole, "page"}, {AspectRole, "aspect"}, {SourceRole, "source"}};
}

namespace {

struct PageState {
    std::atomic_bool abort{false};
    QImage image;
    QString error;
    QSizeF pagePt;
};

class PageResponse : public QQuickImageResponse {
    Q_OBJECT
public:
    explicit PageResponse(std::shared_ptr<PageState> st) : m_state(std::move(st)) {}
    QQuickTextureFactory *textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(m_state->image); }
    QString errorString() const override { return m_state->error; }
    void cancel() override { m_state->abort = true; }

    // runs on the response's thread: measured size first, finished() exactly once, last
    void complete(quint64 gen, int page)
    {
        if (!m_state->pagePt.isEmpty())
            Q_EMIT pageMeasured(gen, page, m_state->pagePt);
        Q_EMIT finished();
    }

Q_SIGNALS:
    void pageMeasured(quint64 generation, int page, QSizeF pagePt);

private:
    std::shared_ptr<PageState> m_state;   // shared with the job: a response deleted early is never touched by the worker
};

class PageJob : public QRunnable {
public:
    PageJob(std::shared_ptr<PageState> st, QPointer<PageResponse> r, quint64 gen, int page, int width)
        : m_state(std::move(st)), m_response(std::move(r)), m_gen(gen), m_page(page), m_width(width) {}
    void run() override
    {
        PageState &s = *m_state;
        if (pools::isCurrent(m_gen) && !s.abort) {
            s.image = pdf::renderPage(PdfModel::documentPath(m_gen), m_page, m_width, &s.abort, &s.pagePt);
            if (s.abort || !pools::isCurrent(m_gen)) {
                s.image = {};
                s.pagePt = {};
            } else if (s.image.isNull()) {
                s.error = QStringLiteral("render failed");
            }
        }
        // post to the long-lived application object, never to the response: it may be gone by now
        QMetaObject::invokeMethod(QCoreApplication::instance(),
                                  [r = m_response, g = m_gen, p = m_page] { if (r) r->complete(g, p); },
                                  Qt::QueuedConnection);
    }
private:
    std::shared_ptr<PageState> m_state;
    QPointer<PageResponse> m_response;
    quint64 m_gen;
    int m_page;
    int m_width;
};

} // namespace

PdfPageProvider::PdfPageProvider(PdfModel *model) : m_model(model) {}

QQuickImageResponse *PdfPageProvider::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    const QStringList parts = id.split(u'/');
    bool okGen = false, okPage = false;
    const quint64 gen = parts.value(0).toULongLong(&okGen);
    const int page = parts.value(1).toInt(&okPage);
    const auto st = std::make_shared<PageState>();
    auto *r = new PageResponse(st);
    const quint64 g = okGen && okPage ? gen : 0;
    const int pg = okPage ? page : -1;
    // queued connection: dropped if the model dies while a render is running
    QObject::connect(r, &PageResponse::pageMeasured, m_model, &PdfModel::setPageSize, Qt::QueuedConnection);
    pools::heavy()->start(new PageJob(st, r, g, pg, requestedSize.width() > 0 ? requestedSize.width() : 1000));
    return r;
}

} // namespace ql

#include "PdfModel.moc"
