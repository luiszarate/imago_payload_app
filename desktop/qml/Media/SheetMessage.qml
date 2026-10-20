import QtQuick
import QtQuick.Controls
import Imago

// What the sheet says when it has no files to show: loading, empty, failed, not supported.
Column {
    id: root

    property string icon
    property string title
    property string body
    property string action
    property bool busy: false
    signal triggered()

    width: Math.min(420, parent ? parent.width - 48 : 420)
    spacing: 10

    BusyIndicator {
        visible: root.busy
        running: visible
        anchors.horizontalCenter: parent.horizontalCenter
        implicitWidth: 36
        implicitHeight: 36
    }
    Icon {
        visible: !root.busy && root.icon.length > 0
        anchors.horizontalCenter: parent.horizontalCenter
        name: root.icon
        size: 34
        color: Theme.textTertiary
    }
    Text {
        width: parent.width
        text: root.title
        color: Theme.text
        font: AppFont.display(16, 600)
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
    }
    Text {
        visible: root.body.length > 0
        width: parent.width
        text: root.body
        color: Theme.textSecondary
        font: AppFont.body(13, 500)
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        lineHeight: 1.2
    }
    Item { width: 1; height: 4; visible: root.action.length > 0 }
    MediaButton {
        visible: root.action.length > 0
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.action
        glyph: "refresh"
        onClicked: root.triggered()
    }
}
