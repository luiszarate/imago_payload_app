import QtQuick
import Imago

// Square icon button for the side rails. Large targets, for gloves and for touch screens.
Item {
    id: root

    property string icon
    property string label
    property bool active: false
    property color tint: Theme.text
    signal clicked()

    implicitWidth: 56
    implicitHeight: 52
    scale: mouse.pressed ? 0.94 : 1
    Behavior on scale { NumberAnimation { duration: 80 } }

    Accessible.role: Accessible.Button
    Accessible.name: label
    Accessible.onPressAction: clicked()

    Rectangle {
        anchors.fill: parent
        radius: 13
        color: Theme.surfaceRaised
        gradient: root.active ? Theme.accent : null
        border.color: mouse.containsMouse && !root.active ? Theme.stroke : "transparent"
    }

    Column {
        anchors.centerIn: parent
        spacing: 3

        Icon {
            anchors.horizontalCenter: parent.horizontalCenter
            name: root.icon
            color: root.active ? Theme.text : root.tint
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: root.label.length > 0
            width: Math.min(implicitWidth, root.width - 4)
            text: root.label.toUpperCase()
            color: Theme.text
            font: AppFont.display(10, 600)
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
