import QtQuick

// Rows of the text model (StyledText, escaped by TextModel). Width comes from the window, never from the text.
Item {
    ListView {
        id: list
        objectName: "textList"
        anchors.fill: parent
        model: ql.text
        topMargin: 16
        bottomMargin: 16
        reuseItems: true
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
            font.pixelSize: 16
            color: dim ? ql.theme.muted : ql.theme.foreground
        }
    }
    ScrollIndicator { flickable: list }
}
