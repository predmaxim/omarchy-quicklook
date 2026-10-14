#include "ImageStore.h"
#include <QMutexLocker>

namespace ql {

namespace {
const QString kPrefix = QStringLiteral("image://ql/");
}

ImageStore::ImageStore() : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage ImageStore::requestImage(const QString &id, QSize *size, const QSize &)
{
    QMutexLocker lock(&m_mutex);
    const QImage img = m_images.value(id);
    if (size)
        *size = img.size();
    return img;
}

QString ImageStore::put(quint64 generation, const QString &slot, const QImage &image)
{
    QMutexLocker lock(&m_mutex);
    // one image per (generation, slot): looping animation frames must not pile up
    const QString head = QString::number(generation) + u'/' + slot + u'/';
    for (auto it = m_images.begin(); it != m_images.end();)
        it = it.key().startsWith(head) ? m_images.erase(it) : std::next(it);
    const QString id = head + QString::number(++m_serial);
    m_images.insert(id, image);
    return kPrefix + id;
}

void ImageStore::dropOlderThan(quint64 generation)
{
    QMutexLocker lock(&m_mutex);
    for (auto it = m_images.begin(); it != m_images.end();)
        it = it.key().section(u'/', 0, 0).toULongLong() < generation ? m_images.erase(it) : std::next(it);
}

int ImageStore::count() const
{
    QMutexLocker lock(&m_mutex);
    return int(m_images.size());
}

} // namespace ql
