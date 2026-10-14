#pragma once
// Keys at the window root (spec: "Клавиши").
#include <QObject>

namespace ql {

enum class KeyAction {
    None, Close, Open, Left, Right, Up, Down, PageUp, PageDown, Home, End,
    FullScreen, PlayPause, SeekBack, SeekForward, Mute,
    Trash, ZoomIn, ZoomOut, ZoomFit, PanLeft, PanRight, PanUp, PanDown   // appended: QML knows PageUp..End as 7..10
};

// Letters: Latin Qt key first, then nativeScanCode (evdev + 8): Q 24, F 41, K 45, P 33, J 44, L 46, M 58.
// Del (no modifiers) -> Trash. Ctrl (without Alt/Meta): = and + zoom in, - out, 0 fit, arrows pan.
// Other keys with Ctrl, Alt or Meta are not ours (None).
KeyAction mapKey(int key, quint32 nativeScanCode, Qt::KeyboardModifiers modifiers);
uint selectionDirection(KeyAction action);   // Up 2, Down 3, Left 4, Right 5, else 0

class KeyRouter : public QObject {   // installed on the preview QQuickWindow
    Q_OBJECT
public:
    using QObject::QObject;
    bool eventFilter(QObject *watched, QEvent *event) override;
        // KeyPress (not auto-repeat for Close/Open/FullScreen/PlayPause/Mute/Trash/ZoomFit) -> action(), consumed;
        // QEvent::Close -> closeRequested(), consumed (keeps the platform window alive)

Q_SIGNALS:
    void action(ql::KeyAction action);
    void closeRequested();
};

} // namespace ql

Q_DECLARE_METATYPE(ql::KeyAction)
