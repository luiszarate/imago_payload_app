import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Imago

// Controller mapping: pick a function, then press the button or move the axis for it.
Column {
    id: root

    spacing: 6

    component RowButtons: Row {
        id: buttons
        property bool learning: false
        property bool canClear: false
        signal assign()
        signal clear()
        spacing: 8
        Button {
            visible: buttons.canClear && !buttons.learning
            text: "Clear"
            flat: true
            font: AppFont.body(13, 600)
            onClicked: buttons.clear()
        }
        Button {
            text: buttons.learning ? "Cancel" : "Assign"
            enabled: App.gamepadName.length > 0 || buttons.learning
            flat: true
            highlighted: true
            font: AppFont.body(13, 600)
            onClicked: buttons.assign()
        }
    }

    FormSection {
        header: "Controller"
        footer: "Xbox, PlayStation and generic Bluetooth or USB gamepads work, and HOTAS, flight sticks and pedals. Pair it in the system's Bluetooth settings or plug it in by USB. It keeps working while another window has the focus."
        FormRow {
            Icon {
                name: "sports_esports"
                size: 22
                color: App.gamepadName.length > 0 ? Theme.ok : Theme.textTertiary
            }
            Text {
                Layout.fillWidth: true
                text: App.gamepadName || "No controller connected"
                color: Theme.text
                font: AppFont.body(15, 600)
            }
        }
    }

    FormSection {
        header: "Axes"
        footer: "Assign, then move the stick, hat, throttle or trigger fully in the direction that should mean right, up or zoom in."
        Repeater {
            model: App.gamepadAxes
            Column {
                id: axis
                required property var modelData
                required property int index
                readonly property bool learning: App.gamepadLearning === "axis:" + modelData.key
                width: parent.width
                RowDivider { visible: axis.index > 0 }
                FormRow {
                    Column {
                        Layout.fillWidth: true
                        Text { text: axis.modelData.title; color: Theme.text; font: AppFont.body(15, 600) }
                        Text {
                            text: axis.learning ? "Move the axis now…" : (axis.modelData.bound ? axis.modelData.label : "Not assigned")
                            color: axis.learning ? Theme.magenta : Theme.textSecondary
                            font: AppFont.body(12, 500)
                        }
                    }
                    RowButtons {
                        learning: axis.learning
                        canClear: axis.modelData.bound
                        onAssign: App.learnGamepadInput(axis.learning ? "" : "axis:" + axis.modelData.key)
                        onClear: App.clearGamepadAxis(axis.modelData.key)
                    }
                }
                SwitchRow {
                    visible: axis.modelData.bound
                    title: "Invert"
                    checked: axis.modelData.inverted
                    onToggled: on => App.setGamepadAxisInverted(axis.modelData.key, on)
                }
            }
        }
    }

    FormSection {
        header: "Buttons"
        footer: "Assign, then press the button. A button does one thing: assigning it again moves it."
        Repeater {
            model: App.gamepadButtons
            Column {
                id: action
                required property var modelData
                required property int index
                readonly property bool learning: App.gamepadLearning === "action:" + modelData.key
                width: parent.width
                RowDivider { visible: action.index > 0 }
                FormRow {
                    Column {
                        Layout.fillWidth: true
                        Text { text: action.modelData.title; color: Theme.text; font: AppFont.body(15, 600) }
                        Text {
                            text: action.learning ? "Press a button now…" : (action.modelData.bound ? action.modelData.label : "Not assigned")
                            color: action.learning ? Theme.magenta : Theme.textSecondary
                            font: AppFont.body(12, 500)
                        }
                    }
                    RowButtons {
                        learning: action.learning
                        canClear: action.modelData.bound
                        onAssign: App.learnGamepadInput(action.learning ? "" : "action:" + action.modelData.key)
                        onClear: App.clearGamepadButton(action.modelData.key)
                    }
                }
            }
        }
    }

    FormSection {
        FormRow {
            clickable: true
            onClicked: App.resetGamepadMapping()
            Text { text: "Restore default mapping"; color: Theme.danger; font: AppFont.body(15, 600) }
        }
    }
}
