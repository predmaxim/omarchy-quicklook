#pragma once
// Office documents: signatures, encryption, first page size (spec: "Тип файла" п. 4, "Офис").
#include <QByteArray>
#include <QSizeF>
#include <QString>
#include <functional>

namespace ql::office {

inline const QSizeF kA4{595.28, 841.89};
inline const QSizeF kLetter{612.0, 792.0};
inline const QSizeF kSlide43{720.0, 540.0};

enum class Family { None, Ooxml, Odf, Cfb, Rtf };

struct Header {
    bool office = false;      // a known signature is present
    bool encrypted = false;   // OOXML in CFB with EncryptedPackage, or ODF with manifest:encryption-data
    Family family = Family::None;
    QSizeF firstPagePt;       // predicted, or the fallback (A4 portrait; 4:3 for ppt/pptx)
    bool predicted = false;
};

Header probe(const QString &path, const QString &fileName, const QByteArray &head);
bool cfbHasEncryptedPackage(const QString &path);   // directory sector from header (0x30, sector size 0x1E)
bool odfEncrypted(const QByteArray &manifestXml);
QSizeF predictDocx(const QByteArray &documentXml);  // first w:sectPr in document order: w:pgSz w/h twips -> pt (orient)
QSizeF predictXlsx(const QByteArray &workbookXml, const QByteArray &workbookRelsXml,
                   const std::function<QByteArray(const QString &entry)> &readEntry);
        // first visible non-empty sheet: pageSetup paperSize (missing = Letter) + orientation
QSizeF predictOdf(const QByteArray &stylesXml);     // first style:page-layout-properties fo:page-width/height
QSizeF predictPptx(const QByteArray &presentationXml);  // p:sldSz cx/cy EMU -> pt
QSizeF excelPaper(int paperSize, bool landscape);   // 1 Letter, 9 A4, ... (unknown -> Letter)

} // namespace ql::office
