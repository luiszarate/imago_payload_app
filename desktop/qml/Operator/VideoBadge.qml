import QtQuick
import Imago

// Says the picture is not live, so nobody steers by a stale image without knowing. One warning
// at a time, the most important first; hidden while the picture is live.
Rectangle {
    id: root

    // "catchingUp", "reconnecting", "frozen", "behind", or empty. Before the first picture the
    // connect panel says what the video is doing, so a stream that is not up yet has no badge.
    readonly property string kind: !App.isConnected ? ""
                                   : App.videoResyncing ? "catchingUp"
                                   : !App.videoActive ? (App.videoHadPicture ? "reconnecting" : "")
                                   : App.videoFrozenMs > 0 ? "frozen"
                                   : App.videoLagMs >= 250 ? "behind"
                                   : ""

    visible: kind.length > 0
    implicitWidth: row.implicitWidth + 24
    implicitHeight: 28
    radius: height / 2
    color: Theme.warning
    Accessible.role: Accessible.AlertMessage
    Accessible.name: label.text

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 7
        Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: root.kind === "catchingUp" ? "sync"
                  : root.kind === "reconnecting" ? "refresh"
                  : root.kind === "frozen" ? "ac_unit"
                  : "schedule"
            size: 15
            color: Theme.background
        }
        Text {
            id: label
            anchors.verticalCenter: parent.verticalCenter
            text: root.kind === "catchingUp" ? "CATCHING UP WITH LIVE VIDEO"
                  : root.kind === "reconnecting" ? "VIDEO RECONNECTING…"
                  : root.kind === "frozen" ? "VIDEO FROZEN " + (App.videoFrozenMs / 1000).toFixed(1) + " s"
                  : "VIDEO " + (App.videoLagMs / 1000).toFixed(1) + " s BEHIND"
            color: Theme.background
            font: AppFont.figures(12, 700)
        }
    }
}
