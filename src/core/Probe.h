#pragma once
// Probe pipeline run on pools::probe() (spec: "Показ", пробы и бюджеты).
#include "AudioTags.h"
#include "FileInfo.h"
#include "Image.h"
#include "Office.h"
#include "Pdf.h"
#include "TextDecode.h"
#include "TextRows.h"
#include "Types.h"
#include <QImage>
#include <QMetaType>

namespace ql {

struct ProbeRequest {
    quint64 generation = 0;
    QString uri;
    QString preferredMonitor;   // the window's monitor when switching while shown; "" on first show
    Monitor fallbackMonitor;    // hyprland::screenMonitor(), taken on the GUI thread
};

struct ProbeResult {
    quint64 generation = 0;
    FileInfo file;
    Kind kind = Kind::None;
    CardReason reason = CardReason::None;
    Monitor monitor;
    Layout layout;               // set by detailStage (and by fallbackFor)
    bool detailed = false;       // detailStage finished
    image::Header image;
    pdf::Header pdf;
    office::Header office;
    QString officeCachedPdf;     // cache hit: shown as Pdf with the exact size
    QSizeF svgSize;
    text::Decoded text;
    text::Measure textMeasure;
    bool highlight = false;      // false when a line > 2000 chars is in the first 64 KiB
    AudioTags audio;
    QImage placeholder;          // Files thumbnail (x-large/large/normal, mtime-checked) or null
};

ProbeResult classifyStage(const ProbeRequest &request);  // resolveFile + classify + currentMonitor
void detailStage(ProbeResult &result);
        // Image: header (+ tooLarge/raw rules) + layout + thumbnail; Svg: size (budget 80 ms) + layout;
        // Pdf: header (budget 80 ms) + layout (+ thumbnail); Office: probe (budget 80 ms), cache lookup, layout;
        // Text: readFile + measure + layout; Audio: tags + card layout; Video: thumbnail only (size comes from media);
        // cards: card layout. Errors turn kind into FileCard with a reason.
Layout fallbackFor(const ProbeResult &result);
        // Svg 4:3, Pdf/Office A4 (ppt/pptx 4:3), Video 16:9 — fit into box
ProbeResult runProbe(const ProbeRequest &request);       // classifyStage + detailStage (tests)

} // namespace ql

Q_DECLARE_METATYPE(ql::ProbeResult)
