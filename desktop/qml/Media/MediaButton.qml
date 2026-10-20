import QtQuick
import QtQuick.Controls
import Imago

// Button of the Media window: "accent" for the one action that moves files, "danger" for
// delete, "plain" for the rest; with an icon only it is round and says what it does on hover.
AbstractButton {
    id: root

    property string glyph
    property string kind: "plain"
    property string tip
    readonly property bool iconOnly: text.length === 0

    implicitWidth: iconOnly ? 34 : row.implicitWidth + 26
    implicitHeight: 34
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    opacity: enabled ? 1 : 0.4
    scale: pressed ? 0.96 : 1
    Behavior on scale { NumberAnimation { duration: 80 } }

    Accessible.name: text.length > 0 ? text : tip

    background: Rectangle {
        radius: height / 2
        color: root.kind === "accent" ? "transparent"
               : root.kind === "danger" ? (root.hovered ? Theme.alpha(Theme.danger, 0.16) : "transparent")
               : root.hovered ? Qt.lighter(Theme.surfaceRaised, 1.5) : Theme.surfaceRaised
        gradient: root.kind === "accent" ? Theme.accent : null
        border.width: root.visualFocus ? 2 : 1
        border.color: root.visualFocus ? Theme.text
                      : root.kind === "danger" ? Theme.alpha(Theme.danger, 0.55)
                      : root.kind === "accent" ? "transparent" : Theme.stroke
        opacity: root.kind === "accent" && root.hovered ? 0.88 : 1
    }

    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 6
            Icon {
                visible: root.glyph.length > 0
                anchors.verticalCenter: parent.verticalCenter
                name: root.glyph
                size: 18
                color: root.kind === "danger" ? Theme.danger : Theme.text
            }
            Text {
                visible: !root.iconOnly
                anchors.verticalCenter: parent.verticalCenter
                text: root.text
                color: root.kind === "danger" ? Theme.danger : Theme.text
                font: AppFont.body(13, 700)
            }
        }
    }

    ToolTipText {
        visible: root.tip.length > 0 && root.hovered
        text: root.tip
    }

    HoverHandler { cursorShape: Qt.PointingHandCursor }
}
