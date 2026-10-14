#include "Keys.h"
#include <QEvent>
#include <QKeyEvent>

namespace ql {

KeyAction mapKey(int key, quint32 nativeScanCode, Qt::KeyboardModifiers modifiers)
{
    if (modifiers & (Qt::AltModifier | Qt::MetaModifier))
        return KeyAction::None;
    if (modifiers & Qt::ControlModifier) {   // = - 0 and arrows sit on the same keys in us and ru
        switch (key) {
        case Qt::Key_Equal: case Qt::Key_Plus: return KeyAction::ZoomIn;
        case Qt::Key_Minus: return KeyAction::ZoomOut;
        case Qt::Key_0: return KeyAction::ZoomFit;
        case Qt::Key_Left: return KeyAction::PanLeft;
        case Qt::Key_Right: return KeyAction::PanRight;
        case Qt::Key_Up: return KeyAction::PanUp;
        case Qt::Key_Down: return KeyAction::PanDown;
        default: return KeyAction::None;
        }
    }
    switch (key) {
    case Qt::Key_Delete: return modifiers & Qt::ShiftModifier ? KeyAction::None : KeyAction::Trash;
    case Qt::Key_Space: case Qt::Key_Escape: return KeyAction::Close;
    case Qt::Key_Return: case Qt::Key_Enter: return KeyAction::Open;
    case Qt::Key_Left: return KeyAction::Left;
    case Qt::Key_Right: return KeyAction::Right;
    case Qt::Key_Up: return KeyAction::Up;
    case Qt::Key_Down: return KeyAction::Down;
    case Qt::Key_PageUp: return KeyAction::PageUp;
    case Qt::Key_PageDown: return KeyAction::PageDown;
    case Qt::Key_Home: return KeyAction::Home;
    case Qt::Key_End: return KeyAction::End;
    case Qt::Key_F11: return KeyAction::FullScreen;
    default: break;
    }
    // Latin letter: use it. Other letters (Cyrillic etc., code points): by scan code.
    // Punctuation is deliberately not looked up (Dvorak/AZERTY would misfire).
    if (key < Qt::Key_A || key > Qt::Key_Z) {
        if (key < 0x80 || key > 0x10FFFF || !QChar::isLetter(char32_t(key))) return KeyAction::None;
        switch (nativeScanCode) {
        case 24: key = Qt::Key_Q; break;
        case 41: key = Qt::Key_F; break;
        case 45: key = Qt::Key_K; break;
        case 33: key = Qt::Key_P; break;
        case 44: key = Qt::Key_J; break;
        case 46: key = Qt::Key_L; break;
        case 58: key = Qt::Key_M; break;
        default: return KeyAction::None;
        }
    }
    switch (key) {
    case Qt::Key_Q: return KeyAction::Close;
    case Qt::Key_F: return KeyAction::FullScreen;
    case Qt::Key_K: case Qt::Key_P: return KeyAction::PlayPause;
    case Qt::Key_J: return KeyAction::SeekBack;
    case Qt::Key_L: return KeyAction::SeekForward;
    case Qt::Key_M: return KeyAction::Mute;
    default: return KeyAction::None;
    }
}

uint selectionDirection(KeyAction action)
{
    switch (action) {
    case KeyAction::Up: return 2;
    case KeyAction::Down: return 3;
    case KeyAction::Left: return 4;
    case KeyAction::Right: return 5;
    default: return 0;
    }
}

bool KeyRouter::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched);
    if (event->type() == QEvent::Close) {
        Q_EMIT closeRequested();
        return true;
    }
    if (event->type() != QEvent::KeyPress) return false;
    auto *e = static_cast<QKeyEvent *>(event);
    const KeyAction a = mapKey(e->key(), e->nativeScanCode(), e->modifiers());
    if (a == KeyAction::None) return false;
    const bool oneShot = a == KeyAction::Close || a == KeyAction::Open || a == KeyAction::FullScreen
        || a == KeyAction::PlayPause || a == KeyAction::Mute || a == KeyAction::Trash || a == KeyAction::ZoomFit;
    if (e->isAutoRepeat() && oneShot) return true;
    Q_EMIT action(a);
    return true;
}

} // namespace ql
