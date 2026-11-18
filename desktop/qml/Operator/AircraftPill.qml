import QtQuick
import Imago

// The aircraft's own figures when its telemetry reaches the app: altitude above home, ground
// and air speed, battery (amber, then red, as it runs low). One pill above the map; hidden while
// none of them is known.
Rectangle {
    id: root

    visible: App.aircraft.length > 0
    implicitWidth: row.implicitWidth + 24
    implicitHeight: 30
    radius: height / 2
    color: Theme.alpha(Theme.surface, 0.8)
    border.color: Theme.stroke
    Accessible.role: Accessible.StaticText
    Accessible.name: "Aircraft"
    Accessible.description: App.aircraft.map(f => f.caption + " " + f.value).join(", ")

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 12

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: "flight"
            size: 16
            color: Theme.textSecondary
        }
        Repeater {
            model: App.aircraft
            Row {
                required property var modelData
                anchors.verticalCenter: parent.verticalCenter
                spacing: 5
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: parent.modelData.caption
                    color: Theme.textTertiary
                    font: AppFont.display(10, 700, 1)
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: parent.modelData.value
                    color: parent.modelData.level === 2 ? Theme.danger
                           : parent.modelData.level === 1 ? Theme.warning : Theme.text
                    font: AppFont.figures(13, 600)
                }
            }
        }
    }
}
