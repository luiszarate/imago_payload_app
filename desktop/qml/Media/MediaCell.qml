import QtQuick
import Imago

// One shot of the sheet (the Orus L saves an EO and an IR file per shot, field report
// 2026-10-20): a 16:9 frame (thumbnail cropped to fill, or a tile for a video), EO+IR / IR /
// TIFF badges, and below it the capture time and the "on this device" check (both files).
Item {
    id: root

    required property int index
    required property string name
    required property bool video
    required property bool thermal
    required property bool radiometric
    required property string sizeText
    required property string timeText
    required property string thumbnailUrl
    required property bool thumbnailFailed
    required property bool onDevice
    required property bool selected
    required property bool paired
    required property string pair
    /// The picture is the IR twin's: EO photos carry no thumbnail and are ~10 MB.
    required property bool thumbnailFromPair

    /// The sheet's keyboard cursor, and the capture the timeline points at while scrubbing.
    property bool isCurrent: false
    property bool keyboardFocus: false
    property bool scrubTarget: false
    property real gutter: 6

    signal clicked(int modifiers)
    signal doubleClicked()
    signal menuRequested(real x, real y)

    readonly property real frameHeight: Math.round((width - 2 * gutter) * 9 / 16)

    Accessible.role: Accessible.ListItem
    Accessible.name: (video ? "Video " : radiometric ? "Radiometric TIFF " : thermal ? "IR photo " : "Photo ")
                     + (timeText || name)
    Accessible.selected: selected

    // The core fetches thumbnails newest first; one on screen jumps the queue.
    function askForThumbnail() {
        if (!video && thumbnailUrl.length === 0 && !thumbnailFailed) App.media.requestThumbnail(name)
    }
    Component.onCompleted: askForThumbnail()
    onThumbnailUrlChanged: askForThumbnail()

    Rectangle {
        id: frame
        x: root.gutter
        y: root.gutter
        width: root.width - 2 * root.gutter
        height: root.frameHeight
        color: Theme.surfaceRaised
        clip: true

        // Loading or no thumbnail: the kind's icon on a quiet surface.
        Icon {
            anchors.centerIn: parent
            visible: !root.video && picture.status !== Image.Ready
            name: root.radiometric ? "grid_on" : root.thermal ? "whatshot" : "image"
            size: Math.min(28, parent.height / 3)
            color: root.thumbnailFailed ? Theme.alpha(Theme.text, 0.30) : Theme.alpha(Theme.text, 0.16)
        }
        Image {
            id: picture
            anchors.fill: parent
            visible: status === Image.Ready
            source: root.thumbnailUrl
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: false
            sourceSize.width: Math.round(width * Screen.devicePixelRatio)
            sourceSize.height: Math.round(height * Screen.devicePixelRatio)
            opacity: status === Image.Ready ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 160; easing.type: Easing.OutQuad } }
        }

        // Videos: no picture, a tile with the play glyph and the size.
        Item {
            anchors.fill: parent
            visible: root.video
            Rectangle {
                anchors.centerIn: parent
                width: Math.min(44, parent.height * 0.42)
                height: width
                radius: width / 2
                color: Theme.alpha(Theme.text, 0.08)
                border.color: Theme.alpha(Theme.text, 0.18)
                Icon {
                    anchors.centerIn: parent
                    anchors.horizontalCenterOffset: 1
                    name: "play_arrow"
                    size: parent.width * 0.6
                    color: Theme.text
                }
            }
            Text {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 7
                text: root.sizeText
                color: Theme.textSecondary
                font: AppFont.figures(11, 600)
            }
        }

        Row {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 6
            spacing: 4
            Repeater {
                model: [root.paired ? "EO+IR" : root.thermal && !root.radiometric ? "IR" : "",
                        root.radiometric ? "TIFF" : ""].filter(t => t)
                Rectangle {
                    required property string modelData
                    width: badge.implicitWidth + 10
                    height: 17
                    radius: 4
                    color: Theme.alpha(Theme.background, 0.78)
                    Text {
                        id: badge
                        anchors.centerIn: parent
                        text: parent.modelData
                        color: Theme.text
                        font: AppFont.display(10, 700, 0.4)
                    }
                }
            }
        }

        // Says quietly that the picture is the thermal view, so nobody takes it for the EO photo.
        Rectangle {
            visible: root.thumbnailFromPair && picture.status === Image.Ready && !root.video
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: 6
            width: viewTag.implicitWidth + 10
            height: 17
            radius: 4
            color: Theme.alpha(Theme.background, 0.62)
            Text {
                id: viewTag
                anchors.centerIn: parent
                text: "IR view"
                color: Theme.textSecondary
                font: AppFont.body(10, 700)
            }
        }

        // Selection: magenta edge and check.
        Rectangle {
            anchors.fill: parent
            color: root.selected ? Theme.alpha(Theme.magenta, 0.10) : "transparent"
            border.width: root.selected || root.scrubTarget ? 2 : mouse.containsMouse || (root.isCurrent && root.keyboardFocus) ? 1 : 0
            border.color: root.selected || root.scrubTarget ? Theme.magenta
                          : root.isCurrent && root.keyboardFocus ? Theme.alpha(Theme.text, 0.7) : Theme.alpha(Theme.text, 0.28)
        }
        Rectangle {
            visible: root.selected
            x: 6
            y: 6
            width: 20
            height: 20
            radius: 10
            gradient: Theme.accent
            Icon {
                anchors.centerIn: parent
                name: "check"
                size: 15
                color: Theme.text
            }
        }
    }

    Item {
        anchors.left: frame.left
        anchors.right: frame.right
        anchors.top: frame.bottom
        anchors.topMargin: 5
        height: 18

        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: root.timeText || root.name
            width: Math.min(implicitWidth, parent.width - 22)
            elide: Text.ElideMiddle
            color: root.selected ? Theme.text : Theme.textSecondary
            font: root.timeText ? AppFont.figures(12, 600) : AppFont.body(12, 600)
        }
        Icon {
            visible: root.onDevice
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            name: "check_circle"
            size: 15
            color: Theme.ok
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        onClicked: event => {
            if (event.button === Qt.RightButton) root.menuRequested(event.x, event.y)
            else root.clicked(event.modifiers)
        }
        onDoubleClicked: event => { if (event.button === Qt.LeftButton) root.doubleClicked() }
    }

    ToolTipText {
        visible: mouse.containsMouse
        delay: 900
        text: root.name + (root.paired ? " + " + root.pair : "") + (root.sizeText ? "  ·  " + root.sizeText : "")
              + (root.onDevice ? "  ·  on this device" : "")
    }
}
