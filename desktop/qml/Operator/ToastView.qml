import QtQuick
import Imago

// Temporary messages: command failures, link changes. Small, in the top right corner of the
// picture: at the bottom centre they covered what the operator was looking at (field test over
// the sea, 2026-11). Nothing here takes clicks; they reach the video below.
Item {
    id: root

    property var shown: ({})
    readonly property bool active: App.toast.text !== undefined

    implicitWidth: bubble.width
    implicitHeight: bubble.height

    Connections {
        target: App
        function onToastChanged() {
            // The last message stays drawn while it fades away.
            if (App.toast.text !== undefined) root.shown = App.toast
        }
    }

    Rectangle {
        id: bubble
        width: Math.min(row.implicitWidth + 28, 380)
        height: row.implicitHeight + 16
        radius: 14
        color: Theme.alpha(Theme.surface, 0.9)
        border.color: Theme.stroke
        opacity: root.active ? 1 : 0
        // In from the right edge, towards the gimbal rail it sits beside.
        x: root.active ? 0 : 24
        Behavior on opacity { NumberAnimation { duration: 180 } }
        Behavior on x { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        Accessible.role: Accessible.AlertMessage
        Accessible.name: root.shown.text || ""

        Row {
            id: row
            anchors.centerIn: parent
            spacing: 7
            Icon {
                id: icon
                anchors.verticalCenter: parent.verticalCenter
                name: root.shown.error ? "warning" : "info"
                size: 16
                color: root.shown.error ? Theme.danger : Theme.magenta
            }
            // Two lines at most: a longer message is cut short rather than covering the picture.
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, 380 - 28 - icon.width - row.spacing)
                text: root.shown.text || ""
                color: Theme.text
                font: AppFont.body(13, 600)
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
        }
    }
}
