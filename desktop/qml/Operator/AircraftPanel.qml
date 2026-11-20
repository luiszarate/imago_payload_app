import QtQuick
import Imago

// The aircraft's own figures in a large window: one small panel left of the map's card, on its
// baseline, one figure per line (as on the iPad). Compact windows keep the pill above the map
// (AircraftPill). Hidden while none of them is known.
Panel {
    id: root

    visible: App.aircraft.length > 0
    implicitWidth: 150
    implicitHeight: figures.implicitHeight + 14
    radius: 12
    Accessible.role: Accessible.StaticText
    Accessible.name: "Aircraft"
    Accessible.description: App.aircraft.map(f => f.caption + " " + f.value).join(", ")

    Column {
        id: figures
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        spacing: 3

        Repeater {
            model: App.aircraft
            Row {
                required property var modelData
                width: figures.width
                spacing: 6
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 26
                    text: parent.modelData.caption
                    color: Theme.textTertiary
                    font: AppFont.display(10, 700, 0.8)
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 32
                    text: parent.modelData.value
                    color: parent.modelData.level === 2 ? Theme.danger
                           : parent.modelData.level === 1 ? Theme.warning : Theme.text
                    font: AppFont.figures(12, 600)
                    elide: Text.ElideRight
                }
            }
        }
    }
}
