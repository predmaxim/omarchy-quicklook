#include "PdfModel.h"
#include "Pdf.h"
#include "Pools.h"
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QQuickImageResponse>
#include <QQuickTextureFactory>
#include <QRunnable>
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

class PageResponse : public QQuickImageResponse, public QRunnable {
    Q_OBJECT
public:
    PageResponse(quint64 gen, int page, int width) : m_gen(gen), m_page(page), m_width(width) { setAutoDelete(false); }
    QQuickTextureFactory *textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(m_image); }
    QString errorString() const override { return m_error; }
    void cancel() override { m_abort = true; }

    void run() override
    {
        QSizeF pt;
        if (pools::isCurrent(m_gen) && !m_abort) {
            m_image = pdf::renderPage(PdfModel::documentPath(m_gen), m_page, m_width, &m_abort, &pt);
            if (m_image.isNull() && !m_abort && pools::isCurrent(m_gen))
                m_error = QStringLiteral("render failed");
            else if (!pt.isEmpty() && !m_abort)
                Q_EMIT pageMeasured(m_gen, m_page, pt);
        }
        // posted to the response's own thread, last action: the engine deletes us only after finished()
        QMetaObject::invokeMethod(this, [this] { Q_EMIT finished(); }, Qt::QueuedConnection);
    }

Q_SIGNALS:
    void pageMeasured(quint64 generation, int page, QSizeF pagePt);

private:
    quint64 m_gen;
    int m_page;
    int m_width;
    std::atomic_bool m_abort{false};
    QImage m_image;
    QString m_error;
};

} // namespace

PdfPageProvider::PdfPageProvider(PdfModel *model) : m_model(model) {}

QQuickImageResponse *PdfPageProvider::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    const QStringList parts = id.split(u'/');
    bool okGen = false, okPage = false;
    const quint64 gen = parts.value(0).toULongLong(&okGen);
    const int page = parts.value(1).toInt(&okPage);
    auto *r = new PageResponse(okGen && okPage ? gen : 0, okPage ? page : -1, requestedSize.width() > 0 ? requestedSize.width() : 1000);
    // queued connection: dropped if the model dies while a render is running
    QObject::connect(r, &PageResponse::pageMeasured, m_model, &PdfModel::setPageSize, Qt::QueuedConnection);
    pools::heavy()->start(r);
    return r;
}

} // namespace ql

#include "PdfModel.moc"
