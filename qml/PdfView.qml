import QtQuick

// PDF pages (office documents once converted). Page i: width min(W, H * w/h), centered, height by aspect; rendered
// by the async provider at the page's pixel width. The Files thumbnail stands in for page 0 until it is drawn.
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
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: false
        focus: false
        visible: !ql.busy
        delegate: Item {
            id: page
            required property int index
            required property real aspect
            required property string source
            readonly property real pageWidth: Math.min(width, ListView.view.height / aspect)
            width: ListView.view.width
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
                sourceSize.width: Math.round(page.pageWidth * ql.scale)
                asynchronous: true
                retainWhileLoading: true
                cache: false
            }
        }
    }
    ScrollIndicator { flickable: list }

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
