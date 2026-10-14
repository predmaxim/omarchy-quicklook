#include "StateStore.h"
#include <QSettings>
#include <QStandardPaths>

namespace ql {

StateStore::StateStore(const QString &file) : m_file(file) {}

QString StateStore::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + QStringLiteral("/omarchy-quicklook/state.ini");
}

bool StateStore::muted() const
{
    return QSettings(m_file, QSettings::IniFormat).value(QStringLiteral("muted"), false).toBool();
}

void StateStore::setMuted(bool muted)
{
    QSettings s(m_file, QSettings::IniFormat);
    s.setValue(QStringLiteral("muted"), muted);
    s.sync();
}

} // namespace ql
