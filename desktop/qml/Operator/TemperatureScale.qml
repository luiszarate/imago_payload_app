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
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.scale.meanText ? "MEAN " + root.scale.meanText : ""
                color: Theme.textSecondary
                font: AppFont.figures(root.compact ? 10 : 12, 600)
            }
            Text {
                anchors.right: parent.right
                text: root.scale.maxText
                color: Theme.text
                font: AppFont.figures(root.compact ? 10 : 12, 600)
            }
        }
    }
}
