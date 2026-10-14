import QtQuick

// 520x180 at ql.cardRect: cover (or the type icon), title, "artist — album", time and progress. Tags are foreign
// strings: plain text only.
Item {
    x: ql.cardRect.x
    y: ql.cardRect.y
    width: ql.cardRect.width
    height: ql.cardRect.height

    function time(ms) {
        const s = Math.floor(ms / 1000)
        const m = Math.floor(s / 60)
        return m + ":" + String(s % 60).padStart(2, "0")
    }

    Image {
        id: cover
        x: 24
        y: (parent.height - height) / 2
        width: 132
        height: 132
        source: ql.coverSource !== "" ? ql.coverSource : ql.cardIcon
        fillMode: Image.PreserveAspectFit
        cache: false
        smooth: true
    }
    Column {
        x: cover.x + cover.width + 16
        width: parent.width - x - 24
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6
        Text {
            width: parent.width
            text: ql.audioTitle
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: ql.theme.foreground
            font.pixelSize: ql.theme.titleSize
        }
        Text {
            width: parent.width
            visible: text !== ""
            text: ql.audioSubtitle
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: ql.theme.muted
            font.pixelSize: ql.theme.baseSize
        }
        Text {
            text: ql.media.duration > 0 ? time(ql.media.position) + " / " + time(ql.media.duration) : ""
            textFormat: Text.PlainText
            color: ql.theme.muted
            font.pixelSize: ql.theme.captionSize
        }
        Rectangle {
            width: parent.width
            height: 3
            color: ql.theme.muted
            Rectangle {
                height: parent.height
                width: ql.media.duration > 0 ? parent.width * ql.media.position / ql.media.duration : 0
                color: ql.theme.accent
            }
        }
    }
}
