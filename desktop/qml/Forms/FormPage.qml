import QtQuick
import QtQuick.Controls
import Imago

// A full-window page with a title bar, drawn in the main window (not a separate window), so a
// gamepad keeps reaching the app while one is open.
Rectangle {
    id: root

    property string title
    property alias leading: leadingRow.data
    property alias trailing: trailingRow.data
    property alias header: headerColumn.data
    default property alias content: contentColumn.data

    color: Theme.background

    // Takes every click and scroll that the page itself does not use.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: wheel => wheel.accepted = true
    }

    Item {
        id: bar
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 52

        Row {
            id: leadingRow
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
        }
        Text {
            anchors.centerIn: parent
            width: Math.min(implicitWidth, parent.width - 320)
            text: root.title
            color: Theme.text
            font: AppFont.display(17, 600)
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
        }
        Row {
            id: trailingRow
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
        }
    }
    Rectangle {
        id: divider
        anchors { left: parent.left; right: parent.right; top: bar.bottom }
        height: 1
        color: Theme.stroke
    }

    Flickable {
        id: flick
        anchors { left: parent.left; right: parent.right; top: divider.bottom; bottom: parent.bottom }
        contentWidth: width
        contentHeight: body.implicitHeight + 24
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ScrollBar {}

        Column {
            id: body
            width: Math.min(720, flick.width - 32)
            x: (flick.width - width) / 2
            y: 12
            spacing: 6

            Column {
                id: headerColumn
                width: parent.width
            }
            Column {
                id: contentColumn
                width: parent.width
                spacing: 6
            }
        }
    }
}
