import QtQuick

// Rows of the text model (StyledText, escaped by TextModel). Width comes from the window, never from the text.
// Zoom is the font size (16 px at 1); the row at the top stays at the top.
Item {
    id: view
    ListView {
        id: list
        objectName: "textList"
        anchors.fill: parent
        model: ql.text
        topMargin: 16
        bottomMargin: 16
        reuseItems: true
        cacheBuffer: 0   // no asynchronous buffer incubation: a reset during it makes DelegateModel warn
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: false
        focus: false
        delegate: Text {
            required property string rowText
            required property bool dim
            x: 16
            width: ListView.view.width - 32
            text: rowText
            textFormat: Text.StyledText
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            font.family: "monospace"
            font.pixelSize: Math.round(16 * ql.zoom)
            color: dim ? ql.theme.muted : ql.theme.foreground
        }
    }
    ScrollIndicator { flickable: list }

    Connections {
        target: ql
        enabled: view.visible
        function onZoomStepped(from) {
            const top = list.indexAt(20, list.contentY + 1)
            if (top >= 0)
                Qt.callLater(function() { list.positionViewAtIndex(top, ListView.Beginning) })
        }
    }
}
