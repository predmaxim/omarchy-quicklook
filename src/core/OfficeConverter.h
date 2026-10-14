#pragma once
// LibreOffice -> PDF with a private warm instance (spec: "Офис", конвертер).
#include <QObject>
#include <QProcessEnvironment>
#include <QString>
#include <memory>

namespace ql {

class OfficeCache;

class OfficeConverter : public QObject {
    Q_OBJECT
public:
    OfficeConverter(OfficeCache *cache, const QProcessEnvironment &launchEnv, QObject *parent = nullptr);
    ~OfficeConverter() override;   // terminates the warm instance

    void convert(quint64 generation, const QString &path);
        // one job at a time; a waiting job is replaced by a newer one; the running job finishes into the cache
    void setProgram(const QString &soffice);          // tests (default "soffice")
    void setTimeouts(int convertMs, int idleMs);      // defaults 30000, 300000

    static QString profileDir();                      // $XDG_CACHE_HOME/omarchy-quicklook/lo-profile
    static QString tmpDir();                          // $XDG_CACHE_HOME/omarchy-quicklook/lo-tmp
    static bool ensureProfileSecurity(const QString &profileDir);
        // <profile>/user/registrymodifications.xcu has MacroSecurityLevel=3 and BlockUntrustedRefererLinks=true
        // under /org.openoffice.Office.Common/Security/Scripting (oor:op="fuse"); writes them if missing
    static int killOrphans(const QString &profileDir);   // kills soffice processes whose argv mentions the profile

Q_SIGNALS:
    void converted(quint64 generation, const QString &pdfPath);   // cached path
    void failed(quint64 generation);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace ql
