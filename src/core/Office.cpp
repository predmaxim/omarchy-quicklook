#include "Office.h"
#include <KZip>
#include <QFile>
#include <QHash>
#include <QtEndian>
#include <QXmlStreamReader>
#include <cmath>

namespace ql::office {

namespace {
constexpr qint64 kMaxPart = 4 * 1024 * 1024;   // zip-bomb guard: a larger part is treated as absent

QByteArray readEntry(KZip &zip, const QString &name)
{
    const auto *e = zip.directory()->entry(name);
    if (!e || !e->isFile())
        return {};
    std::unique_ptr<QIODevice> dev(static_cast<const KArchiveFile *>(e)->createDevice());
    return dev ? dev->read(kMaxPart) : QByteArray();
}

// Namespace processing is off: documents are matched by qualified name (w:sectPr, fo:page-width ...).
struct Xml : QXmlStreamReader {
    explicit Xml(const QByteArray &xml) : QXmlStreamReader(xml) { setNamespaceProcessing(false); }
};

double toPt(const QString &v)   // "21cm" -> pt; NaN on unknown unit
{
    static const QHash<QString, double> unit{{"cm", 72 / 2.54}, {"mm", 72 / 25.4}, {"in", 72}, {"pt", 1}, {"pc", 12}};
    for (auto it = unit.cbegin(); it != unit.cend(); ++it) {
        if (v.endsWith(it.key())) {
            bool ok = false;
            const double n = v.chopped(2).toDouble(&ok);
            return ok ? n * it.value() : NAN;
        }
    }
    return NAN;
}

QSizeF sizeFrom(const QXmlStreamAttributes &a)
{
    const double w = toPt(a.value("fo:page-width").toString()), h = toPt(a.value("fo:page-height").toString());
    return (w > 0 && h > 0) ? QSizeF(w, h) : QSizeF();
}

bool nonEmptySheet(const QByteArray &xml)
{
    return xml.contains("<row ") || xml.contains("<row>") || xml.contains("<row/");
}
}

bool odfEncrypted(const QByteArray &manifestXml)
{
    return manifestXml.contains("manifest:encryption-data") || manifestXml.contains("encrypted-package");
}

QSizeF excelPaper(int paperSize, bool landscape)
{
    QSizeF s;
    switch (paperSize) {
    case 5: s = {612, 1008}; break;
    case 8: s = {841.89, 1190.55}; break;
    case 9: s = {595.28, 841.89}; break;
    case 11: s = {419.53, 595.28}; break;
    default: s = {612, 792}; break;   // 1 and unknown
    }
    return landscape ? s.transposed() : s;
}

QSizeF predictDocx(const QByteArray &documentXml)
{
    Xml r(documentXml);
    bool inSect = false;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement() && r.qualifiedName() == u"w:sectPr") {
            inSect = true;
        } else if (r.isEndElement() && r.qualifiedName() == u"w:sectPr") {
            return {};   // the first sectPr has no pgSz
        } else if (inSect && r.isStartElement() && r.qualifiedName() == u"w:pgSz") {
            const auto a = r.attributes();
            double w = a.value("w:w").toDouble() / 20, h = a.value("w:h").toDouble() / 20;
            if (w <= 0 || h <= 0)
                return {};
            if (a.value("w:orient") == u"landscape" && w < h)
                std::swap(w, h);
            return {w, h};
        }
    }
    return {};
}

QSizeF predictXlsx(const QByteArray &workbookXml, const QByteArray &workbookRelsXml,
                   const std::function<QByteArray(const QString &entry)> &readEntry)
{
    QHash<QString, QString> targets;
    for (Xml r(workbookRelsXml); !r.atEnd();) {
        r.readNext();
        if (r.isStartElement() && r.name() == u"Relationship")
            targets.insert(r.attributes().value("Id").toString(), r.attributes().value("Target").toString());
    }
    for (Xml r(workbookXml); !r.atEnd();) {
        r.readNext();
        if (!r.isStartElement() || r.qualifiedName() != u"sheet")
            continue;
        const auto a = r.attributes();
        if (a.value("state") == u"hidden" || a.value("state") == u"veryHidden")
            continue;
        QString t = targets.value(a.value("r:id").toString());
        if (t.isEmpty())
            continue;
        t = t.startsWith('/') ? t.mid(1) : "xl/" + t;
        const QByteArray sheet = readEntry(t);
        if (!nonEmptySheet(sheet))
            continue;
        for (Xml s(sheet); !s.atEnd();) {
            s.readNext();
            if (s.isStartElement() && s.qualifiedName() == u"pageSetup") {
                const auto p = s.attributes();
                return excelPaper(p.hasAttribute("paperSize") ? p.value("paperSize").toInt() : 1,
                                  p.value("orientation") == u"landscape");
            }
        }
        return excelPaper(1, false);
    }
    return {};
}

QSizeF predictOdf(const QByteArray &stylesXml)
{
    // The first master page's layout is the document's first page: in ODP the first page-layout
    // is the handout (A4 portrait), not the slide. Without a master page: the first layout.
    QHash<QString, QSizeF> layouts;
    QSizeF first;
    QString layoutName, master;
    for (Xml r(stylesXml); !r.atEnd();) {
        r.readNext();
        if (!r.isStartElement())
            continue;
        if (r.qualifiedName() == u"style:page-layout") {
            layoutName = r.attributes().value("style:name").toString();
        } else if (r.qualifiedName() == u"style:page-layout-properties") {
            const QSizeF s = sizeFrom(r.attributes());
            if (!s.isEmpty()) {
                layouts.insert(layoutName, s);
                if (first.isEmpty())
                    first = s;
            }
        } else if (r.qualifiedName() == u"style:master-page" && master.isEmpty()) {
            master = r.attributes().value("style:page-layout-name").toString();
        }
    }
    return layouts.value(master, first);
}

QSizeF predictPptx(const QByteArray &presentationXml)
{
    for (Xml r(presentationXml); !r.atEnd();) {
        r.readNext();
        if (r.isStartElement() && r.qualifiedName() == u"p:sldSz") {
            const double w = r.attributes().value("cx").toDouble() / 12700, h = r.attributes().value("cy").toDouble() / 12700;
            return (w > 0 && h > 0) ? QSizeF(w, h) : QSizeF();
        }
    }
    return {};
}

bool cfbHasEncryptedPackage(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray h = f.read(512);
    if (h.size() < 512)
        return false;
    auto u16 = [](const QByteArray &b, int o) { return qFromLittleEndian<quint16>(b.constData() + o); };
    auto u32 = [](const QByteArray &b, int o) { return qFromLittleEndian<quint32>(b.constData() + o); };
    const int shift = u16(h, 0x1E);
    if (shift != 9 && shift != 12)
        return false;
    const qint64 ssz = 1LL << shift, perFat = ssz / 4;
    // ponytail: FAT sector ids come only from the header DIFAT (109 entries); a directory chain past
    // ~7 MB with 512-byte sectors is not followed. Add DIFAT sectors if such files matter.
    auto nextSector = [&](quint32 s) -> quint32 {
        const quint32 fat = s / perFat < 109 ? u32(h, 0x4C + int(s / perFat) * 4) : 0xFFFFFFFE;
        if (fat >= 0xFFFFFFFA || !f.seek((qint64(fat) + 1) * ssz + (s % perFat) * 4))
            return 0xFFFFFFFE;
        const QByteArray b = f.read(4);
        return b.size() == 4 ? u32(b, 0) : 0xFFFFFFFE;
    };
    static const QByteArray want = QByteArray(reinterpret_cast<const char *>(u"EncryptedPackage"), 32);   // UTF-16LE bytes
    quint32 s = u32(h, 0x30);
    for (qint64 guard = f.size() / ssz + 1; s < 0xFFFFFFFA && guard > 0; --guard, s = nextSector(s)) {
        if (!f.seek((qint64(s) + 1) * ssz))
            return false;
        const QByteArray sec = f.read(ssz);
        for (int o = 0; o + 128 <= sec.size(); o += 128) {
            const int len = u16(sec, o + 0x40);
            if (len == want.size() + 2 && sec.mid(o, want.size()) == want)
                return true;
        }
    }
    return false;
}

Header probe(const QString &path, const QString &fileName, const QByteArray &head)
{
    Header h;
    const QString suffix = fileName.section('.', -1).toLower();
    const bool slides = suffix == u"ppt" || suffix == u"pptx";
    h.firstPagePt = slides ? kSlide43 : kA4;

    if (head.startsWith("PK\x03\x04")) {
        KZip zip(path);
        if (!zip.open(QIODevice::ReadOnly))
            return h;
        const bool odf = head.mid(30, 8) == "mimetype";
        QSizeF s;
        if (odf) {
            h.family = Family::Odf;
            h.office = true;
            h.encrypted = odfEncrypted(readEntry(zip, "META-INF/manifest.xml"));
            if (!h.encrypted)
                s = predictOdf(readEntry(zip, "styles.xml"));
        } else if (zip.directory()->entry("word/document.xml")) {
            s = predictDocx(readEntry(zip, "word/document.xml"));
            h.family = Family::Ooxml;
        } else if (zip.directory()->entry("xl/workbook.xml")) {
            s = predictXlsx(readEntry(zip, "xl/workbook.xml"), readEntry(zip, "xl/_rels/workbook.xml.rels"),
                            [&zip](const QString &e) { return readEntry(zip, e); });
            h.family = Family::Ooxml;
        } else if (zip.directory()->entry("ppt/presentation.xml")) {
            s = predictPptx(readEntry(zip, "ppt/presentation.xml"));
            h.family = Family::Ooxml;
            h.firstPagePt = kSlide43;
        } else {
            return h;   // a plain zip
        }
        h.office = true;
        if (!s.isEmpty()) {
            h.firstPagePt = s;
            h.predicted = true;
        }
    } else if (head.startsWith("\xD0\xCF\x11\xE0")) {
        h.office = true;
        h.family = Family::Cfb;
        h.encrypted = cfbHasEncryptedPackage(path);
    } else if (head.startsWith("{\\rtf")) {
        h.office = true;
        h.family = Family::Rtf;
    }
    return h;
}

} // namespace ql::office
