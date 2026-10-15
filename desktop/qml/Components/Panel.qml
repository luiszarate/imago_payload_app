import QtQuick
import Imago

// Translucent panel used for every control group over the video. It takes every click and
// scroll inside it, so a click between its buttons does not reach the video (where it would
// start tracking).
Rectangle {
    color: Theme.alpha(Theme.surface, 0.78)
    radius: 18
    border.color: Theme.stroke
    border.width: 1

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: wheel => wheel.accepted = true
    }
}
