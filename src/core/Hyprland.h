#pragma once
// Hyprland IPC: focused/named monitor (spec: "Размер окна", монитор).
#include "Types.h"
#include <QByteArray>
#include <QList>
#include <optional>

namespace ql {

struct HyprMonitor {
    QString name;
    bool focused = false;
    QSize modePx;                   // "width"/"height": mode size, not rotated
    double scale = 1.0;
    int transform = 0;
    int reserved[4] = {0, 0, 0, 0}; // left, top, right, bottom in logical px
};

namespace hyprland {
QList<HyprMonitor> parseMonitors(const QByteArray &json);
Monitor toMonitor(const HyprMonitor &h);          // logical = mode / scale, swapped for transform 1,3,5,7, minus reserved
QString socketPath();                             // $XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/.socket.sock, "" if unset
QByteArray request(const QByteArray &command, int timeoutMs = 100); // "" on failure; safe from any thread
std::optional<Monitor> focusedMonitor();
std::optional<Monitor> monitorNamed(const QString &name);
Monitor currentMonitor(const QString &preferredName, const Monitor &fallback);
        // preferredName non-empty and present -> it; else focused; else fallback
Monitor screenMonitor();                          // GUI thread only: primary QScreen logical size, scale 1
} // namespace hyprland

} // namespace ql
