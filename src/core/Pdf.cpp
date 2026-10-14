#include "Pdf.h"
#include <poppler-qt6.h>
#include <algorithm>
#include <memory>

namespace ql::pdf {

namespace {
constexpr int kMaxSide = 16384;   // a strip page requested with page 0's aspect would make Splash allocate gigabytes

std::unique_ptr<Poppler::Document> load(const QString &path)
{
    return std::unique_ptr<Poppler::Document>(Poppler::Document::load(path).release());
}
}

Header probe(const QString &path)
{
    Header h;
    const auto doc = load(path);
    if (!doc)
        return h;
    h.ok = true;
    if (doc->isLocked()) {
        h.locked = true;
        return h;
    }
    h.pages = doc->numPages();
    const std::unique_ptr<Poppler::Page> p(h.pages > 0 ? doc->page(0) : nullptr);
    h.firstPagePt = p ? p->pageSizeF() : QSizeF();
    if (!p || h.firstPagePt.isEmpty())
        h = Header{};
    return h;
}

QImage renderPage(const QString &path, int page, int widthPx, const std::atomic_bool *abort, QSizeF *pagePt)
{
    const auto aborted = [abort] { return abort && abort->load(); };
    const auto doc = load(path);
    if (!doc || doc->isLocked() || aborted())   // loading cannot be interrupted; poppler polls the flag only every ~10 operations
        return {};
    doc->setRenderBackend(Poppler::Document::SplashBackend);
    doc->setRenderHint(Poppler::Document::Antialiasing, true);
    doc->setRenderHint(Poppler::Document::TextAntialiasing, true);
    const std::unique_ptr<Poppler::Page> p(doc->page(page));
    if (!p)
        return {};
    const QSizeF pt = p->pageSizeF();
    if (pt.isEmpty())
        return {};
    if (pagePt)
        *pagePt = pt;
    double dpi = 72.0 * widthPx / pt.width();
    dpi = std::min(dpi, 72.0 * kMaxSide / std::max(pt.width(), pt.height()));
    const auto shouldAbort = [](const QVariant &closure) {
        const auto *flag = reinterpret_cast<const std::atomic_bool *>(closure.value<quintptr>());
        return flag && flag->load();
    };
    return p->renderToImage(dpi, dpi, -1, -1, -1, -1, Poppler::Page::Rotate0, nullptr, nullptr, shouldAbort,
                            QVariant::fromValue(reinterpret_cast<quintptr>(abort)));
}

} // namespace ql::pdf
