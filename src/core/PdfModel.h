#pragma once
// Pages model + async page provider "pdf" (spec: "Просмотрщики → PDF").
#include <QAbstractListModel>
#include <QQuickAsyncImageProvider>
#include <QSizeF>
#include <QVector>

namespace ql {

class PdfModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Roles { PageRole = Qt::UserRole + 1, AspectRole, SourceRole };

    explicit PdfModel(QObject *parent = nullptr);

    void setDocument(quint64 generation, const QString &path, int pages, QSizeF firstPagePt);  // all aspects = page 0
    void clear();
    Q_INVOKABLE void setPageSize(quint64 generation, int page, QSizeF pagePt);  // queued from render jobs; GUI thread
    int count() const { return m_pages; }
    QSizeF pageSize(int page) const;

    static QString documentPath(quint64 generation);   // thread-safe registry read by the provider

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;   // Aspect = h / w; Source = "image://pdf/<gen>/<page>"
    QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void countChanged();

private:
    quint64 m_generation = 0;
    int m_pages = 0;
    QVector<QSizeF> m_sizes;
};

class PdfPageProvider : public QQuickAsyncImageProvider {
public:
    explicit PdfPageProvider(PdfModel *model);
    // id "<gen>/<page>"; width from requestedSize (QML sets sourceSize); runs on pools::heavy();
    // response emits finished() on every outcome, also after cancel(); reports the page size to the model
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

private:
    PdfModel *m_model;
};

} // namespace ql
