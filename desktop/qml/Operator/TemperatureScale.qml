import QtQuick
import Imago

// The picture's temperature range on the palette's colours: coldest on the left, hottest on the
// right, a tick at the mean and the isotherm band outlined where it falls.
Item {
    id: root

    property bool compact: false
    readonly property var scale: App.thermalScale

    implicitHeight: column.implicitHeight
    Accessible.role: Accessible.Indicator
    Accessible.name: "Temperature in the picture"
    Accessible.description: scale.valid ? "from " + scale.minText + " to " + scale.maxText + ", mean " + scale.meanText
                                        : "Not reported"

    Column {
        id: column
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        spacing: root.compact ? 3 : 5

        Item {
            id: bar
            width: parent.width
            height: root.compact ? 8 : 11

            PaletteStrip {
                anchors.fill: parent
                colors: App.irPaletteColors
            }
            Rectangle {
                visible: root.scale.bandFrom >= 0
                x: root.scale.bandFrom * bar.width
                width: Math.max(4, (root.scale.bandTo - root.scale.bandFrom) * bar.width)
                height: bar.height
                radius: 3
                color: "transparent"
                border.color: Theme.danger
                border.width: 2
            }
            Rectangle {
                visible: root.scale.meanAt >= 0
                x: root.scale.meanAt * bar.width - 1
                y: -3
                width: 2
                height: bar.height + 6
                radius: 1
                color: Theme.text
                border.color: Theme.alpha(Theme.background, 0.6)
                border.width: 0.5
            }
        }

        Item {
            width: parent.width
            height: minText.implicitHeight

            Text {
                id: minText
                anchors.left: parent.left
                text: root.scale.minText
                color: Theme.text
                font: AppFont.figures(root.compact ? 10 : 12, 600)
            }
            // On a narrow scale (the thermal bar beside the attitude indicator) the mean drops its
            // label, then goes, rather than run into the ends.
            Text {
                id: meanText
                readonly property real room: parent.width - Math.max(minText.implicitWidth, maxText.implicitWidth) * 2 - 16
                readonly property string full: root.scale.meanText ? "MEAN " + root.scale.meanText : ""
                anchors.horizontalCenter: parent.horizontalCenter
                text: meanMetrics.advanceWidth <= room ? full : ""
                color: Theme.textSecondary
                font: AppFont.figures(root.compact ? 10 : 12, 600)
                TextMetrics {
                    id: meanMetrics
                    text: meanText.full
                    font: meanText.font
                }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: meanText.text === "" && shortMetrics.advanceWidth <= meanText.room
                text: root.scale.meanText
                color: Theme.textSecondary
                font: meanText.font
                TextMetrics {
                    id: shortMetrics
                    text: root.scale.meanText
                    font: meanText.font
                }
            }
            Text {
                id: maxText
                anchors.right: parent.right
                text: root.scale.maxText
                color: Theme.text
                font: AppFont.figures(root.compact ? 10 : 12, 600)
            }
        }
    }
}
