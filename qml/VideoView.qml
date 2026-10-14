import QtQuick
import QtMultimedia

// Placeholder (Files thumbnail) in the content rect until the first frame, then the frames MediaController
// forwards into this VideoOutput (default ClearOutput: MediaController never forwards empty frames, so the last
// frame stays at the end, and its own clear on switch/close must work — task 15). Click: pause / play.
Item {
    Image {
        x: ql.contentRect.x
        y: ql.contentRect.y
        width: ql.contentRect.width
        height: ql.contentRect.height
        visible: !ql.media.hasFrame
        source: ql.placeholderSource
        fillMode: Image.PreserveAspectFit
        cache: false
        smooth: true
    }
    VideoOutput {
        id: vo
        x: ql.contentRect.x
        y: ql.contentRect.y
        width: ql.contentRect.width
        height: ql.contentRect.height
        fillMode: VideoOutput.PreserveAspectFit
        Component.onCompleted: ql.setVideoSink(vo.videoSink)
    }
    Rectangle {   // progress, an indicator only
        anchors.bottom: parent.bottom
        height: 3
        width: ql.media.duration > 0 ? parent.width * ql.media.position / ql.media.duration : 0
        color: ql.theme.accent
    }
    MouseArea {
        anchors.fill: parent
        onClicked: ql.togglePlay()
    }
}
