import QtQuick

// Thin bar at the right edge while the flickable moves, fading out 1 s later. It also performs the keyboard
// scrolling of its flickable (PgUp/PgDn/Home/End arrive as ql.scrollRequested, Ctrl+arrows as ql.panRequested),
// so the views share one copy.
Rectangle {
    id: bar
    required property Flickable flickable

    x: parent.width - width - 2
    y: flickable.visibleArea.yPosition * flickable.height
    width: 3
    height: flickable.visibleArea.heightRatio * flickable.height
    radius: 1.5
    color: ql.theme.muted
    visible: flickable.contentHeight > flickable.height
    opacity: 0
    Behavior on opacity { NumberAnimation { duration: 200 } }

    Connections {
        target: bar.flickable
        function onContentYChanged() {
            bar.opacity = 1
            fade.restart()
        }
    }
    Timer {
        id: fade
        interval: 1000
        onTriggered: bar.opacity = 0
    }

    Connections {
        target: ql
        enabled: bar.flickable.visible
        // int(ql::KeyAction): PageUp 7, PageDown 8, Home 9, End 10
        function onScrollRequested(action) {
            const f = bar.flickable
            const top = f.originY - f.topMargin
            const bottom = Math.max(top, f.originY + f.contentHeight + f.bottomMargin - f.height)
            if (action === 7)
                f.contentY = Math.max(top, f.contentY - f.height)
            else if (action === 8)
                f.contentY = Math.min(bottom, f.contentY + f.height)
            else if (action === 9)
                f.positionViewAtBeginning()
            else if (action === 10)
                f.positionViewAtEnd()
        }
        function onPanRequested(dx, dy) {   // a tenth of the view, within the content
            const f = bar.flickable
            if (dx !== 0 && f.contentWidth > f.width)
                f.contentX = Math.max(f.originX, Math.min(f.originX + f.contentWidth - f.width, f.contentX + dx * f.width / 10))
            if (dy !== 0) {
                const top = f.originY - f.topMargin
                const bottom = Math.max(top, f.originY + f.contentHeight + f.bottomMargin - f.height)
                f.contentY = Math.max(top, Math.min(bottom, f.contentY + dy * f.height / 10))
            }
        }
    }
}
