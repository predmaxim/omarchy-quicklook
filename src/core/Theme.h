#pragma once
// Omarchy theme: palette, font size, icon theme (spec: "Тема Omarchy", "Общее").
#include <QColor>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>

namespace ql {

class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QColor background READ background NOTIFY changed)
    Q_PROPERTY(QColor foreground READ foreground NOTIFY changed)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor muted READ muted NOTIFY changed)
    Q_PROPERTY(int baseSize READ baseSize NOTIFY changed)
    Q_PROPERTY(int captionSize READ captionSize NOTIFY changed)
    Q_PROPERTY(int titleSize READ titleSize NOTIFY changed)
    Q_PROPERTY(QString iconTheme READ iconTheme NOTIFY changed)
public:
    explicit Theme(QObject *parent = nullptr);   // calls reload() and starts watching

    void reload();          // `omarchy-theme-color --all` (fallback: colors.toml), base-size, icons.theme
    QHash<QString, QColor> palette() const { return m_palette; }
    QColor background() const;
    QColor foreground() const;
    QColor accent() const;
    QColor muted() const;
    int baseSize() const { return m_baseSize; }
    int captionSize() const;   // round(base * 0.833)
    int titleSize() const;     // round(base * 1.167)
    QString iconTheme() const { return m_iconTheme; }

    static QHash<QString, QColor> parsePalette(const QByteArray &tsv);       // "key<TAB>#rrggbb" per line
    static QHash<QString, QColor> parseColorsToml(const QByteArray &toml);   // key = "#rrggbb"; adds muted -> color8 -> foreground
    static int parseBaseSize(const QByteArray &toml, int fallback);          // [font] base-size = N
    static QString currentDir();      // $XDG_STATE_HOME or ~/.local/state, + /omarchy/current
    static QString userShellToml();   // $XDG_CONFIG_HOME or ~/.config, + /omarchy/shell.toml

Q_SIGNALS:
    void changed();

private:
    void rewatch();   // re-add theme.name, user shell.toml and ~/.config/omarchy (atomic writes drop watches)

    QHash<QString, QColor> m_palette;
    int m_baseSize = 16;
    QString m_iconTheme;
    QFileSystemWatcher m_watcher;
};

} // namespace ql
