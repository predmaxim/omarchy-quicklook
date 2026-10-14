import QtQuick

// File, folder or error card at ql.cardRect: 96 px type icon, name (two lines, computed by Controller for this
// width and font), type, size and date (or items), reason in the accent color. Narrower than 440 (an error inside
// a small content window): icon above the text, name in one line. All strings are foreign: plain text only.
Item {
    id: card
    objectName: "fileCard"
    readonly property bool narrow: width < 440
    x: ql.cardRect.x
    y: ql.cardRect.y
    width: ql.cardRect.width
    height: ql.cardRect.height

    Image {
        id: icon
        x: card.narrow ? (card.width - width) / 2 : 24
        y: card.narrow ? 12 : (card.height - height) / 2
        width: 96
        height: 96
        source: ql.cardIcon
        cache: false
        smooth: true
    }
    Column {
        x: card.narrow ? 24 : icon.x + icon.width + 16
        y: card.narrow ? icon.y + icon.height + 8 : (card.height - height) / 2
        width: card.width - x - 24
        spacing: 4
        Text {
            width: parent.width
            text: ql.cardName
            textFormat: Text.PlainText
            horizontalAlignment: card.narrow ? Text.AlignHCenter : Text.AlignLeft
            color: ql.theme.foreground
            font.pixelSize: ql.theme.titleSize
        }
        Text {
            width: parent.width
            visible: !card.narrow && text !== ""
            text: ql.cardType
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: ql.theme.muted
            font.pixelSize: ql.theme.baseSize
        }
        Text {
            width: parent.width
            visible: !card.narrow && text !== ""
            text: ql.cardDetails
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: ql.theme.muted
            font.pixelSize: ql.theme.captionSize
        }
        Text {
            width: parent.width
            visible: text !== ""
            text: ql.cardReason
            textFormat: Text.PlainText
            horizontalAlignment: card.narrow ? Text.AlignHCenter : Text.AlignLeft
            elide: Text.ElideRight
            color: ql.theme.accent
            font.pixelSize: ql.theme.baseSize
        }
    }
}
