import QtQuick
import QtQuick.Layouts
import Imago

// A row that opens a menu of {value, title} options.
FormRow {
    id: root

    property string title
    property string note
    property var options: []
    property var current
    property bool busy: false
    signal chosen(var value)

    clickable: true
    onClicked: menu.popup(valueText, 0, valueText.height + 6)

    function titleOf(value) {
        for (const option of options) {
            if (option.value === value) return option.title
        }
        return "—"
    }

    Column {
        Layout.fillWidth: true
        Row {
            spacing: 8
            Text { text: root.title; color: Theme.text; font: AppFont.body(15, 600) }
            BusyIndicatorSmall { visible: root.busy; anchors.verticalCenter: parent.verticalCenter }
        }
        Text {
            visible: root.note.length > 0
            text: root.note
            color: Theme.textTertiary
            font: AppFont.body(12, 500)
        }
    }
    Text {
        id: valueText
        text: root.titleOf(root.current)
        color: Theme.textSecondary
        font: AppFont.body(15, 600)
    }
    Icon {
        name: "unfold_more"
        size: 18
        color: Theme.textTertiary
    }

    CheckMenu {
        id: menu
        options: root.options
        current: root.current
        onChosen: value => root.chosen(value)
    }
}
