import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Imago

// Isotherm settings: the band to colour, presets, the IR gain and on/off.
Popup {
    id: root

    property int low: 35
    property int high: 40

    width: 330
    padding: 16
    margins: 8
    // Modal without dimming, so the click that closes it does not reach the video (tracking).
    modal: true
    dim: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onAboutToShow: {
        low = App.isothermLow
        high = App.isothermHigh
    }
    Connections {
        target: App
        function onThermalChanged() {
            if (!App.showsThermalBar || !App.isothermSupported) root.close()
        }
    }

    background: Rectangle {
        color: Theme.surface
        radius: 16
        border.color: Theme.stroke
    }

    // − / + with autorepeat while held.
    component Stepper: AbstractButton {
        id: stepper
        property string glyph
        implicitWidth: 44
        implicitHeight: 40
        autoRepeat: true
        scale: pressed ? 0.94 : 1
        Accessible.role: Accessible.Button
        background: Rectangle {
            radius: 10
            color: Theme.surfaceRaised
            border.color: stepper.hovered && stepper.enabled ? Theme.stroke : "transparent"
        }
        contentItem: Icon {
            name: stepper.glyph
            size: 18
            color: stepper.enabled ? Theme.text : Theme.textTertiary
        }
    }

    component TemperatureRow: RowLayout {
        id: row
        property string title
        property int value
        property int minimum
        property int maximum
        signal stepped(int value)

        width: parent.width
        spacing: 6
        Text {
            Layout.fillWidth: true
            text: row.title
            color: Theme.textSecondary
            font: AppFont.body(14, 600)
        }
        Stepper {
            glyph: "remove"
            Accessible.name: row.title + " lower"
            enabled: row.value > row.minimum
            onClicked: row.stepped(Math.max(row.minimum, row.value - 1))
        }
        Text {
            Layout.preferredWidth: 70
            horizontalAlignment: Text.AlignHCenter
            text: row.value + " °C"
            color: Theme.text
            font: AppFont.figures(16, 600)
        }
        Stepper {
            glyph: "add"
            Accessible.name: row.title + " higher"
            enabled: row.value < row.maximum
            onClicked: row.stepped(Math.min(row.maximum, row.value + 1))
        }
    }

    contentItem: Column {
        spacing: 14

        RowLayout {
            width: parent.width
            Text {
                Layout.fillWidth: true
                text: "ISOTHERM"
                color: Theme.text
                font: AppFont.display(13, 700, 1.2)
            }
            Switch {
                checked: App.isothermOn
                Accessible.name: "Isotherm"
                onToggled: checked ? App.applyIsotherm(root.low, root.high) : App.disableIsotherm()
            }
        }
        Text {
            width: parent.width
            text: "Colours everything between the two temperatures from black to red, over the palette."
            color: Theme.textSecondary
            font: AppFont.body(12, 500)
            wrapMode: Text.WordWrap
        }

        Row {
            id: presets
            width: parent.width
            spacing: 8
            Repeater {
                model: App.isothermPresets
                Rectangle {
                    id: preset
                    required property var modelData
                    width: (presets.width - presets.spacing * 2) / 3
                    height: 46
                    radius: 11
                    color: Theme.surfaceRaised
                    border.color: presetMouse.containsMouse ? Theme.stroke : "transparent"
                    scale: presetMouse.pressed ? 0.94 : 1
                    Accessible.role: Accessible.Button
                    Accessible.name: modelData.name + ", " + modelData.low + " to " + modelData.high + " degrees"
                    Column {
                        anchors.centerIn: parent
                        spacing: 2
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: preset.modelData.name
                            color: Theme.text
                            font: AppFont.display(12, 700)
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: preset.modelData.low + "–" + preset.modelData.high + " °C"
                            color: Theme.textSecondary
                            font: AppFont.figures(10, 500)
                        }
                    }
                    MouseArea {
                        id: presetMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.low = preset.modelData.low
                            root.high = preset.modelData.high
                            App.applyIsotherm(root.low, root.high)
                        }
                    }
                }
            }
        }

        TemperatureRow {
            title: "From"
            value: root.low
            minimum: App.isothermMinC
            maximum: root.high - 1
            onStepped: value => root.low = App.clampIsothermLow(value, root.high)
        }
        TemperatureRow {
            title: "To"
            value: root.high
            minimum: root.low + 1
            maximum: App.isothermMaxC
            onStepped: value => root.high = App.clampIsothermHigh(value, root.low)
        }

        // The gain, when the camera has the setting.
        Column {
            visible: App.irLowGain >= 0
            width: parent.width
            spacing: 6
            Text {
                text: "GAIN"
                color: Theme.textTertiary
                font: AppFont.display(10, 700, 1)
            }
            Rectangle {
                width: parent.width
                height: 34
                radius: 10
                color: Theme.surfaceRaised
                Row {
                    anchors.fill: parent
                    anchors.margins: 2
                    Repeater {
                        model: [{ low: false, title: "High · to " + App.highGainMaxC + " °C" },
                                { low: true, title: "Low · to " + App.isothermMaxC + " °C" }]
                        Rectangle {
                            required property var modelData
                            readonly property bool selected: (App.irLowGain === 1) === modelData.low
                            width: parent.width / 2
                            height: parent.height
                            radius: 8
                            color: "transparent"
                            gradient: selected ? Theme.accent : null
                            Accessible.role: Accessible.RadioButton
                            Accessible.name: modelData.title
                            Accessible.checked: selected
                            Text {
                                anchors.centerIn: parent
                                text: parent.modelData.title
                                color: parent.selected ? Theme.text : Theme.textSecondary
                                font: AppFont.body(12, parent.selected ? 700 : 600)
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: if (!parent.selected) App.setIRLowGain(parent.modelData.low)
                            }
                        }
                    }
                }
            }
        }
        Text {
            visible: root.high > App.highGainMaxC && App.irLowGain !== 1
            width: parent.width
            text: "Above " + App.highGainMaxC + " °C the camera switches to low gain."
            color: Theme.warning
            font: AppFont.body(11, 500)
            wrapMode: Text.WordWrap
        }

        Rectangle {
            width: parent.width
            height: 46
            radius: 12
            gradient: Theme.accent
            scale: applyMouse.pressed ? 0.97 : 1
            Accessible.role: Accessible.Button
            Accessible.name: applyText.text
            Text {
                id: applyText
                anchors.centerIn: parent
                text: (App.isothermOn ? "Apply " : "Turn on ") + root.low + "–" + root.high + " °C"
                color: Theme.text
                font: AppFont.display(14, 700)
            }
            MouseArea {
                id: applyMouse
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: App.applyIsotherm(root.low, root.high)
            }
        }
    }
}
