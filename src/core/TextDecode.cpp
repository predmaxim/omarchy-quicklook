#include "TextDecode.h"

#include <QFile>
#include <QStringConverter>
#include <uchardet/uchardet.h>

namespace ql::text {

namespace {

bool isAscii(const QByteArray &b, qsizetype from, qsizetype to)
{
    for (qsizetype i = from; i < to; ++i)
        if (static_cast<unsigned char>(b[i]) >= 0x80)
            return false;
    return true;
}

// Detection window: the first kDetectBytes; if they are all ASCII, the kDetectBytes starting
// at the first non-ASCII byte (else a legacy-encoded file with a long ASCII head passes as UTF-8).
QByteArray detectionWindow(const QByteArray &bytes, bool &reachesEnd)
{
    qsizetype start = 0;
    const qsizetype head = qMin<qsizetype>(bytes.size(), kDetectBytes);
    if (isAscii(bytes, 0, head)) {
        start = head;
        while (start < bytes.size() && static_cast<unsigned char>(bytes[start]) < 0x80)
            ++start;
        if (start == bytes.size())
            start = 0;
    }
    const qsizetype len = qMin<qsizetype>(bytes.size() - start, kDetectBytes);
    reachesEnd = start + len == bytes.size();
    return bytes.mid(start, len);
}

bool strictUtf8(const QByteArray &window, bool reachesEnd)
{
    // Stateful when the window cuts the data: a character split by the cut is not an error.
    QStringDecoder dec(QStringConverter::Utf8, reachesEnd ? QStringConverter::Flag::Stateless : QStringConverter::Flag::Default);
    const QString s = dec.decode(window); // lazy: hasError() is valid only after conversion
    Q_UNUSED(s)
    return !dec.hasError();
}

} // namespace

QByteArray mapCharsetName(const QByteArray &name)
{
    if (name == "UHC")
        return "windows-949";
    return QStringDecoder(name.constData()).isValid() ? name : QByteArray("windows-1252");
}

QByteArray detectEncoding(const QByteArray &bytes)
{
    if (const auto bom = QStringConverter::encodingForData(bytes)) {
        if (const char *n = QStringConverter::nameForEncoding(*bom))
            return n;
    }
    bool reachesEnd = false;
    const QByteArray window = detectionWindow(bytes, reachesEnd);
    if (strictUtf8(window, reachesEnd))
        return "UTF-8";

    QByteArray result;
    if (uchardet_t ud = uchardet_new()) {
        uchardet_handle_data(ud, window.constData(), static_cast<size_t>(window.size()));
        uchardet_data_end(ud);
        result = uchardet_get_charset(ud);
        uchardet_delete(ud);
    }
    if (result.isEmpty() || result == "ASCII")
        return "windows-1252";
    return mapCharsetName(result);
}

Decoded decode(QByteArray bytes, qint64 totalBytes)
{
    Decoded d;
    d.totalBytes = totalBytes;
    d.truncated = totalBytes > kMaxBytes;
    if (bytes.size() > kMaxBytes)
        bytes.truncate(kMaxBytes);
    if (d.truncated) {
        // ponytail: byte search; inexact for UTF-16/32 (0x0A may be half a unit), fine for rare >2 MiB files
        if (const qsizetype nl = bytes.lastIndexOf('\n'); nl >= 0)
            bytes.truncate(nl + 1);
    }
    d.encoding = detectEncoding(bytes);
    d.shownBytes = bytes.size();
    QStringDecoder dec(d.encoding.constData());
    d.text = dec.decode(bytes);
    if (d.text.startsWith(QChar(0xFEFF)))
        d.text.remove(0, 1);
    return d;
}

Decoded readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const qint64 total = f.size();
    return decode(f.read(kMaxBytes), total);
}

} // namespace ql::text
