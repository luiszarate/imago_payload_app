import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Imago

FormRow {
    id: root

    property string title
    property string note
    property bool checked: false
    property bool busy: false
    signal toggled(bool checked)

    clickable: true
    onClicked: toggled(!checked)

    Column {
        Layout.fillWidth: true
        // Sized by the title alone and stretched to the row: a long note wraps under it instead
        // of pushing the switch out of the row.
        Layout.preferredWidth: titleRow.implicitWidth
        Row {
            id: titleRow
            spacing: 8
            Text { text: root.title; color: Theme.text; font: AppFont.body(15, 600) }
            BusyIndicatorSmall { visible: root.busy; anchors.verticalCenter: parent.verticalCenter }
        }
        Text {
            visible: root.note.length > 0
            width: parent.width
            text: root.note
            color: Theme.textTertiary
            font: AppFont.body(12, 500)
            wrapMode: Text.WordWrap
        }
    }
    Switch {
        checked: root.checked
        onToggled: root.toggled(checked)
    }
}
