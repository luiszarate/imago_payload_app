import QtQuick
import QtQuick.Layouts
import Imago

// One row inside a section. Clickable when it has a click handler.
Item {
    id: root

    default property alias content: layout.data
    property bool clickable: false
    signal clicked()

    width: parent ? parent.width : 0
    implicitHeight: Math.max(48, layout.implicitHeight + 12)

    Rectangle {
        anchors.fill: parent
        radius: 12
        color: Theme.alpha(Theme.text, 0.04)
        visible: root.clickable && mouse.containsMouse
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        enabled: root.clickable
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
    RowLayout {
        id: layout
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 8
    }
}
