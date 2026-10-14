#include "Launcher.h"
#include <QFileInfo>
#include <QProcess>
#include <gio/gio.h>

namespace ql {

Launcher::Launcher(const QProcessEnvironment &launchEnv) : m_env(launchEnv) {}

QString Launcher::defaultDesktopId(const QString &mimeName)
{
    GAppInfo *info = g_app_info_get_default_for_type(mimeName.toUtf8().constData(), FALSE);
    if (!info)
        return {};
    const char *id = g_app_info_get_id(info);
    const QString result = id ? QString::fromUtf8(id) : QString();
    g_object_unref(info);
    return result;
}

Launcher::Result Launcher::open(const QString &path, const QString &mimeName)
{
    const QString id = m_idOverride.isEmpty() ? defaultDesktopId(mimeName) : m_idOverride;
    if (id.isEmpty())
        return Result::NoApp;
    const bool isDir = mimeName == QLatin1String("inode/directory");   // no stat on the GUI thread
    QProcess p;
    p.setProgram(m_program);
    p.setArguments({QStringLiteral("--"), id, path});
    p.setWorkingDirectory(isDir ? path : QFileInfo(path).absolutePath());
    p.setProcessEnvironment(m_env);
    qint64 pid = 0;
    return p.startDetached(&pid) ? Result::Launched : Result::Failed;
}

} // namespace ql
