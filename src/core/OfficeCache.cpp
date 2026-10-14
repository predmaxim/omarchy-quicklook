#include "OfficeCache.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <algorithm>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ql {

QString OfficeCache::defaultDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/omarchy-quicklook/office";
}

OfficeCache::OfficeCache(const QString &dir) : m_dir(dir)
{
    QDir().mkpath(m_dir);
    ::chmod(QFile::encodeName(m_dir).constData(), 0700);
}

QString OfficeCache::keyFor(const QString &path) const
{
    struct stat st {};
    if (::stat(QFile::encodeName(path).constData(), &st) != 0)
        return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(QByteArray::number(kVersion) + '\0' + QFileInfo(path).canonicalFilePath().toUtf8() + '\0'
              + QByteArray::number(qint64(st.st_mtim.tv_sec) * 1000000000 + st.st_mtim.tv_nsec) + '\0'
              + QByteArray::number(qint64(st.st_size)));
    return QString::fromLatin1(h.result().toHex());
}

QString OfficeCache::lookup(const QString &path) const
{
    const QString key = keyFor(path);
    if (key.isEmpty())
        return {};
    const QString pdf = m_dir + "/" + key + ".pdf";
    if (QFileInfo(pdf).size() <= 0)
        return {};
    ::utimensat(AT_FDCWD, QFile::encodeName(pdf).constData(), nullptr, 0);   // null times = now
    return pdf;
}

QString OfficeCache::store(const QString &path, const QString &jobPdf)
{
    const QString key = keyFor(path);
    if (key.isEmpty())
        return {};
    const QString dst = m_dir + "/" + key + ".pdf";
    const QByteArray d = QFile::encodeName(dst);
    if (::rename(QFile::encodeName(jobPdf).constData(), d.constData()) != 0) {
        // EXDEV: copy next to the target and rename, so the final file appears atomically
        const QString part = dst + ".part";
        QFile::remove(part);
        if (!QFile::copy(jobPdf, part) || ::rename(QFile::encodeName(part).constData(), d.constData()) != 0) {
            QFile::remove(part);
            return {};
        }
        QFile::remove(jobPdf);
    }
    ::chmod(d.constData(), 0600);
    evict();
    return dst;
}

void OfficeCache::evict(qint64 maxBytes)
{
    QFileInfoList l = QDir(m_dir).entryInfoList({"*.pdf"}, QDir::Files);
    std::sort(l.begin(), l.end(), [](const QFileInfo &a, const QFileInfo &b) { return a.lastModified() < b.lastModified(); });
    qint64 total = 0;
    for (const auto &f : l)
        total += f.size();
    for (const auto &f : l) {
        if (total <= maxBytes)
            break;
        if (QFile::remove(f.filePath()))
            total -= f.size();
    }
}

} // namespace ql
