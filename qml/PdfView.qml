import QtQuick

// PDF pages (office documents once converted). Page i: width min(W, H * w/h) * zoom, centered, height by aspect;
// rendered by the async provider at the page's pixel width (at most 4096 px). The Files thumbnail stands in for
// page 0 until it is drawn. Zoomed wider than the window the pages pan sideways; the content width follows page 0.
Item {
    id: view

    Rectangle {   // the 8 px gap between pages in the theme's background
        anchors.fill: parent
        color: ql.theme.background
        visible: list.count > 1
    }

    ListView {
        id: list
        objectName: "pdfList"
        anchors.fill: parent
        model: ql.pdf
        spacing: 8
        cacheBuffer: 0   // no asynchronous buffer incubation: a reset during it makes DelegateModel warn
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.AutoFlickDirection
        contentWidth: Math.max(width, ql.contentRect.width * ql.zoom)   // ql.contentRect: page 0 at the fit
        keyNavigationEnabled: false
        focus: false
        visible: !ql.busy
        delegate: Item {
            id: page
            required property int index
            required property real aspect
            required property string source
            readonly property real pageWidth: Math.min(ListView.view.width, ListView.view.height / aspect) * ql.zoom
            width: ListView.view.contentWidth
            height: pageWidth * aspect

            Rectangle {
                x: (page.width - page.pageWidth) / 2
                width: page.pageWidth
                height: page.height
                color: "white"
            }
            Image {   // Files thumbnail until page 0 is drawn (spec "Показ" 5)
                x: (page.width - page.pageWidth) / 2
                width: page.pageWidth
                height: page.height
                visible: page.index === 0 && pageImage.status !== Image.Ready
                source: page.index === 0 ? ql.placeholderSource : ""
                fillMode: Image.PreserveAspectFit
                cache: false
                smooth: true
            }
            Image {
                id: pageImage
                objectName: "pageImage"
                x: (page.width - page.pageWidth) / 2
                width: page.pageWidth
                height: page.height
                source: page.source
                sourceSize.width: Math.round(Math.min(page.pageWidth, 4096 / ql.scale))   // logical: Qt applies the DPR
                asynchronous: true
                retainWhileLoading: true
                cache: false
            }
        }
    }
    ScrollIndicator { flickable: list }

    Connections {
        target: ql
        enabled: view.visible
        function onZoomChanged() {
            if (ql.zoom === 1)
                list.contentX = list.originX
        }
        // Ctrl +/-/0: the spot in the middle of the window stays there (page and the place on it)
        function onZoomStepped(from) {
            const cy = list.contentY + list.height / 2
            const index = list.indexAt(list.contentX + list.width / 2, cy)
            const item = index >= 0 ? list.itemAtIndex(index) : null
            const fy = item ? (cy - item.y) / item.height : 0
            const fx = (list.contentX - list.originX + list.width / 2) / Math.max(list.width, ql.contentRect.width * from)
            Qt.callLater(function() {
                if (index >= 0) {
                    list.positionViewAtIndex(index, ListView.Beginning)
                    const it = list.itemAtIndex(index)
                    const top = list.originY - list.topMargin
                    const bottom = Math.max(top, list.originY + list.contentHeight + list.bottomMargin - list.height)
                    if (it)
                        list.contentY = Math.max(top, Math.min(bottom, it.y + fy * it.height - list.height / 2))
                }
                list.contentX = list.originX + Math.max(0, Math.min(list.contentWidth - list.width, fx * list.contentWidth - list.width / 2))
            })
        }
    }

    Item {   // office conversion: white paper with the type icon, the name and a busy indicator
        visible: ql.busy
        x: ql.contentRect.x
        y: ql.contentRect.y
        width: ql.contentRect.width
        height: ql.contentRect.height
        Image {   // ODF built-in preview or the Files thumbnail, when there is one
            anchors.fill: parent
            source: ql.placeholderSource
            fillMode: Image.PreserveAspectFit
            cache: false
            smooth: true
        }
        Column {
            anchors.centerIn: parent
            width: parent.width - 48
            spacing: 12
            visible: ql.placeholderSource === ""
            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 96
                height: 96
                source: ql.cardIcon
                cache: false
                smooth: true
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: ql.cardName
                textFormat: Text.PlainText
                elide: Text.ElideMiddle
                color: "#303030"
                font.pixelSize: ql.theme.titleSize
            }
        }
        Rectangle {   // busy indicator: a rotating arc segment
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 32
            width: 28
            height: 28
            radius: 14
            color: "transparent"
            border.width: 3
            border.color: "#c8c8c8"
            Rectangle {
                width: 8
                height: 8
                radius: 4
                x: parent.width / 2 - 4
                y: -2.5
                color: ql.theme.accent
            }
            RotationAnimator on rotation {
                from: 0
                to: 360
                duration: 1000
                loops: Animation.Infinite
                running: ql.busy
            }
        }
    }
}
