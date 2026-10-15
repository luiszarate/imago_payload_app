import QtQuick
import Imago

// Temporary messages: command failures, link changes.
Item {
    id: root

    property var shown: ({})
    readonly property bool active: App.toast.text !== undefined

    implicitWidth: bubble.width
    implicitHeight: bubble.height

    Connections {
        target: App
        function onToastChanged() {
            // The last message stays drawn while it slides away.
            if (App.toast.text !== undefined) root.shown = App.toast
        }
    }

    Rectangle {
        id: bubble
        width: Math.min(row.implicitWidth + 32, 560)
        height: row.implicitHeight + 20
        radius: 14
        color: Theme.alpha(Theme.surface, 0.9)
        border.color: Theme.stroke
        opacity: root.active ? 1 : 0
        y: root.active ? 0 : 24
        Behavior on opacity { NumberAnimation { duration: 180 } }
        Behavior on y { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        Accessible.role: Accessible.AlertMessage
        Accessible.name: root.shown.text || ""

        Row {
            id: row
            anchors.centerIn: parent
            spacing: 8
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: root.shown.error ? "warning" : "info"
                size: 18
                color: root.shown.error ? Theme.danger : Theme.magenta
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, 500)
                text: root.shown.text || ""
                color: Theme.text
                font: AppFont.body(14, 600)
                wrapMode: Text.WordWrap
            }
        }
    }
}
