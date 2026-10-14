#pragma once
// Synchronous image provider "ql" (spec: "Картинки").
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

namespace ql {

class ImageStore : public QQuickImageProvider {
public:
    ImageStore();
    // id "<gen>/<slot>/<serial>"; repeated requests of the same id return the same image (DPR change re-requests)
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
    QString put(quint64 generation, const QString &slot, const QImage &image);  // -> "image://ql/<gen>/<slot>/<serial>"
    void dropOlderThan(quint64 generation);
    int count() const;   // tests

private:
    mutable QMutex m_mutex;
    QHash<QString, QImage> m_images;
    quint64 m_serial = 0;
};

} // namespace ql
