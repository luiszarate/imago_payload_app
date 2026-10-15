import QtQuick
import Imago

// A group of rows with an optional header and footer.
Column {
    id: root

    property string header
    property string footer
    default property alias rows: box.data

    width: parent ? parent.width : 0
    topPadding: 10
    spacing: 6

    Text {
        visible: root.header.length > 0
        leftPadding: 14
        text: root.header.toUpperCase()
        color: Theme.textSecondary
        font: AppFont.display(12, 600)
    }
    Rectangle {
        width: parent.width
        height: box.implicitHeight
        radius: 12
        color: Theme.surfaceRaised

        Column {
            id: box
            width: parent.width
        }
    }
    Text {
        visible: root.footer.length > 0
        width: parent.width
        leftPadding: 14
        rightPadding: 14
        text: root.footer
        color: Theme.textSecondary
        font: AppFont.body(12, 500)
        wrapMode: Text.WordWrap
    }
}
