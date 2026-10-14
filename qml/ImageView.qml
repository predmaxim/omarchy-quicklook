import QtQuick

// Placeholder (Files thumbnail) and the decoded image, both exactly in the content rect; the image replaces the
// placeholder in place. image://ql ids change per generation and slot: never cache.
Item {
    Image {
        x: ql.contentRect.x
        y: ql.contentRect.y
        width: ql.contentRect.width
        height: ql.contentRect.height
        visible: main.status !== Image.Ready
        source: ql.placeholderSource
        fillMode: Image.PreserveAspectFit
        cache: false
        smooth: true
    }
    Image {
        id: main
        objectName: "mainImage"
        x: ql.contentRect.x
        y: ql.contentRect.y
        width: ql.contentRect.width
        height: ql.contentRect.height
        visible: ql.imageSource !== ""
        source: ql.imageSource
        fillMode: Image.PreserveAspectFit
        cache: false
        smooth: true
    }
}
