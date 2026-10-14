#pragma once
// Text encoding pipeline (spec: "Текст"): BOM -> strict UTF-8 -> uchardet (<=16 KiB) -> QStringDecoder -> windows-1252.
#include <QByteArray>
#include <QString>

namespace ql::text {

inline constexpr qint64 kMaxBytes = 2 * 1024 * 1024;
inline constexpr qint64 kDetectBytes = 16 * 1024;

struct Decoded {
    QString text;
    QByteArray encoding;      // name actually used
    bool truncated = false;   // file larger than kMaxBytes
    qint64 shownBytes = 0;    // bytes decoded (after the cut at the last '\n')
    qint64 totalBytes = 0;
};

QByteArray mapCharsetName(const QByteArray &uchardetName); // "UHC" -> "windows-949"; names QStringDecoder rejects -> "windows-1252"
QByteArray detectEncoding(const QByteArray &bytes);        // "UTF-8"/"UTF-16LE"/... from BOM, "UTF-8" if strictly valid,
                                                           // else mapped uchardet name, else "windows-1252"
Decoded decode(QByteArray bytes, qint64 totalBytes);       // bytes: first <= kMaxBytes; cut at last '\n' when totalBytes > kMaxBytes
Decoded readFile(const QString &path);                     // reads up to kMaxBytes and decodes

} // namespace ql::text
