import QtQuick

// A Markdown file rendered: HTML from text::markdownHtml (line heights, cell padding are in it); V switches to
// TextView with the raw text. Zoom is the base font size (16 px at 1), headings scale with it.
Item {
    Flickable {
        id: flick
        objectName: "markdownView"
        anchors.fill: parent
        contentWidth: width
        contentHeight: md.height
        topMargin: 48      // geometry::kTextPadding
        bottomMargin: 48
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Text {
            id: md
            x: 48            // not padding: QTextDocument tables ignore it
            width: flick.width - 96
            text: ql.markdown
            textFormat: Text.RichText
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            font.pixelSize: Math.round(16 * ql.zoom)
            color: ql.theme.foreground
            linkColor: ql.theme.accent
            onLinkActivated: link => Qt.openUrlExternally(link)
        }
    }
    ScrollIndicator { flickable: flick }
}
