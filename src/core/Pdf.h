#pragma once
// PDF via poppler-qt6 (spec: "PDF и офис", "Просмотрщики → PDF").
#include <QImage>
#include <QSizeF>
#include <QString>
#include <atomic>

namespace ql::pdf {

struct Header {
    bool ok = false;
    bool locked = false;
    int pages = 0;
    QSizeF firstPagePt;
};

Header probe(const QString &path);   // Document::load + page(0)->pageSizeF(); nullptr -> ok = false
QImage renderPage(const QString &path, int page, int widthPx, const std::atomic_bool *abort, QSizeF *pagePt);
        // own Poppler::Document per call, Splash, Antialiasing + TextAntialiasing; white paper; null if aborted/failed

} // namespace ql::pdf
