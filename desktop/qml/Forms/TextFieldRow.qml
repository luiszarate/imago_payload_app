import QtQuick
import QtQuick.Controls
import Imago

// A text field filling its row, with a placeholder and an optional label.
Item {
    id: root

    property alias text: field.text
    property string placeholder
    property string label
    signal edited(string text)

    width: parent ? parent.width : 0
    implicitHeight: field.implicitHeight + 12

    TextField {
        id: field
        anchors.fill: parent
        anchors.margins: 6
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        placeholderText: root.label.length > 0 ? root.label : root.placeholder
        font: AppFont.body(15, 600)
        selectByMouse: true
        inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
        onTextEdited: root.edited(text)
    }
}
