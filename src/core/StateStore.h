#pragma once
// Remembered state: mute (spec: "Клавиши", M).
#include <QString>

namespace ql {

class StateStore {
public:
    explicit StateStore(const QString &file = defaultPath());
    static QString defaultPath();   // QStandardPaths::GenericStateLocation + "/omarchy-quicklook/state.ini"
    bool muted() const;
    void setMuted(bool muted);

private:
    QString m_file;
};

} // namespace ql
