import QtQuick

// Placeholder (Files thumbnail) and the decoded image, both in the content rect scaled by ql.zoom; the image replaces
// the placeholder in place. Zoomed past the window the image pans: Ctrl+arrows (ScrollIndicator), drag, wheel.
// image://ql ids change per generation and slot: never cache.
Item {
    id: view
    readonly property real boxWidth: ql.contentRect.width * ql.zoom
    readonly property real boxHeight: ql.contentRect.height * ql.zoom

    Flickable {
        id: flick
        objectName: "imageFlick"
        anchors.fill: parent
        contentWidth: Math.max(width, view.boxWidth)
        contentHeight: Math.max(height, view.boxHeight)
        interactive: ql.zoom > 1
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        // centered: at zoom 1 exactly the content rect (the layouts center it)
        Image {
            x: (flick.contentWidth - view.boxWidth) / 2
            y: (flick.contentHeight - view.boxHeight) / 2
            width: view.boxWidth
            height: view.boxHeight
            visible: main.status !== Image.Ready
            source: ql.placeholderSource
            fillMode: Image.PreserveAspectFit
            cache: false
            smooth: true
        }
        Image {
            id: main
            objectName: "mainImage"
            x: (flick.contentWidth - view.boxWidth) / 2
            y: (flick.contentHeight - view.boxHeight) / 2
            width: view.boxWidth
            height: view.boxHeight
            visible: ql.imageSource !== ""
            source: ql.imageSource
            fillMode: Image.PreserveAspectFit
            cache: false
            smooth: true
            mipmap: true   // the full-size decode for zoom, shown back at the fit
        }
    }
    ScrollIndicator { flickable: flick }

    Connections {
        target: ql
        enabled: view.visible
        function onZoomChanged() {
            if (ql.zoom === 1) {
                flick.contentX = 0
                flick.contentY = 0
            }
        }
        // Ctrl +/-/0: the point in the middle of the window stays there
        function onZoomStepped(from) {
            const oldW = Math.max(flick.width, ql.contentRect.width * from)
            const oldH = Math.max(flick.height, ql.contentRect.height * from)
            const fx = (flick.contentX + flick.width / 2) / oldW
            const fy = (flick.contentY + flick.height / 2) / oldH
            flick.contentX = Math.max(0, Math.min(flick.contentWidth - flick.width, fx * flick.contentWidth - flick.width / 2))
            flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, fy * flick.contentHeight - flick.height / 2))
        }
    }
}
