#include "OfficeConverter.h"
#include "OfficeCache.h"
#include "Pools.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent>
#include <csignal>
#include <dirent.h>
#include <optional>
#include <sys/stat.h>
#include <unistd.h>

namespace ql {

namespace {

constexpr int kReadyPollMs = 50;
constexpr int kReadyMaxMs = 15000;
constexpr int kTermGraceMs = 3000;

const QByteArray kScriptingPath = "/org.openoffice.Office.Common/Security/Scripting";

QByteArray securityItem(const char *name, const char *value)
{
    return "<item oor:path=\"" + kScriptingPath + "\"><prop oor:name=\"" + name + "\" oor:op=\"fuse\"><value>" + value
        + "</value></prop></item>\n";
}

QString userInstallationArg(const QString &profile)
{
    return "-env:UserInstallation=" + QUrl::fromLocalFile(profile).toString(QUrl::FullyEncoded);   // spaces, '#' 
}

QString baseDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/omarchy-quicklook";
}

// (name, inode) of every /tmp/OSL_PIPE_<uid>_* entry: LibreOffice's IPC pipes
QSet<QPair<QByteArray, ino_t>> oslPipes()
{
    QSet<QPair<QByteArray, ino_t>> out;
    const QByteArray prefix = "OSL_PIPE_" + QByteArray::number(::getuid()) + '_';
    if (DIR *dir = ::opendir("/tmp")) {
        while (dirent *e = ::readdir(dir))
            if (qstrncmp(e->d_name, prefix.constData(), prefix.size()) == 0)
                out.insert({QByteArray(e->d_name), e->d_ino});
        ::closedir(dir);
    }
    return out;
}

void killGroup(QProcess *p, int sig)
{
    const qint64 pid = p ? p->processId() : 0;
    if (pid > 0)   // never kill(-0): that is our own process group
        ::kill(-pid_t(pid), sig);
}

void killAndDelete(QProcess *&p)   // the whole group; reaped before delete (no "destroyed while running")
{
    if (!p)
        return;
    p->disconnect();
    killGroup(p, SIGKILL);
    p->waitForFinished(1000);
    delete p;
    p = nullptr;
}

void clearTmp()
{
    const QFileInfoList entries =
        QDir(OfficeConverter::tmpDir()).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const QFileInfo &f : entries) {
        if (f.isDir() && !f.isSymLink())
            QDir(f.filePath()).removeRecursively();
        else
            QFile::remove(f.filePath());
    }
}

} // namespace

struct OfficeConverter::Private {
    struct Job {
        quint64 gen = 0;
        QString path;
    };

    Private(OfficeConverter *q, const OfficeCache &cache, const QProcessEnvironment &env) : q(q), cache(cache), env(env) {}

    OfficeConverter *q;
    OfficeCache cache;   // a handle on a folder, safe to copy into pool tasks
    QProcessEnvironment env;
    QString program = QStringLiteral("soffice");
    int convertMs = 30000;
    int idleMs = 300000;
    bool orphansKilled = false;

    QProcess *instance = nullptr;
    QProcess *dying = nullptr;   // idle-stopped instance during its SIGTERM grace
    std::optional<Job> current;
    std::optional<Job> pending;
    int jobSerial = 0;   // per started job: deferred calls of an ended job do nothing (current->gen may be retargeted)
    QProcess *proc = nullptr;
    std::shared_ptr<QTemporaryDir> jobDir;

    QTimer convertTimer, idleTimer, readyTimer, graceTimer;
    QElapsedTimer readyClock;
    QSet<QPair<QByteArray, ino_t>> pipesBefore;

    QProcess *spawn(const QStringList &args)
    {
        auto *p = new QProcess(q);
        p->setProcessEnvironment(env);
        p->setChildProcessModifier([] { ::setpgid(0, 0); });   // own group: kill(-pid) reaches soffice.bin too
        p->setStandardOutputFile(QProcess::nullDevice());   // unread output would pile up in QProcess for 5 min
        p->setStandardErrorFile(QProcess::nullDevice());
        p->start(program, args);
        return p;
    }

    void startJob(const Job &job)
    {
        current = job;
        const int serial = ++jobSerial;
        idleTimer.stop();
        convertTimer.start(convertMs);   // covers the instance start too
        if (instance && instance->state() != QProcess::NotRunning) {
            QTimer::singleShot(0, q, [this, serial] {   // signals never fire inside convert()
                if (current && jobSerial == serial && !proc)
                    runConversion();
            });
            return;
        }
        reapDying();
        killAndDelete(instance);   // exited on its own
        if (!ensureProfileSecurity(profileDir())) {
            QTimer::singleShot(0, q, [this, serial] {
                if (current && jobSerial == serial && !proc)
                    endJob(false);
            });
            return;
        }
        if (!orphansKilled) {
            killOrphans(profileDir());
            orphansKilled = true;
        }
        QDir().mkpath(tmpDir());
        ::chmod(QFile::encodeName(tmpDir()).constData(), 0700);
        const QString anchor = baseDir() + "/anchor.odt";
        QFile::remove(anchor);
        QFile::copy(QStringLiteral(":/resources/anchor.odt"), anchor);
        pipesBefore = oslPipes();
        instance = spawn({QStringLiteral("--headless"), QStringLiteral("--invisible"), QStringLiteral("--norestore"),
                          QStringLiteral("--nologo"), userInstallationArg(profileDir()), anchor});
        readyClock.start();
        readyTimer.start(kReadyPollMs);
    }

    void pollReady()
    {
        // ready = the instance created its IPC pipe: a new name, or a new inode under the name a killed instance left
        if (instance->state() != QProcess::NotRunning && readyClock.elapsed() < kReadyMaxMs && oslPipes() == pipesBefore)
            return;
        readyTimer.stop();
        runConversion();   // no pipe: the CLI then converts standalone
    }

    void runConversion()
    {
        jobDir = std::make_shared<QTemporaryDir>(tmpDir() + "/job-XXXXXX");
        proc = spawn({QStringLiteral("--headless"), QStringLiteral("--norestore"), userInstallationArg(profileDir()),
                      QStringLiteral("--convert-to"), QStringLiteral("pdf"), QStringLiteral("--outdir"), jobDir->path(),
                      current->path});
        QObject::connect(proc, &QProcess::finished, q, [this] { onConverted(); });
        QObject::connect(   // queued: start() may report the failure synchronously
            proc, &QProcess::errorOccurred, proc,
            [this, p = proc](QProcess::ProcessError e) {
                if (p == proc && e == QProcess::FailedToStart)
                    endJob(false);
            },
            Qt::QueuedConnection);
    }

    void onConverted()
    {
        const Job job = *current;
        // success = any non-empty PDF in the private job dir (LibreOffice derives its name from the source, with quirks)
        QtConcurrent::run(pools::probe(), [cache = cache, path = job.path, dir = jobDir]() mutable {
            const QFileInfoList pdfs = QDir(dir->path()).entryInfoList({QStringLiteral("*.pdf")}, QDir::Files);
            for (const QFileInfo &f : pdfs)
                if (f.size() > 0)
                    return cache.store(path, f.filePath());
            return QString();
        }).then(q, [q = q, gen = job.gen](const QString &pdf) {
            if (pdf.isEmpty())
                Q_EMIT q->failed(gen);
            else
                Q_EMIT q->converted(gen, pdf);
        });
        endJob(true);
    }

    void endJob(bool reported)   // reported: onConverted() emits the result
    {
        const quint64 gen = current->gen;
        convertTimer.stop();
        readyTimer.stop();
        if (proc) {
            proc->disconnect(q);
            proc->deleteLater();
            proc = nullptr;
        }
        jobDir.reset();
        current.reset();
        idleTimer.start(idleMs);
        if (!reported)
            Q_EMIT q->failed(gen);
        if (pending) {
            const Job next = *pending;
            pending.reset();
            startJob(next);
        }
    }

    void onTimeout()
    {
        killAndDelete(proc);
        killAndDelete(instance);
        endJob(false);
    }

    void onIdle()
    {
        if (!instance)
            return;
        killGroup(instance, SIGTERM);
        dying = instance;
        instance = nullptr;
        graceTimer.start(kTermGraceMs);
    }

    void reapDying()   // end of the grace, or a new job arrived during it
    {
        if (!dying)
            return;
        graceTimer.stop();
        killAndDelete(dying);
        clearTmp();
    }
};

OfficeConverter::OfficeConverter(OfficeCache *cache, const QProcessEnvironment &launchEnv, QObject *parent)
    : QObject(parent), d(new Private(this, *cache, launchEnv))
{
    d->env.insert(QStringLiteral("TMPDIR"), tmpDir());
    d->convertTimer.setSingleShot(true);
    d->idleTimer.setSingleShot(true);
    d->graceTimer.setSingleShot(true);
    connect(&d->convertTimer, &QTimer::timeout, this, [this] { d->onTimeout(); });
    connect(&d->idleTimer, &QTimer::timeout, this, [this] { d->onIdle(); });
    connect(&d->graceTimer, &QTimer::timeout, this, [this] { d->reapDying(); });
    connect(&d->readyTimer, &QTimer::timeout, this, [this] { d->pollReady(); });
}

OfficeConverter::~OfficeConverter()
{
    killAndDelete(d->proc);
    killAndDelete(d->instance);
    d->reapDying();
    clearTmp();
}

void OfficeConverter::convert(quint64 generation, const QString &path)
{
    if (d->current && d->current->path == path) {   // Files came back to the file being converted
        d->current->gen = generation;
        d->pending.reset();
    } else if (d->current) {
        d->pending = Private::Job{generation, path};
    } else {
        d->startJob({generation, path});
    }
}

void OfficeConverter::setProgram(const QString &soffice)
{
    d->program = soffice;
}

void OfficeConverter::setTimeouts(int convertMs, int idleMs)
{
    d->convertMs = convertMs;
    d->idleMs = idleMs;
}

QString OfficeConverter::profileDir()
{
    return baseDir() + "/lo-profile";
}

QString OfficeConverter::tmpDir()
{
    return baseDir() + "/lo-tmp";
}

bool OfficeConverter::ensureProfileSecurity(const QString &profileDir)
{
    const QString path = profileDir + "/user/registrymodifications.xcu";
    QByteArray xcu;
    if (QFile in(path); in.open(QIODevice::ReadOnly))
        xcu = in.readAll();

    // the later item wins: the last one for the key must carry our value, else a corrected item is appended
    auto effective = [&xcu](const char *name, const char *value) {
        const QByteArray prop = QByteArray("oor:name=\"") + name + '"';
        bool ok = false;
        for (const QByteArray &line : xcu.split('\n'))
            if (line.contains("oor:path=\"" + kScriptingPath + '"') && line.contains(prop))
                ok = line.contains(QByteArray("<value>") + value + "</value>");
        return ok;
    };
    QByteArray missing;
    if (!effective("MacroSecurityLevel", "3"))
        missing += securityItem("MacroSecurityLevel", "3");
    if (!effective("BlockUntrustedRefererLinks", "true"))
        missing += securityItem("BlockUntrustedRefererLinks", "true");
    if (missing.isEmpty())
        return true;

    const qsizetype end = xcu.lastIndexOf("</oor:items>");
    if (end < 0) {   // no profile yet (or not a registry document): write a complete one
        xcu = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
              "<oor:items xmlns:oor=\"http://openoffice.org/2001/registry\" xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" "
              "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\n"
            + missing + "</oor:items>\n";
    } else {
        xcu.insert(end, missing);
    }
    if (!QDir().mkpath(profileDir + "/user"))
        return false;
    ::chmod(QFile::encodeName(profileDir).constData(), 0700);
    QSaveFile out(path);
    return out.open(QIODevice::WriteOnly) && out.write(xcu) == xcu.size() && out.commit();
}

int OfficeConverter::killOrphans(const QString &profileDir)
{
    const QByteArray arg = userInstallationArg(profileDir).toUtf8();
    int n = 0;
    const QStringList entries = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        bool isPid = false;
        const pid_t pid = entry.toInt(&isPid);
        if (!isPid || pid == ::getpid())
            continue;
        QFile f("/proc/" + entry + "/cmdline");
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QByteArray cmdline = f.readAll();
        const QList<QByteArray> argv = cmdline.split('\0');
        if (cmdline.contains("soffice") && (argv.contains(arg) || argv.contains(arg + '/')) && ::kill(pid, SIGKILL) == 0)
            ++n;
    }
    return n;
}

} // namespace ql
