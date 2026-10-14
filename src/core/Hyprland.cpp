#include "Hyprland.h"
#include <QDeadlineTimer>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QScreen>
#include <cmath>

namespace ql::hyprland {

namespace {
std::optional<Monitor> pick(const QList<HyprMonitor> &list, const QString &name)
{
    for (const auto &h : list)
        if (name.isEmpty() ? h.focused : h.name == name)
            return toMonitor(h);
    return std::nullopt;
}
}

QList<HyprMonitor> parseMonitors(const QByteArray &json)
{
    QList<HyprMonitor> out;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isArray())
        return out;
    for (const QJsonValue &v : doc.array()) {
        const QJsonObject o = v.toObject();
        HyprMonitor h;
        h.name = o["name"].toString();
        h.focused = o["focused"].toBool(false);
        h.modePx = QSize(o["width"].toInt(0), o["height"].toInt(0));
        h.scale = o["scale"].toDouble(1.0);
        h.transform = o["transform"].toInt(0);
        const QJsonArray r = o["reserved"].toArray();
        for (int i = 0; i < 4 && i < r.size(); ++i)
            h.reserved[i] = r[i].toInt(0);
        out << h;
    }
    return out;
}

Monitor toMonitor(const HyprMonitor &h)
{
    const double s = h.scale > 0 ? h.scale : 1.0;
    double w = h.modePx.width() / s, hh = h.modePx.height() / s;
    if (h.transform & 1) // 1,3,5,7 are the 90/270 degree rotations
        std::swap(w, hh);
    return {h.name,
            QSize(int(std::floor(w - h.reserved[0] - h.reserved[2])), int(std::floor(hh - h.reserved[1] - h.reserved[3]))),
            s};
}

QString socketPath()
{
    const QString sig = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
    const QString xdg = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (sig.isEmpty() || xdg.isEmpty())
        return {};
    return xdg + "/hypr/" + sig + "/.socket.sock";
}

QByteArray request(const QByteArray &command, int timeoutMs)
{
    const QString path = socketPath();
    if (path.isEmpty())
        return {};
    QDeadlineTimer deadline(timeoutMs);
    QLocalSocket sock;
    sock.connectToServer(path);
    if (!sock.waitForConnected(timeoutMs))
        return {};
    sock.write(command);
    if (!sock.waitForBytesWritten(int(qMax<qint64>(1, deadline.remainingTime()))))
        return {};
    QByteArray data;
    // Hyprland answers one request per connection, then closes it.
    while (sock.state() != QLocalSocket::UnconnectedState) {
        const qint64 left = deadline.remainingTime();
        if (left <= 0)
            return {};
        sock.waitForReadyRead(int(left));
        data += sock.readAll();
    }
    data += sock.readAll();
    return data;
}

std::optional<Monitor> focusedMonitor() { return pick(parseMonitors(request("j/monitors")), {}); }

std::optional<Monitor> monitorNamed(const QString &name)
{
    return name.isEmpty() ? std::nullopt : pick(parseMonitors(request("j/monitors")), name);
}

Monitor currentMonitor(const QString &preferredName, const Monitor &fallback)
{
    const auto list = parseMonitors(request("j/monitors"));
    if (!preferredName.isEmpty())
        if (auto m = pick(list, preferredName))
            return *m;
    return pick(list, {}).value_or(fallback);
}

Monitor screenMonitor()
{
    const QScreen *s = QGuiApplication::primaryScreen();
    return s ? Monitor{s->name(), s->size(), 1.0} : Monitor{{}, {1920, 1080}, 1.0};
}

} // namespace ql::hyprland
