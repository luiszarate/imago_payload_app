import QtQuick
import Imago

// Round icon button of the top bar.
Rectangle {
    id: root

    property string icon
    property string label
    property bool active: false
    signal clicked()

    implicitWidth: 38
    implicitHeight: 34
    radius: height / 2
    color: Theme.alpha(Theme.surface, 0.8)
    gradient: active ? Theme.accent : null
    border.color: mouse.containsMouse ? Theme.alpha(Theme.text, 0.3) : Theme.stroke
    scale: mouse.pressed ? 0.94 : 1

    Accessible.role: Accessible.Button
    Accessible.name: label
    Accessible.onPressAction: clicked()

    Icon {
        anchors.centerIn: parent
        name: root.icon
        size: 18
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    ToolTipText {
        visible: mouse.containsMouse
        text: root.label
    }
}
