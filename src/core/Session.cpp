#include "Session.h"

namespace ql {

Session::Session(QObject *parent) : QObject(parent) {}

Session::Decision Session::showFile(const QString &uri, bool closeIfAlreadyShown)
{
    if (!m_visible) {
        if (!closeIfAlreadyShown)
            return {Effect::None, m_uri, m_generation, false};
        m_visible = true;
        m_uri = uri;
        const Decision d{Effect::Open, m_uri, ++m_generation, true};
        Q_EMIT visibleChanged(true);
        return d;
    }
    if (uri == m_uri) {
        if (!closeIfAlreadyShown)
            return {Effect::None, m_uri, m_generation, false};
        return close();
    }
    m_uri = uri;
    return {Effect::Switch, m_uri, ++m_generation, false};
}

Session::Decision Session::close()
{
    if (!m_visible)
        return {Effect::None, m_uri, m_generation, false};
    m_visible = false;
    m_uri.clear();
    const Decision d{Effect::Close, QString(), ++m_generation, false};
    Q_EMIT visibleChanged(false);
    return d;
}

} // namespace ql
