#pragma once
// Enter: open in the default app through uwsm-app (spec: "Клавиши", "Процесс").
#include <QProcessEnvironment>
#include <QString>

namespace ql {

class Launcher {
public:
    enum class Result { Launched, NoApp, Failed };

    explicit Launcher(const QProcessEnvironment &launchEnv);   // snapshot taken in main() before qputenv

    static QString defaultDesktopId(const QString &mimeName);  // g_app_info_get_default_for_type; "" if none
    Result open(const QString &path, const QString &mimeName); // QProcess object: program, args {"--", id, path},
                                                                // cwd = file dir (dir itself for folders), env = launchEnv,
                                                                // startDetached(&pid)
    void setProgram(const QString &program) { m_program = program; }           // tests
    void setDesktopIdOverride(const QString &id) { m_idOverride = id; }        // tests
    QProcessEnvironment environment() const { return m_env; }

private:
    QProcessEnvironment m_env;
    QString m_program = QStringLiteral("uwsm-app");
    QString m_idOverride;
};

} // namespace ql
