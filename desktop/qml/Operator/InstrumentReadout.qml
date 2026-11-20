import QtQuick
import Imago

// One figure under an instrument: a caption, then its value ("PITCH -12°"). Shared by the
// attitude indicator and the map's card, so the two read as one pair.
Row {
    property string label
    property string value
    property bool warning: false
    property bool compact: false

    spacing: 4
    Text {
        anchors.verticalCenter: parent.verticalCenter
        text: parent.label
        color: Theme.textTertiary
        font: AppFont.display(parent.compact ? 9 : 10, 700, 1)
    }
    Text {
        anchors.verticalCenter: parent.verticalCenter
        text: parent.value
        color: parent.warning ? Theme.warning : Theme.text
        font: AppFont.figures(parent.compact ? 12 : 13, 600)
    }
}
