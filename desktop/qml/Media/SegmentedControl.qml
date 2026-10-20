import QtQuick
import Imago

// A quiet segmented control: in the Media window magenta is kept for the selection, the active
// flight and progress, so the chosen segment is only lifted.
Rectangle {
    id: root

    property var options: []
    property int current: 0
    signal chosen(int index)

    implicitWidth: row.implicitWidth + 4
    implicitHeight: 34
    radius: height / 2
    color: Theme.surfaceRaised
    border.color: Theme.stroke

    Row {
        id: row
        anchors.fill: parent
        anchors.margins: 2
        Repeater {
            model: root.options
            Rectangle {
                id: segment
                required property string modelData
                required property int index
                readonly property bool active: root.current === index
                width: label.implicitWidth + 28
                height: parent.height
                radius: height / 2
                color: active ? Theme.alpha(Theme.text, 0.14) : mouse.containsMouse ? Theme.alpha(Theme.text, 0.05)
                                                                                 : "transparent"
                Accessible.role: Accessible.RadioButton
                Accessible.name: modelData
                Accessible.checked: active
                Text {
                    id: label
                    anchors.centerIn: parent
                    text: segment.modelData
                    color: segment.active ? Theme.text : Theme.textSecondary
                    font: AppFont.body(13, segment.active ? 700 : 600)
                }
                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.chosen(segment.index)
                }
            }
        }
    }
}
