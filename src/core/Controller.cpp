#include "Controller.h"
#include "FileInfo.h"
#include "FrameAnimator.h"
#include "Geometry.h"
#include "Hyprland.h"
#include "ImageStore.h"
#include "Launcher.h"
#include "MediaController.h"
#include "OfficeCache.h"
#include "OfficeConverter.h"
#include "PdfModel.h"
#include "Pools.h"
#include "PreviewerService.h"
#include "Probe.h"
#include "StateStore.h"
#include "TextModel.h"
#include "Theme.h"
#include <KArchiveDirectory>
#include <KArchiveFile>
#include <KSyntaxHighlighting/Repository>
#include <KZip>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFontMetricsF>
#include <QIcon>
#include <QImageReader>
#include <QMediaPlayer>
#include <QMimeDatabase>
#include <QMouseEvent>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <QVideoSink>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace ql {

namespace {

constexpr int kGraceMs = 40;           // image decode (spec "Показ" 3)
constexpr int kProbeBudgetMs = 80;     // SVG / PDF page 0 / office prediction
constexpr int kMediaBudgetMs = 300;    // LoadedMedia
constexpr int kHintMs = 2000;
constexpr int kTrashWaitMs = 500;      // Del: how long Files may take to move its selection before we try the other way
constexpr qint64 kMaxZoomDecodePx = 40'000'000;   // full-size decode for zoom: 160 MB at 4 bytes a pixel
constexpr qint64 kSeekMs = 10000;
constexpr int kCardMargin = 24;        // FileCard.qml / AudioCard.qml use the same numbers
constexpr int kCardIcon = 96;
constexpr int kCardGap = 16;
constexpr int kCoverSide = 132;
constexpr qint64 kMaxOdfThumbnail = 4 * 1024 * 1024;   // read at most this much: a zip entry may lie about its size
constexpr int kMaxCountedItems = 10001;                // itemsText shows "10 000+" above 10 000

// Probe-pool result handed to the GUI thread.
struct Detail {
    ProbeResult r;
    QImage cover;        // audio: decoded at 132 px * scale
    QStringList rows;    // text: split off the GUI thread
};

// ODF's built-in preview, the placeholder of an office cache miss (spec "Офис").
QImage odfThumbnail(const QString &path)
{
    KZip zip(path);
    if (!zip.open(QIODevice::ReadOnly))
        return {};
    const auto *entry = dynamic_cast<const KArchiveFile *>(zip.directory()->entry(QStringLiteral("Thumbnails/thumbnail.png")));
    if (!entry || entry->size() > kMaxOdfThumbnail)
        return {};
    const std::unique_ptr<QIODevice> device(entry->createDevice());
    if (!device)
        return {};
    const QByteArray png = device->read(kMaxOdfThumbnail + 1);
    return png.size() > kMaxOdfThumbnail ? QImage() : QImage::fromData(png, "PNG");
}

QImage coverOf(const ProbeResult &r)
{
    if (r.audio.cover.isEmpty())
        return {};
    const int side = qRound(kCoverSide * r.monitor.scale);
    return image::decodeCover(r.audio.cover, r.audio.coverMime, QSize(side, side));
}

Detail detail(ProbeResult r)
{
    if (!pools::isCurrent(r.generation))
        return Detail{std::move(r), {}, {}};   // stale: quit at once
    detailStage(r);
    Detail d{std::move(r), {}, {}};
    if (d.r.kind == Kind::Text)
        d.rows = text::splitRows(d.r.text.text);
    else if (d.r.kind == Kind::Audio)
        d.cover = coverOf(d.r);
    else if (d.r.kind == Kind::Office && d.r.placeholder.isNull() && d.r.office.family == office::Family::Odf)
        d.r.placeholder = odfThumbnail(d.r.file.path);
    return d;
}

Detail audioDetail(ProbeResult r)   // a media file without a video track
{
    if (!pools::isCurrent(r.generation))
        return Detail{std::move(r), {}, {}};
    r.audio = readAudioTags(r.file.path);
    Detail d{std::move(r), {}, {}};
    d.cover = coverOf(d.r);
    return d;
}

int countItems(const QString &path)
{
    int n = 0;
    for (QDirIterator it(path, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System); n < kMaxCountedItems && it.hasNext(); it.next())
        ++n;
    return n;
}

// Double click anywhere opens the file. A window filter, never consuming: a TapHandler at the root never fires
// above a ListView, and one stacked over the views blocks ListView dragging.
struct DoubleClicks : QObject {
    std::function<void()> onDoubleClick;
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::MouseButtonDblClick && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton)
            onDoubleClick();
        return false;
    }
};

struct Sources {
    QString image{}, placeholder{}, cover{};   // {} : designated initializers may omit them without a warning
    bool busy = false;
};

} // namespace

struct Controller::Private {
    Private(Controller *q, PreviewerService *service, Session *session, const QProcessEnvironment &env)
        : q(q), session(session), service(service), converter(&officeCache, env), launcher(env) {}

    Controller *q;
    Session *session;
    PreviewerService *service;
    QPointer<QQuickWindow> window;
    std::unique_ptr<ImageStore> ownedStore = std::make_unique<ImageStore>();   // until the engine takes it
    ImageStore *store = ownedStore.get();
    PdfModel pdfModel;
    TextModel textModel;
    Theme theme;
    StateStore state;
    MediaController media{&state};
    OfficeCache officeCache;
    OfficeConverter converter;
    FrameAnimator animator{store};
    Launcher launcher;
    KeyRouter keys;
    DoubleClicks doubleClicks;
    QTimer loadTimer, graceTimer, budgetTimer, hintTimer, trashTimer;
    QElapsedTimer pendingSince;   // the first switch the screen has not caught up with: the grace bounds this wait

    ProbeResult cur;            // the current generation, as far as it got
    QSize videoSize;            // cur: from LoadedMedia
    QSize decodedPx;            // cur: target of the last image/SVG decode
    quint64 shownGeneration = 0;
    Layout layout;              // the shown file's own window and content (kept through full screen)
    bool fullScreen = false;

    // what QML shows
    Kind kind = Kind::None;
    QSizeF natural;             // fullscreenContent input: image px, SVG size, page pt, video px
    double scale = 1.0;
    QRectF contentRect, cardRect;
    QString imageSource, placeholderSource, coverSource, cardIcon;
    QString cardName, cardType, cardDetails, cardReason, audioTitle, audioSubtitle, hint;
    bool busy = false;
    FileInfo shownFile;         // Enter, Del, card name
    int zoomLevel = 0;          // zoom = 1.25^zoomLevel within the kind's range
    double zoom = 1;
    QString trashTarget;        // Del: waits for Files to show another file
    bool trashTriedLeft = false;

    bool applied() const { return cur.generation && shownGeneration == cur.generation; }
    bool current(quint64 generation) const { return pools::isCurrent(generation) && cur.generation == generation; }

    // Full screen, or a size the compositor set and we did not: Hyprland "maximized" (Omarchy Super+Alt+F),
    // tiling... Hyprland reports Maximized for every window, so windowStates cannot tell; the sign is the actual
    // size != the file's size. The content then fits the actual window; min/max are left alone (spec "Размер окна").
    bool foreignSize() const
    {
        return window && window->isVisible() && (fullScreen || window->size() != layout.window);
    }

    QSizeF naturalOf(const ProbeResult &r) const
    {
        switch (r.kind) {
        case Kind::Image: return QSizeF(r.image.displayPx);
        case Kind::Svg: return r.svgSize;
        case Kind::Pdf: return r.pdf.firstPagePt;
        case Kind::Office: return r.office.firstPagePt;
        case Kind::Video: return QSizeF(videoSize);
        default: return {};
        }
    }

    // Where r's content will be drawn: its own layout, or fitted into the window that keeps its size
    // (full screen, a fallback already on screen, a compositor size).
    QRectF placedContent(const ProbeResult &r) const
    {
        QSizeF nat = naturalOf(r);
        if (nat.isEmpty())
            nat = r.layout.content.size();
        if (fullScreen && window)
            return geometry::fullscreenContent(r.kind, window->size(), nat, r.monitor.scale);
        if (applied())
            return geometry::fullscreenContent(r.kind, foreignSize() ? window->size() : layout.window, nat, r.monitor.scale);
        return r.layout.content;
    }

    void setWindowSize(QSize s)
    {
        const QSize c = window->size();
        if (s.width() <= c.width() && s.height() <= c.height()) {
            window->setMinimumSize(s);
            window->setMaximumSize(s);
        } else if (s.width() >= c.width() && s.height() >= c.height()) {
            window->setMaximumSize(s);
            window->setMinimumSize(s);
        } else {
            window->setMinimumSize(c.boundedTo(s));
            window->setMaximumSize(c.expandedTo(s));
            window->setMinimumSize(s);
            window->setMaximumSize(s);
        }
    }

    QString cardNameFor(double cardWidth) const
    {
        const QString name = shownFile.displayName;
        QFont font;
        font.setPixelSize(theme.titleSize());
        const QFontMetricsF fm(font);
        const bool narrow = cardWidth < geometry::kFileCard.width();   // icon above the text, one line
        const double width = narrow ? cardWidth - 2 * kCardMargin : cardWidth - 2 * kCardMargin - kCardIcon - kCardGap;
        if (fm.horizontalAdvance(name) <= width)
            return name;
        if (narrow)
            return fm.elidedText(name, Qt::ElideMiddle, width);
        qsizetype lo = 1, hi = name.size() - 1;   // longest prefix that fits
        while (lo < hi) {
            const qsizetype mid = (lo + hi + 1) / 2;
            if (fm.horizontalAdvance(name.left(mid)) <= width)
                lo = mid;
            else
                hi = mid - 1;
        }
        if (lo > 1 && name.at(lo - 1).isHighSurrogate())
            --lo;
        return name.left(lo) + u'\n' + fm.elidedText(name.mid(lo), Qt::ElideMiddle, width);
    }

    void updateRects()
    {
        const bool fitted = foreignSize();
        const QSize size = fitted ? window->size() : layout.window;
        contentRect = fitted ? geometry::fullscreenContent(kind, size, natural, scale) : layout.content;
        cardRect = geometry::cardRectIn(size, kind == Kind::Audio ? Kind::Audio : Kind::FileCard);
        const bool named = kind == Kind::FileCard || kind == Kind::FolderCard || kind == Kind::Office;
        cardName = named ? cardNameFor(cardRect.width()) : QString();
    }

    void fillCard(const ProbeResult &r)
    {
        const bool ru = uiRussian();
        cardReason = reasonText(r.reason, ru);
        cardType.clear();
        cardDetails.clear();
        cardIcon.clear();
        audioTitle.clear();
        audioSubtitle.clear();
        if (r.kind != Kind::FileCard && r.kind != Kind::FolderCard && r.kind != Kind::Office && r.kind != Kind::Audio)
            return;
        const QMimeDatabase db;
        QMimeType mime = db.mimeTypeForName(r.file.mimeName);
        if (!mime.isValid())
            mime = db.mimeTypeForName(QStringLiteral("application/octet-stream"));
        cardType = mime.comment();
        QStringList details;
        if (r.kind != Kind::FolderCard && r.file.size >= 0)
            details << (r.file.size == 0 ? uiText(Str::EmptyFile, ru) : formatSize(r.file.size, ru));
        if (r.file.modified.isValid())
            details << formatDate(r.file.modified, ru);
        cardDetails = details.join(QStringLiteral(" · "));
        const QIcon icon = QIcon::fromTheme(mime.iconName(), QIcon::fromTheme(mime.genericIconName()));
        const int px = qRound((r.kind == Kind::Audio ? kCoverSide : kCardIcon) * r.monitor.scale);   // audio: in the cover's place
        if (!icon.isNull())
            cardIcon = store->put(r.generation, QStringLiteral("icon"), icon.pixmap(QSize(px, px)).toImage());
        if (r.kind == Kind::Audio) {
            audioTitle = r.audio.title.isEmpty() ? r.file.displayName : r.audio.title;
            QStringList parts;
            for (const QString &s : {r.audio.artist, r.audio.album})
                if (!s.isEmpty())
                    parts << s;
            audioSubtitle = parts.join(QStringLiteral(" — "));
        }
    }

    // "Готово к показу" in one pass: size, title, content or placeholder, show(). A generation already on
    // screen keeps its window: later content or an error card is fitted into it.
    void apply(const ProbeResult &r, Layout l, const Sources &s, const std::function<void()> &fill = {})
    {
        const quint64 generation = r.generation;
        const bool again = shownGeneration == generation;
        animator.stop();
        if (r.kind != Kind::Video && r.kind != Kind::Audio)
            media.release();
        if (r.kind != Kind::Text)
            textModel.clear();
        if (r.kind != Kind::Pdf)
            pdfModel.clear();

        kind = r.kind;
        scale = r.monitor.scale;
        natural = naturalOf(r);
        if (natural.isEmpty())
            natural = l.content.size();
        if (again)
            l = {layout.window, geometry::fullscreenContent(kind, layout.window, natural, scale)};
        layout = l;
        imageSource = s.image;
        placeholderSource = s.placeholder;
        coverSource = s.cover;
        busy = s.busy;
        shownFile = r.file;
        fillCard(r);
        if (!fullScreen)
            setWindowSize(l.window);
        updateRects();
        window->setTitle(r.file.displayName);
        if (!window->isVisible()) {
            window->resize(l.window);
            window->show();
        }
        if (!again)
            resetZoom();
        shownGeneration = generation;
        pendingSince.invalidate();
        store->dropOlderThan(generation);
        Q_EMIT q->contentChanged();
        Q_EMIT q->imageChanged();
        if (fill)
            fill();   // models after the size: rows are laid out at the final width
        if (!again)
            Q_EMIT q->shown(generation);
    }

    QString put(const char *slot, const QImage &image)
    {
        return image.isNull() ? QString() : store->put(cur.generation, QLatin1String(slot), image);
    }

    void showError(CardReason reason)
    {
        cur.kind = Kind::FileCard;
        cur.reason = reason;
        apply(cur, geometry::cardLayout(Kind::FileCard), {});
    }

    void hidePreview()
    {
        loadTimer.stop();
        graceTimer.stop();
        budgetTimer.stop();
        if (window) {
            window->hide();
            if (window->windowStates() & Qt::WindowFullScreen)
                window->setWindowStates(Qt::WindowNoState);   // the next show is a normal one
        }
        const bool wasFull = fullScreen;
        fullScreen = false;
        media.release();
        animator.stop();
        textModel.clear();
        pdfModel.clear();
        cur = ProbeResult();
        videoSize = decodedPx = QSize();
        kind = Kind::None;
        imageSource.clear();
        placeholderSource.clear();
        coverSource.clear();
        cardIcon.clear();
        cardReason.clear();
        busy = false;
        resetZoom();
        store->dropOlderThan(pools::currentGeneration());
        Q_EMIT q->contentChanged();
        Q_EMIT q->imageChanged();
        if (wasFull)
            Q_EMIT q->fullScreenChanged();
    }

    void onDecision(const Session::Decision &decision)
    {
        pools::currentGeneration() = decision.generation;
        if (!trashTarget.isEmpty())
            trashNow();   // Files has moved its selection (or the preview closes): the file can go now
        graceTimer.stop();
        budgetTimer.stop();
        if (!hint.isEmpty()) {   // it was about the file that is going away
            hintTimer.stop();
            hint.clear();
            Q_EMIT q->hintChanged();
        }
        if (decision.effect == Session::Effect::Close) {
            pendingSince.invalidate();
            hidePreview();
        } else if (decision.effect == Session::Effect::Open || decision.effect == Session::Effect::Switch) {
            if (!pendingSince.isValid())   // the screen was up to date: the user's wait starts now
                pendingSince.start();
            loadTimer.start();   // coalesces a batch of calls into one load of the final address
        }
    }

    void startLoad()
    {
        if (!session->visible())
            return;
        const ProbeRequest request{session->generation(), session->uri(),
                                   window && window->isVisible() && window->screen() ? window->screen()->name() : QString(),
                                   hyprland::screenMonitor()};
        QtConcurrent::run(pools::probe(), [request] {
            return pools::isCurrent(request.generation) ? classifyStage(request) : ProbeResult();
        }).then(q, [this](const ProbeResult &r) { onClassified(r); });
    }

    void onClassified(const ProbeResult &r)
    {
        if (!pools::isCurrent(r.generation))
            return;
        cur = r;
        videoSize = decodedPx = QSize();
        if (r.kind == Kind::Svg || r.kind == Kind::Pdf || r.kind == Kind::Office)
            budgetTimer.start(kProbeBudgetMs);
        QtConcurrent::run(pools::probe(), [r]() mutable { return detail(std::move(r)); })
            .then(q, [this](Detail d) { onDetailed(std::move(d)); });
    }

    void onBudget()   // the probe or LoadedMedia missed its budget: the fallback size with a placeholder only
    {
        if (!pools::isCurrent(cur.generation) || applied())
            return;
        const bool video = cur.kind == Kind::Video;
        apply(cur, fallbackFor(cur), {.placeholder = video ? put("placeholder", cur.placeholder) : QString()});
        if (video)
            media.activate(cur.generation, false);   // the previous player must not keep playing behind it
    }

    void onDetailed(Detail d)
    {
        if (!pools::isCurrent(d.r.generation))
            return;
        if (d.r.kind != Kind::Video)
            budgetTimer.stop();
        cur = std::move(d.r);
        const quint64 generation = cur.generation;
        switch (cur.kind) {
        case Kind::Image:
        case Kind::Svg:
            if (applied())
                apply(cur, cur.layout, {});   // the 4:3 fallback is on screen: refit to the real aspect
            startDecode((placedContent(cur).size() * cur.monitor.scale).toSize());
            return;
        case Kind::Pdf:   // also an office cache hit: the probe hands it over as Pdf with officeCachedPdf
            showPdf(cur.officeCachedPdf.isEmpty() ? cur.file.path : cur.officeCachedPdf);
            return;
        case Kind::Office:   // cache miss: white paper, type icon, name, busy indicator until the PDF is ready
            apply(cur, cur.layout, {.placeholder = put("placeholder", cur.placeholder), .busy = true});
            converter.convert(generation, cur.file.path);
            return;
        case Kind::Text: {
            KSyntaxHighlighting::Definition definition;
            if (cur.highlight) {
                auto &repository = TextModel::repository();
                definition = repository.definitionForFileName(cur.file.displayName);
                if (!definition.isValid())
                    definition = repository.definitionForMimeType(cur.file.mimeName);
            }
            const QString footer = cur.text.truncated
                ? truncatedHint(cur.text.shownBytes, cur.text.totalBytes, uiRussian()) : QString();
            const QStringList rows = std::move(d.rows);
            apply(cur, cur.layout, {}, [&] { textModel.setContent(rows, definition, footer); });
            return;
        }
        case Kind::Audio:
            apply(cur, cur.layout, {.cover = put("cover", d.cover)});
            media.load(generation, QUrl::fromLocalFile(cur.file.path));
            media.activate(generation, true);
            return;
        case Kind::Video:
            media.load(generation, QUrl::fromLocalFile(cur.file.path));
            budgetTimer.start(kMediaBudgetMs);
            return;
        case Kind::FolderCard:
            apply(cur, cur.layout, {});
            countFolder();
            return;
        default:
            apply(cur, cur.layout, {});
            return;
        }
    }

    void startDecode(QSize px)
    {
        const quint64 generation = cur.generation;
        const QString path = cur.file.path;
        decodedPx = px;
        auto job = cur.kind == Kind::Svg
            ? QtConcurrent::run(pools::heavy(), [generation, path, px] {
                  return pools::isCurrent(generation) ? image::renderSvg(path, px) : QImage();
              })
            : QtConcurrent::run(pools::heavy(), [generation, path, header = cur.image, px] {
                  return image::decodeIfCurrent(generation, path, header, px);
              });
        job.then(q, [this, generation](const QImage &image) { onDecoded(generation, image); });
        if (!applied()) {   // a held arrow restarts nothing: the grace counts from the first unshown switch
            const qint64 waited = pendingSince.isValid() ? pendingSince.elapsed() : 0;
            graceTimer.start(int(std::max<qint64>(0, kGraceMs - waited)));
        }
    }

    void onGrace()   // the decode is late: show the placeholder now, the image replaces it when it lands
    {
        if (!pools::isCurrent(cur.generation) || applied())
            return;
        apply(cur, cur.layout, {.placeholder = put("placeholder", cur.placeholder)});
    }

    void onDecoded(quint64 generation, const QImage &image)
    {
        if (!current(generation) || (cur.kind != Kind::Image && cur.kind != Kind::Svg))
            return;
        const bool first = !applied() || imageSource.isEmpty();
        if (image.isNull()) {
            if (first)
                showError(CardReason::CannotOpen);   // a failed re-decode keeps the image on screen
            return;
        }
        const QString source = store->put(generation, QStringLiteral("main"), image);
        if (!applied()) {
            graceTimer.stop();
            apply(cur, cur.layout, {.image = source});
        } else {
            imageSource = source;
            Q_EMIT q->imageChanged();
        }
        if (first && cur.kind == Kind::Image && cur.image.animated)
            animator.start(generation, cur.file.path, decodedPx);
    }

    // Full screen or a compositor size made the rect larger than what was decoded: decode again for it.
    void redecodeIfLarger()
    {
        if ((kind != Kind::Image && kind != Kind::Svg) || !applied() || cur.kind != kind || animator.running()
            || imageSource.isEmpty())
            return;
        const QSize px = (contentRect.size() * scale).toSize();
        if (px.width() > decodedPx.width() || px.height() > decodedPx.height())
            startDecode(px);
    }

    geometry::ZoomRange zoomRange() const
    {
        return busy ? geometry::ZoomRange() : geometry::zoomRange(kind, natural, scale, contentRect.size());
    }

    void setZoomLevel(int level)   // a Ctrl +/-/0 step
    {
        const double from = zoom;
        zoomLevel = level;
        zoom = geometry::zoomAt(level, zoomRange());
        if (zoom == from)
            return;
        Q_EMIT q->zoomChanged();
        Q_EMIT q->zoomStepped(from);
        if (zoom > 1)
            decodeForZoom();
    }

    void resetZoom()
    {
        zoomLevel = 0;
        if (zoom == 1)
            return;
        zoom = 1;
        Q_EMIT q->zoomChanged();
    }

    // The first zoom past the fit: decode once at the size the deepest zoom shows (an image at most at its own
    // size); animations keep their fitted frames.
    void decodeForZoom()
    {
        if ((kind != Kind::Image && kind != Kind::Svg) || !applied() || cur.kind != kind || animator.running()
            || imageSource.isEmpty())
            return;
        QSize px = (contentRect.size() * zoomRange().max * scale).toSize();
        if (kind == Kind::Image)
            px = px.boundedTo(cur.image.displayPx);
        const qint64 area = qint64(px.width()) * px.height();
        if (area > kMaxZoomDecodePx)   // ponytail: one capped decode, tiles if 40 MP+ photos must be pin-sharp
            px = (QSizeF(px) * std::sqrt(double(kMaxZoomDecodePx) / area)).toSize();
        if (px.width() > decodedPx.width() || px.height() > decodedPx.height())
            startDecode(px);
    }

    void showHint(Str id)
    {
        hint = uiText(id);
        Q_EMIT q->hintChanged();
        hintTimer.start();
    }

    // Del: Files loses its selection when the selected file vanishes, so it moves on first (right, then left at
    // the end of the list); the file goes to the Trash once Files shows another one. The only file: trash, close.
    void trashCurrent()
    {
        if (!applied() || !trashTarget.isEmpty() || shownFile.uri.isEmpty())
            return;
        if (QUrl(shownFile.uri).scheme() == QLatin1String("trash"))
            return showHint(Str::AlreadyInTrash);
        trashTarget = shownFile.path.isEmpty() ? shownFile.uri : shownFile.path;
        trashTriedLeft = false;
        service->emitSelectionEvent(selectionDirection(KeyAction::Right));
        trashTimer.start();
    }

    void onTrashWait()
    {
        if (!trashTriedLeft) {
            trashTriedLeft = true;
            service->emitSelectionEvent(selectionDirection(KeyAction::Left));
            trashTimer.start();
            return;
        }
        trashNow();
        service->requestClose();
    }

    void trashNow()
    {
        trashTimer.stop();
        QtConcurrent::run(pools::probe(), [target = std::exchange(trashTarget, QString())] { return trashFile(target); })
            .then(q, [this](bool ok) {
                if (!ok)
                    showHint(Str::TrashFailed);
            });
    }

    void showPdf(const QString &pdfPath)
    {
        const quint64 generation = cur.generation;
        const pdf::Header h = cur.pdf;
        apply(cur, cur.layout, {.placeholder = put("placeholder", cur.placeholder)},
              [&] { pdfModel.setDocument(generation, pdfPath, h.pages, h.firstPagePt); });
    }

    void onConverted(quint64 generation, const QString &pdfPath)
    {
        if (!current(generation) || cur.kind != Kind::Office)
            return;
        QtConcurrent::run(pools::probe(), [generation, pdfPath] {
            return pools::isCurrent(generation) ? pdf::probe(pdfPath) : pdf::Header();
        }).then(q, [this, generation, pdfPath](const pdf::Header &h) {
            if (!current(generation) || cur.kind != Kind::Office)
                return;
            if (!h.ok || h.locked || h.pages < 1)
                return showError(CardReason::ConversionFailed);
            cur.kind = Kind::Pdf;   // fitted into the predicted window: pageRect leaves margins if the shape differs
            cur.pdf = h;
            cur.officeCachedPdf = pdfPath;
            showPdf(pdfPath);
        });
    }

    void onConversionFailed(quint64 generation)
    {
        if (current(generation) && cur.kind == Kind::Office)
            showError(CardReason::ConversionFailed);
    }

    void onMediaLoaded(quint64 generation, bool ok, bool hasVideo, QSize size)
    {
        if (!current(generation) || (cur.kind != Kind::Video && cur.kind != Kind::Audio))
            return;
        budgetTimer.stop();
        if (!ok)
            return showError(CardReason::CannotOpen);   // InvalidMedia, video or audio (spec "Ошибки и пределы")
        if (cur.kind == Kind::Audio)
            return;   // the card is up and the player active already
        if (hasVideo) {
            videoSize = size;
            const bool active = applied();   // the 16:9 fallback activated it already
            apply(cur, size.isEmpty() ? fallbackFor(cur) : geometry::videoLayout(size, cur.monitor),
                  {.placeholder = active ? placeholderSource : put("placeholder", cur.placeholder)});
            if (!active)
                media.activate(generation, false);   // again would re-arm autoplay and undo a K pause
            return;
        }
        QtConcurrent::run(pools::probe(), [r = cur]() mutable { return audioDetail(std::move(r)); })
            .then(q, [this](Detail d) {
                if (!current(d.r.generation) || cur.kind != Kind::Video)
                    return;
                cur = std::move(d.r);
                cur.kind = Kind::Audio;
                apply(cur, geometry::cardLayout(Kind::Audio), {.cover = put("cover", d.cover)});
                media.activate(cur.generation, true);
            });
    }

    void countFolder()
    {
        const quint64 generation = cur.generation;
        QtConcurrent::run(pools::probe(), [generation, path = cur.file.path] {
            return pools::isCurrent(generation) ? countItems(path) : 0;
        }).then(q, [this, generation](int count) {
            if (!current(generation) || shownGeneration != generation || kind != Kind::FolderCard)
                return;
            const bool ru = uiRussian();
            QStringList details{itemsText(count, ru)};
            if (shownFile.modified.isValid())
                details << formatDate(shownFile.modified, ru);
            cardDetails = details.join(QStringLiteral(" · "));
            Q_EMIT q->contentChanged();
        });
    }

    void onResized()
    {
        if (!window->isVisible() || kind == Kind::None)
            return;
        const QRectF content = contentRect, card = cardRect;
        updateRects();
        if (content != contentRect || card != cardRect)
            Q_EMIT q->contentChanged();
        redecodeIfLarger();
    }

    void onWindowState(Qt::WindowState state)
    {
        const bool full = state == Qt::WindowFullScreen;
        if (full == fullScreen || (full && !window->isVisible()))
            return;
        fullScreen = full;
        resetZoom();   // the fit changes with the window
        if (!full) {   // back to the current file's own size, also after a switch made in full screen
            setWindowSize(layout.window);
            window->resize(layout.window);
        }
        updateRects();
        Q_EMIT q->contentChanged();
        Q_EMIT q->fullScreenChanged();
        redecodeIfLarger();
    }

    void onKeyAction(KeyAction action)
    {
        switch (action) {
        case KeyAction::Close:
            service->requestClose();
            break;
        case KeyAction::Open:
            q->openCurrent();
            break;
        case KeyAction::Left:
        case KeyAction::Right:
        case KeyAction::Up:
        case KeyAction::Down:
            service->emitSelectionEvent(selectionDirection(action));   // content stays until ShowFile
            break;
        case KeyAction::PageUp:
        case KeyAction::PageDown:
        case KeyAction::Home:
        case KeyAction::End:
            Q_EMIT q->scrollRequested(int(action));
            break;
        case KeyAction::FullScreen:
            if (fullScreen)
                window->showNormal();
            else
                window->showFullScreen();
            break;
        case KeyAction::PlayPause:
            q->togglePlay();
            break;
        case KeyAction::SeekBack:
            media.seekBy(-kSeekMs);
            break;
        case KeyAction::SeekForward:
            media.seekBy(kSeekMs);
            break;
        case KeyAction::Mute:
            if (kind == Kind::Video || kind == Kind::Audio)   // the remembered mute is about media only
                media.toggleMute();
            break;
        case KeyAction::Trash:
            trashCurrent();
            break;
        case KeyAction::ZoomIn:
            if (zoom < zoomRange().max)
                setZoomLevel(zoomLevel + 1);
            break;
        case KeyAction::ZoomOut:
            if (zoom > zoomRange().min)
                setZoomLevel(zoomLevel - 1);
            break;
        case KeyAction::ZoomFit:
            setZoomLevel(0);
            break;
        case KeyAction::PanLeft:
        case KeyAction::PanRight:
        case KeyAction::PanUp:
        case KeyAction::PanDown:
            if (zoomRange().max > 1)   // zoomable content only: images, PDF, text
                Q_EMIT q->panRequested(action == KeyAction::PanLeft ? -1 : action == KeyAction::PanRight ? 1 : 0,
                                       action == KeyAction::PanUp ? -1 : action == KeyAction::PanDown ? 1 : 0);
            break;
        case KeyAction::None:
            break;
        }
    }
};

Controller::Controller(PreviewerService *service, Session *session, const QProcessEnvironment &launchEnv, QObject *parent)
    : QObject(parent), d(std::make_unique<Private>(this, service, session, launchEnv))
{
    qRegisterMetaType<ql::ProbeResult>();
    qRegisterMetaType<ql::Session::Decision>();
    qRegisterMetaType<ql::KeyAction>();
    d->loadTimer.setSingleShot(true);
    d->loadTimer.setInterval(0);
    d->graceTimer.setSingleShot(true);
    d->budgetTimer.setSingleShot(true);
    d->hintTimer.setSingleShot(true);
    d->hintTimer.setInterval(kHintMs);
    d->trashTimer.setSingleShot(true);
    d->trashTimer.setInterval(kTrashWaitMs);
    connect(&d->trashTimer, &QTimer::timeout, this, [this] { d->onTrashWait(); });
    connect(service, &PreviewerService::decided, this, [this](const Session::Decision &decision) { d->onDecision(decision); });
    connect(&d->loadTimer, &QTimer::timeout, this, [this] { d->startLoad(); });
    connect(&d->graceTimer, &QTimer::timeout, this, [this] { d->onGrace(); });
    connect(&d->budgetTimer, &QTimer::timeout, this, [this] { d->onBudget(); });
    connect(&d->hintTimer, &QTimer::timeout, this, [this] {
        d->hint.clear();
        Q_EMIT hintChanged();
    });
    connect(&d->animator, &FrameAnimator::frameReady, this, [this](const QString &source) {
        d->imageSource = source;
        Q_EMIT imageChanged();
    });
    connect(&d->media, &MediaController::loaded, this,
            [this](quint64 generation, bool ok, bool hasVideo, QSize size) { d->onMediaLoaded(generation, ok, hasVideo, size); });
    connect(&d->converter, &OfficeConverter::converted, this,
            [this](quint64 generation, const QString &pdf) { d->onConverted(generation, pdf); });
    connect(&d->converter, &OfficeConverter::failed, this, [this](quint64 generation) { d->onConversionFailed(generation); });
    const auto colors = [this] { d->textModel.setColors(d->theme.palette(), d->theme.foreground(), d->theme.background()); };
    connect(&d->theme, &Theme::changed, this, colors);
    colors();
    d->doubleClicks.onDoubleClick = [this] { openCurrent(); };
}

Controller::~Controller() = default;

void Controller::registerImageProviders(QQmlApplicationEngine *engine)
{
    engine->addImageProvider(QStringLiteral("ql"), d->ownedStore.release());   // the engine owns it from now on
    engine->addImageProvider(QStringLiteral("pdf"), new PdfPageProvider(&d->pdfModel));
}

void Controller::attachWindow(QQuickWindow *window)
{
    d->window = window;
    window->setFlags(Qt::Window | Qt::FramelessWindowHint);
    window->setProperty("_q_showWithoutActivating", true);
    window->installEventFilter(&d->keys);
    window->installEventFilter(&d->doubleClicks);
    connect(&d->keys, &KeyRouter::action, this, [this](KeyAction action) { d->onKeyAction(action); });
    connect(&d->keys, &KeyRouter::closeRequested, d->service, &PreviewerService::requestClose);
    connect(window, &QWindow::windowStateChanged, this, [this](Qt::WindowState state) { d->onWindowState(state); });
    connect(window, &QWindow::widthChanged, this, [this] { d->onResized(); });
    connect(window, &QWindow::heightChanged, this, [this] { d->onResized(); });
}

QQuickWindow *Controller::window() const { return d->window; }

void Controller::warmUp()
{
    QImageReader::supportedImageFormats();
    TextModel::warmUp();
    {
        QMediaPlayer player;   // loads the FFmpeg backend
        QVideoSink sink;
        player.setVideoSink(&sink);
    }
    OfficeConverter::killOrphans(OfficeConverter::profileDir());
}

QString Controller::kind() const { return kindName(d->kind); }
QRectF Controller::contentRect() const { return d->contentRect; }
double Controller::scale() const { return d->scale; }
QString Controller::imageSource() const { return d->imageSource; }
QString Controller::placeholderSource() const { return d->placeholderSource; }
bool Controller::busy() const { return d->busy; }
QString Controller::cardName() const { return d->cardName; }
QString Controller::cardType() const { return d->cardType; }
QString Controller::cardDetails() const { return d->cardDetails; }
QString Controller::cardReason() const { return d->cardReason; }
QString Controller::cardIcon() const { return d->cardIcon; }
QRectF Controller::cardRect() const { return d->cardRect; }
QString Controller::audioTitle() const { return d->audioTitle; }
QString Controller::audioSubtitle() const { return d->audioSubtitle; }
QString Controller::coverSource() const { return d->coverSource; }
QObject *Controller::pdfObject() const { return &d->pdfModel; }
QObject *Controller::textObject() const { return &d->textModel; }
QObject *Controller::mediaObject() const { return &d->media; }
QObject *Controller::themeObject() const { return &d->theme; }
bool Controller::fullScreen() const { return d->fullScreen; }
QString Controller::hint() const { return d->hint; }
double Controller::zoom() const { return d->zoom; }

void Controller::setVideoSink(QObject *videoSink) { d->media.setOutputSink(qobject_cast<QVideoSink *>(videoSink)); }

void Controller::openCurrent()
{
    if (!d->session->visible() || d->shownFile.path.isEmpty())
        return;
    switch (d->launcher.open(d->shownFile.path, d->shownFile.mimeName)) {
    case Launcher::Result::Launched:
        d->service->requestClose();
        break;
    case Launcher::Result::NoApp:
        d->showHint(Str::NoApp);
        break;
    case Launcher::Result::Failed:
        break;
    }
}

void Controller::togglePlay() { d->media.togglePlay(); }

void Controller::setLauncherProgram(const QString &program) { d->launcher.setProgram(program); }
quint64 Controller::shownGeneration() const { return d->shownGeneration; }
Layout Controller::currentLayout() const { return d->layout; }
bool Controller::windowShown() const { return d->window && d->window->isVisible(); }

} // namespace ql
