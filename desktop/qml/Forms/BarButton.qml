import QtQuick
import QtQuick.Controls
import Imago

// Text button of a page's title bar.
AbstractButton {
    id: root

    property bool bold: false

    implicitWidth: label.implicitWidth + 24
    implicitHeight: 36
    hoverEnabled: true

    contentItem: Text {
        id: label
        text: root.text
        color: root.enabled ? Theme.magenta : Theme.textTertiary
        opacity: root.hovered ? 0.8 : 1
        font: AppFont.body(16, root.bold ? 700 : 600)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Item {}
}
