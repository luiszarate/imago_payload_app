import QtQuick
import Imago

// Small rounded label for status values in the top bar.
Rectangle {
    id: root

    property string text
    property string icon
    property color tint: Theme.textSecondary

    implicitWidth: row.implicitWidth + 18
    implicitHeight: 24
    radius: height / 2
    color: Theme.alpha(Theme.surfaceRaised, 0.9)

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 5

        Icon {
            visible: root.icon.length > 0
            name: root.icon
            size: 13
            color: root.tint
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: root.text
            color: root.tint
            font: AppFont.figures(12, 600)
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
