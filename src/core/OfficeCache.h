#pragma once
// Converted office PDFs (spec: "Офис", кэш).
#include <QString>

namespace ql {

class OfficeCache {
public:
    static constexpr int kVersion = 1;
    static constexpr qint64 kMaxBytes = 500LL * 1024 * 1024;

    explicit OfficeCache(const QString &dir = defaultDir());
    static QString defaultDir();                    // $XDG_CACHE_HOME (~/.cache) + /omarchy-quicklook/office, created 0700
    QString keyFor(const QString &path) const;      // hex sha256(kVersion \0 realpath \0 mtime_ns \0 size)
    QString lookup(const QString &path) const;      // existing non-empty pdf -> path (mtime touched); else ""
    QString store(const QString &path, const QString &jobPdf);  // rename into place, mode 0600; returns final path or ""
    void evict(qint64 maxBytes = kMaxBytes);        // delete oldest-mtime files until total <= maxBytes
    QString dir() const { return m_dir; }

private:
    void evictExcept(qint64 maxBytes, const QString &keep);
    QString m_dir;
};

} // namespace ql
