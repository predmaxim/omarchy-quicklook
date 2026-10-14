#include "PreviewerService.h"
#include <QDBusConnectionInterface>

namespace ql {

PreviewerService::PreviewerService(Session *session, const QDBusConnection &connection, QObject *parent)
    : QObject(parent), m_session(session), m_connection(connection), m_watcher(QString(), connection, QDBusServiceWatcher::WatchForUnregistration)
{
    new PreviewerAdaptor(this);
    connect(&m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this] { requestClose(); });
    connect(m_session, &Session::visibleChanged, this, &PreviewerService::onVisibleChanged);
}

bool PreviewerService::registerOnBus()
{
    // Object first, so a client that sees the name can call it immediately.
    if (!m_connection.registerObject(kPath, this, QDBusConnection::ExportAdaptors))
        return false;
    return m_connection.interface()->registerService(kService, QDBusConnectionInterface::DontQueueService,
                                                     QDBusConnectionInterface::DontAllowReplacement)
        == QDBusConnectionInterface::ServiceRegistered;
}

bool PreviewerService::visible() const { return m_session->visible(); }

void PreviewerService::emitSelectionEvent(uint direction)
{
    if (direction < 2 || direction > 5)
        return;
    if (auto *a = findChild<PreviewerAdaptor *>())
        Q_EMIT a->SelectionEvent(direction);
    Q_EMIT selectionEvent(direction);
}

void PreviewerService::requestClose() { handleClose(); }

QDBusPendingCall PreviewerService::trashViaFiles(const QString &uri)
{
    if (!m_connection.isConnected())   // a disconnected connection would only time out (25 s)
        return QDBusPendingCall::fromError(QDBusError(QDBusError::Disconnected, QStringLiteral("no session bus")));
    auto msg = QDBusMessage::createMethodCall(QStringLiteral("org.gnome.Nautilus"), QStringLiteral("/org/gnome/Nautilus/FileOperations2"),
                                              QStringLiteral("org.gnome.Nautilus.FileOperations2"), QStringLiteral("TrashURIs"));
    msg << QStringList{uri} << QVariantMap{};
    msg.setAutoStartService(false);
    return m_connection.asyncCall(msg);
}

void PreviewerService::handleClose() { emitDecision(m_session->close()); }

void PreviewerService::handleShowFile(const QString &uri, const QString &windowHandle, bool closeIfAlreadyShown,
                                      const QString &sender)
{
    setParentHandle(windowHandle);
    watchSender(sender);
    emitDecision(m_session->showFile(uri, closeIfAlreadyShown));
}

void PreviewerService::emitDecision(const Session::Decision &decision)
{
    if (decision.effect != Session::Effect::None)
        Q_EMIT decided(decision);
}

void PreviewerService::onVisibleChanged(bool v) { sendPropertiesChanged({{QStringLiteral("Visible"), v}}); }

void PreviewerService::sendPropertiesChanged(const QVariantMap &changed)
{
    auto msg = QDBusMessage::createSignal(kPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                          QStringLiteral("PropertiesChanged"));
    msg << kInterface << changed << QStringList{};
    m_connection.send(msg);
}

void PreviewerService::setParentHandle(const QString &windowHandle)
{
    QString h;
    if (windowHandle.startsWith(QLatin1String("wayland:"))) {
        const QString x = windowHandle.mid(8);
        if (!x.isEmpty() && x != QLatin1String("(null)"))
            h = windowHandle;
    }
    if (h == m_parentHandle)
        return;
    m_parentHandle = h;
    sendPropertiesChanged({{QStringLiteral("ParentHandle"), h}});
}

void PreviewerService::watchSender(const QString &sender)
{
    // Only the latest caller is watched; a vanished watched client closes the preview.
    m_watcher.setWatchedServices(sender.isEmpty() ? QStringList{} : QStringList{sender});
}

PreviewerAdaptor::PreviewerAdaptor(PreviewerService *service) : QDBusAbstractAdaptor(service), m_service(service)
{
    setAutoRelaySignals(true);
}

bool PreviewerAdaptor::visible() const { return m_service->visible(); }
QString PreviewerAdaptor::parentHandle() const { return m_service->parentHandle(); }

void PreviewerAdaptor::ShowFile(const QString &uri, const QString &windowHandle, bool closeIfAlreadyShown,
                                const QString &, const QDBusMessage &message)
{
    m_service->handleShowFile(uri, windowHandle, closeIfAlreadyShown, message.service());
}

void PreviewerAdaptor::Close() { m_service->handleClose(); }

} // namespace ql
