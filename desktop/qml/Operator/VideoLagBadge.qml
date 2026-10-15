import QtQuick
import Imago

// Says the picture is behind live, so nobody steers by a stale image without knowing.
Rectangle {
    implicitWidth: row.implicitWidth + 24
    implicitHeight: 28
    radius: height / 2
    color: Theme.warning

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 7
        Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: App.videoResyncing ? "sync" : "schedule"
            size: 15
            color: Theme.background
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: App.videoResyncing ? "CATCHING UP WITH LIVE VIDEO" : "VIDEO " + (App.videoLagMs / 1000).toFixed(1) + " s BEHIND"
            color: Theme.background
            font: AppFont.figures(12, 700)
        }
    }
}
