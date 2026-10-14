#pragma once
// D-Bus side (spec: "D-Bus"): org.gnome.NautilusPreviewer / org.gnome.NautilusPreviewer2.
#include "Session.h"
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QObject>
#include <QVariantMap>

namespace ql {

class PreviewerService : public QObject {
    Q_OBJECT
public:
    static inline const QString kService = QStringLiteral("org.gnome.NautilusPreviewer");
    static inline const QString kPath = QStringLiteral("/org/gnome/NautilusPreviewer");
    static inline const QString kInterface = QStringLiteral("org.gnome.NautilusPreviewer2");

    PreviewerService(Session *session, const QDBusConnection &connection, QObject *parent = nullptr);

    bool registerOnBus();                  // registerObject (ExportAdaptors) first, then registerService (DontQueue)
    bool visible() const;
    QString parentHandle() const { return m_parentHandle; }
    void emitSelectionEvent(uint direction);   // only 2..5 are sent
    void requestClose();                   // UI close (keys, Super+W): same path as Close()

    // Called by PreviewerAdaptor; public for tests.
    void handleShowFile(const QString &uri, const QString &windowHandle, bool closeIfAlreadyShown,
                        const QString &sender);
    void handleClose();

Q_SIGNALS:
    void decided(const ql::Session::Decision &decision);   // every non-None decision, in order
    void selectionEvent(uint direction);                   // mirrors the D-Bus signal (tests)

private:
    void onVisibleChanged(bool visible);
    void sendPropertiesChanged(const QVariantMap &changed);
    void setParentHandle(const QString &windowHandle);     // only "wayland:<x>" with x != "(null)", else ""
    void watchSender(const QString &sender);
    void emitDecision(const Session::Decision &decision);

    Session *m_session;
    QDBusConnection m_connection;
    QString m_parentHandle;
    QDBusServiceWatcher m_watcher;
};

class PreviewerAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.gnome.NautilusPreviewer2")
    Q_CLASSINFO("D-Bus Introspection",
                "  <interface name=\"org.gnome.NautilusPreviewer2\">\n"
                "    <method name=\"ShowFile\">\n"
                "      <arg direction=\"in\" type=\"s\" name=\"uri\"/>\n"
                "      <arg direction=\"in\" type=\"s\" name=\"windowHandle\"/>\n"
                "      <arg direction=\"in\" type=\"b\" name=\"closeIfAlreadyShown\"/>\n"
                "      <arg direction=\"in\" type=\"s\" name=\"activationToken\"/>\n"
                "    </method>\n"
                "    <method name=\"Close\"/>\n"
                "    <property name=\"ParentHandle\" type=\"s\" access=\"read\"/>\n"
                "    <property name=\"Visible\" type=\"b\" access=\"read\"/>\n"
                "    <signal name=\"SelectionEvent\">\n"
                "      <arg type=\"u\" name=\"direction\"/>\n"
                "    </signal>\n"
                "  </interface>\n")
    Q_PROPERTY(bool Visible READ visible)
    Q_PROPERTY(QString ParentHandle READ parentHandle)
public:
    explicit PreviewerAdaptor(PreviewerService *service);
    bool visible() const;
    QString parentHandle() const;

public Q_SLOTS:
    void ShowFile(const QString &uri, const QString &windowHandle, bool closeIfAlreadyShown,
                  const QString &activationToken, const QDBusMessage &message);
    void Close();

Q_SIGNALS:
    void SelectionEvent(uint direction);

private:
    PreviewerService *m_service;
};

} // namespace ql
