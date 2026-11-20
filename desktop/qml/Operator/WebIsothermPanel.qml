import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Imago

// The isotherm of the payload's web settings page (our Orus L has it only there): one threshold,
// everything warmer is coloured. On/off and the presets go at once; a threshold of one's own goes
// with Apply. Until the page has answered it says why nothing can be set.
Popup {
    id: root

    readonly property var web: App.webIsotherm
    readonly property bool read: web.enabled !== undefined
    property int threshold: 35

    width: 340
    padding: 16
    margins: 8
    // Modal without dimming, so the click that closes it does not reach the video (tracking).
    modal: true
    dim: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onAboutToShow: threshold = read ? web.threshold : 35
    // The page's threshold once it answers, unless one of one's own is being set.
    onReadChanged: if (read) threshold = web.threshold
    Connections {
        target: App
        function onThermalChanged() {
            if (!App.showsThermalBar || App.isothermSupported) root.close()
        }
    }

    background: Rectangle {
        color: Theme.surface
        radius: 16
        border.color: Theme.stroke
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
                enabled: root.read
                checked: root.web.enabled === true
                Accessible.name: "Isotherm"
                onToggled: App.setWebIsotherm(checked, -1)
            }
        }
        Text {
            width: parent.width
            text: root.read ? "The payload's own isotherm: everything warmer than the threshold is coloured. Saved on its web page; it shows at once."
                  : App.webIsothermFailed ? "The payload's web page does not answer (the payload IP in the profile, port 8000). The app keeps trying."
                  : "Reading the payload's web page…"
            color: !root.read && App.webIsothermFailed ? Theme.warning : Theme.textSecondary
            font: AppFont.body(12, 500)
            wrapMode: Text.WordWrap
        }

        Row {
            id: presets
            width: parent.width
            spacing: 8
            Repeater {
                model: App.webIsothermPresets
                Rectangle {
                    id: preset
                    required property var modelData
                    readonly property bool lit: root.web.enabled === true && root.web.threshold === modelData.threshold
                    width: (presets.width - presets.spacing * (App.webIsothermPresets.length - 1)) / App.webIsothermPresets.length
                    height: 46
                    radius: 11
                    color: Theme.surfaceRaised
                    gradient: lit ? Theme.accent : null
                    border.color: presetMouse.containsMouse && !lit && enabled ? Theme.stroke : "transparent"
                    enabled: root.read
                    scale: presetMouse.pressed ? 0.94 : 1
                    Accessible.role: Accessible.Button
                    Accessible.name: modelData.name + ", " + modelData.threshold + " degrees"
                    Accessible.onPressAction: if (enabled) choose()
                    function choose() {
                        root.threshold = modelData.threshold
                        App.setWebIsotherm(true, root.threshold)
                    }
                    Column {
                        anchors.centerIn: parent
                        spacing: 2
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: preset.modelData.name
                            color: root.read ? Theme.text : Theme.textTertiary
                            font: AppFont.display(12, 700)
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: preset.modelData.threshold + " °C"
                            color: preset.lit ? Theme.text : (root.read ? Theme.textSecondary : Theme.textTertiary)
                            font: AppFont.figures(10, 500)
                        }
                    }
                    MouseArea {
                        id: presetMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: preset.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: preset.choose()
                    }
                }
            }
        }

        RowLayout {
            width: parent.width
            Text {
                Layout.fillWidth: true
                text: "Threshold"
                color: Theme.textSecondary
                font: AppFont.body(14, 600)
            }
            // Nothing to set until the page has answered.
            Text {
                visible: !root.read
                text: "— °C"
                color: Theme.textTertiary
                font: AppFont.figures(16, 600)
            }
            SpinBox {
                visible: root.read
                from: App.webThresholdMinC
                to: App.webThresholdMaxC
                editable: true
                value: root.threshold
                textFromValue: (value, locale) => value + " °C"
                valueFromText: (text, locale) => parseInt(text) || 0
                onValueModified: root.threshold = value
                Accessible.name: "Threshold"
            }
        }
        Text {
            visible: root.threshold > App.webLowRangeMaxC
            width: parent.width
            text: "Over " + App.webLowRangeMaxC + " °C the camera measures −20 to 550 °C."
            color: Theme.textSecondary
            font: AppFont.body(11, 500)
            wrapMode: Text.WordWrap
        }

        Rectangle {
            width: parent.width
            height: 46
            radius: 12
            gradient: Theme.accent
            enabled: root.read
            opacity: enabled ? 1 : 0.4
            scale: applyMouse.pressed ? 0.97 : 1
            Accessible.role: Accessible.Button
            Accessible.name: applyText.text
            Text {
                id: applyText
                anchors.centerIn: parent
                text: (root.web.enabled === true ? "Apply " : "Turn on ") + root.threshold + " °C"
                color: Theme.text
                font: AppFont.display(14, 700)
            }
            MouseArea {
                id: applyMouse
                anchors.fill: parent
                cursorShape: parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: App.setWebIsotherm(true, root.threshold)
            }
        }
    }
}
