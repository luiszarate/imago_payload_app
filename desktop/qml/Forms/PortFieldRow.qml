import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Imago

FormRow {
    id: root

    property string title
    property int value
    signal edited(int value)

    Text {
        Layout.fillWidth: true
        text: root.title
        color: Theme.text
        font: AppFont.body(15, 600)
    }
    TextField {
        Layout.preferredWidth: 110
        text: String(root.value)
        font: AppFont.figures(15, 600)
        selectByMouse: true
        validator: IntValidator { bottom: 0; top: 65535 }
        inputMethodHints: Qt.ImhDigitsOnly
        onTextEdited: root.edited(parseInt(text) || 0)
    }
}
