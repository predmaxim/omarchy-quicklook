#pragma once
// Animated images: frames decoded on a dedicated thread, shown via ImageStore (spec: "Картинки", анимация).
#include <QObject>
#include <QSize>
#include <QString>
#include <memory>

namespace ql {

class ImageStore;

class FrameAnimator : public QObject {
    Q_OBJECT
public:
    explicit FrameAnimator(ImageStore *store, QObject *parent = nullptr);
    ~FrameAnimator() override;

    void start(quint64 generation, const QString &path, QSize targetPx);   // loops forever until stop()
    void stop();
    bool running() const;

Q_SIGNALS:
    void frameReady(const QString &source);   // image://ql/<gen>/frame/<serial>

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace ql
