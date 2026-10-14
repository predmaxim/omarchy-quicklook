#pragma once
// Shared value types for omarchy-quicklook (spec: docs/superpowers/specs/2026-10-14-quicklook-design.md).
#include <QDateTime>
#include <QMetaType>
#include <QRectF>
#include <QSize>
#include <QString>

namespace ql {

enum class Kind { None, Image, Svg, Pdf, Office, Text, Video, Audio, FileCard, FolderCard };
enum class CardReason { None, CannotOpen, PasswordProtected, ConversionFailed, TooLarge };
enum class Str { CannotOpen, PasswordProtected, ConversionFailed, TooLarge, NoApp, Items, EmptyFile };

struct Monitor {
    QString name;
    QSize work;          // usable logical px: mode size / scale (sides swapped for odd transform) minus reserved
    double scale = 1.0;  // Hyprland scale, e.g. 1.25
};

struct Layout {
    QSize window;        // integer logical px
    QRectF content;      // where the content is drawn inside the window, logical px
};

QString kindName(Kind kind);  // "image" "svg" "pdf" "office" "text" "video" "audio" "file" "folder"; "" for None
bool uiRussian();             // first non-empty of LC_ALL, LC_MESSAGES, LANG starts with "ru"
QString uiText(Str id, bool russian);
QString uiText(Str id);       // uiText(id, uiRussian())
QString reasonText(CardReason reason, bool russian);  // "" for None
QString itemsText(int count, bool russian);           // "Элементов: 1 234" / "Items: 1,234"; count > 10000 -> "10 000+"
QString truncatedHint(qint64 shownBytes, qint64 totalBytes, bool russian);
                              // "Показаны первые 2,0 МБ из 16,7 МБ — Enter откроет" / "Showing first 2.0 MB of 16.7 MB — Enter opens it"
QString formatSize(qint64 bytes, bool russian);       // 1024-based, one decimal: "512 Б"/"512 B", "1,2 КБ"/"1.2 KB", "16,7 МБ"/"16.7 MB", "ГБ"/"GB"
QString formatDate(const QDateTime &when, bool russian); // "14.10.2026 22:05" / "2026-10-14 22:05" (local time)

} // namespace ql

Q_DECLARE_METATYPE(ql::Kind)
Q_DECLARE_METATYPE(ql::CardReason)
