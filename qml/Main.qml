import QtQuick

// The one preview window. Controller (context property "ql") shows and hides it, sizes it (min == max) and fills
// these views; keys go through its KeyRouter, so no item here takes keyboard focus. Never `visible = true` here.
Window {
    id: root
    visible: false
    color: ql.kind === "video" ? "black"
         : (ql.kind === "pdf" || ql.kind === "office") ? "white"
         : ql.theme.background

    ImageView { anchors.fill: parent; visible: ql.kind === "image" || ql.kind === "svg" }
    PdfView { anchors.fill: parent; visible: ql.kind === "pdf" || ql.kind === "office" }
    TextView { anchors.fill: parent; visible: ql.kind === "text" && ql.markdown === "" }
    MarkdownView { anchors.fill: parent; visible: ql.kind === "text" && ql.markdown !== "" }
    VideoView { anchors.fill: parent; visible: ql.kind === "video" }
    AudioCard { visible: ql.kind === "audio" }
    FileCard { visible: ql.kind === "file" || ql.kind === "folder" || ql.cardReason !== "" }

    Rectangle {   // transient line ("no app for this type")
        visible: ql.hint !== ""
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 16
        width: Math.min(hintText.implicitWidth + 24, parent.width - 16)
        height: hintText.implicitHeight + 12
        radius: height / 2
        color: ql.theme.background
        border.color: ql.theme.muted
        Text {
            id: hintText
            anchors.centerIn: parent
            width: parent.width - 24
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            text: ql.hint
            textFormat: Text.PlainText
            color: ql.theme.foreground
            font.pixelSize: ql.theme.baseSize
        }
    }
}
