#include "Types.h"

#include <QLocale>

namespace ql {

namespace {
QLocale loc(bool russian) { return QLocale(russian ? QLocale::Russian : QLocale::English); }
}

QString kindName(Kind kind)
{
    switch (kind) {
    case Kind::None: break;
    case Kind::Image: return QStringLiteral("image");
    case Kind::Svg: return QStringLiteral("svg");
    case Kind::Pdf: return QStringLiteral("pdf");
    case Kind::Office: return QStringLiteral("office");
    case Kind::Text: return QStringLiteral("text");
    case Kind::Video: return QStringLiteral("video");
    case Kind::Audio: return QStringLiteral("audio");
    case Kind::FileCard: return QStringLiteral("file");
    case Kind::FolderCard: return QStringLiteral("folder");
    }
    return {};
}

bool uiRussian()
{
    for (const char *name : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const QByteArray value = qgetenv(name);
        if (!value.isEmpty())
            return value.startsWith("ru");
    }
    return false;
}

QString uiText(Str id, bool russian)
{
    switch (id) {
    case Str::CannotOpen: return russian ? QString::fromUtf8("Не удалось открыть") : QStringLiteral("Can't open");
    case Str::PasswordProtected: return russian ? QString::fromUtf8("Защищён паролем") : QStringLiteral("Password protected");
    case Str::ConversionFailed: return russian ? QString::fromUtf8("Не удалось сконвертировать") : QStringLiteral("Couldn't convert");
    case Str::TooLarge: return russian ? QString::fromUtf8("Слишком большое") : QStringLiteral("Too large");
    case Str::NoApp: return russian ? QString::fromUtf8("Нет программы для этого типа") : QStringLiteral("No app for this type");
    case Str::Items: return russian ? QString::fromUtf8("Элементов: %1") : QStringLiteral("Items: %1");
    case Str::EmptyFile: return russian ? QString::fromUtf8("Пустой файл") : QStringLiteral("Empty file");
    }
    return {};
}

QString uiText(Str id) { return uiText(id, uiRussian()); }

QString reasonText(CardReason reason, bool russian)
{
    switch (reason) {
    case CardReason::None: return {};
    case CardReason::CannotOpen: return uiText(Str::CannotOpen, russian);
    case CardReason::PasswordProtected: return uiText(Str::PasswordProtected, russian);
    case CardReason::ConversionFailed: return uiText(Str::ConversionFailed, russian);
    case CardReason::TooLarge: return uiText(Str::TooLarge, russian);
    }
    return {};
}

QString itemsText(int count, bool russian)
{
    const QLocale l = loc(russian);
    const QString n = count > 10000 ? l.toString(10000) + QLatin1Char('+') : l.toString(count);
    return uiText(Str::Items, russian).arg(n);
}

QString formatSize(qint64 bytes, bool russian)
{
    if (bytes < 1024)
        return QStringLiteral("%1 %2").arg(bytes).arg(russian ? QString::fromUtf8("Б") : QStringLiteral("B"));
    const QStringList units = russian
        ? QStringList{QString::fromUtf8("КБ"), QString::fromUtf8("МБ"), QString::fromUtf8("ГБ")}
        : QStringList{QStringLiteral("KB"), QStringLiteral("MB"), QStringLiteral("GB")};
    double value = double(bytes) / 1024;
    int unit = 0;
    // 1023.95 would round to "1024.0": show it in the next unit instead
    while (value >= 1023.95 && unit < 2) {
        value /= 1024;
        ++unit;
    }
    QLocale l = loc(russian);
    l.setNumberOptions(QLocale::OmitGroupSeparator);
    return l.toString(value, 'f', 1) + QLatin1Char(' ') + units[unit];
}

QString truncatedHint(qint64 shownBytes, qint64 totalBytes, bool russian)
{
    const QString fmt = russian ? QString::fromUtf8("Показаны первые %1 из %2 — Enter откроет")
                                : QStringLiteral("Showing first %1 of %2 — Enter opens it");
    return fmt.arg(formatSize(shownBytes, russian), formatSize(totalBytes, russian));
}

QString formatDate(const QDateTime &when, bool russian)
{
    return when.toLocalTime().toString(russian ? QStringLiteral("dd.MM.yyyy HH:mm") : QStringLiteral("yyyy-MM-dd HH:mm"));
}

} // namespace ql
