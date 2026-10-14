#pragma once
// Session state machine (spec: "Сессия"). Every call is applied immediately, in arrival order.
#include <QMetaType>
#include <QObject>
#include <QString>

namespace ql {

class Session : public QObject {
    Q_OBJECT
public:
    enum class Effect { None, Open, Switch, Close };
    struct Decision {
        Effect effect = Effect::None;
        QString uri;               // current uri after the decision ("" when hidden)
        quint64 generation = 0;    // generation after the decision
        bool takeFocus = false;    // true only for Open
    };

    explicit Session(QObject *parent = nullptr);

    bool visible() const { return m_visible; }
    QString uri() const { return m_uri; }
    quint64 generation() const { return m_generation; }

    // hidden:            (u,true) -> Open;  (u,false) -> None (stale follow update)
    // shown same uri:    (u,true) -> Close; (u,false) -> None
    // shown other uri:   (v,any)  -> Switch (no focus)
    Decision showFile(const QString &uri, bool closeIfAlreadyShown);
    Decision close();              // D-Bus Close(), keys, Super+W, sender vanished; None when hidden

Q_SIGNALS:
    void visibleChanged(bool visible);

private:
    bool m_visible = false;
    QString m_uri;
    quint64 m_generation = 0;      // +1 on Open, Switch and Close
};

} // namespace ql

Q_DECLARE_METATYPE(ql::Session::Decision)
