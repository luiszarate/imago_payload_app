import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Imago

// Camera, thermal, stream and gimbal settings of the payload, plus the controls of this app.
// Payload settings are read when a page opens and written the moment they change.
FormPage {
    id: root

    property int page: 0
    readonly property bool payloadPage: page !== 3
    signal done()

    title: "Settings"
    trailing: [BarButton { text: "Done"; bold: true; onClicked: root.done() }]

    // Asked again when the link comes (back) up; then once more for what stayed silent.
    function read() {
        if (!App.link.up || !payloadPage) return
        App.readSettings(page, false)
        reread.restart()
    }
    onPageChanged: {
        App.learnGamepadInput("")
        read()
    }
    Component.onCompleted: read()
    Component.onDestruction: App.learnGamepadInput("")
    Connections {
        target: App
        function onLinkChanged() { if (App.link.up) root.read() }
    }
    Timer {
        id: reread
        interval: 2000
        onTriggered: App.readSettings(root.page, true)
    }

    header: [
        // Segmented control for the pages.
        Rectangle {
            width: parent.width
            height: 38
            radius: 10
            color: Theme.surfaceRaised

            Row {
                anchors.fill: parent
                anchors.margins: 2
                Repeater {
                    model: App.settingsPages
                    Rectangle {
                        required property string modelData
                        required property int index
                        readonly property bool active: root.page === index
                        width: parent.width / App.settingsPages.length
                        height: parent.height
                        radius: 8
                        color: "transparent"
                        gradient: active ? Theme.accent : null
                        Accessible.role: Accessible.PageTab
                        Accessible.name: modelData
                        Text {
                            anchors.centerIn: parent
                            text: parent.modelData
                            color: parent.active ? Theme.text : Theme.textSecondary
                            font: AppFont.body(13, parent.active ? 700 : 600)
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.page = parent.index
                        }
                    }
                }
            }
        }
    ]

    FormSection {
        visible: root.payloadPage && !App.isConnected
        FormRow {
            Text {
                Layout.fillWidth: true
                text: "Connect to the payload to see and change its settings."
                color: Theme.textSecondary
                font: AppFont.body(14, 500)
                wrapMode: Text.WordWrap
            }
        }
    }

    // Payload settings. Until the payload answers, a setting shows a spinner.
    Repeater {
        model: root.payloadPage && App.isConnected ? App.settingsSections(root.page) : []
        FormSection {
            id: section
            required property var modelData
            header: modelData.title
            footer: modelData.footer
            Repeater {
                model: section.modelData.settings
                Column {
                    id: settingRow
                    required property var modelData
                    required property int index
                    readonly property var value: App.cameraValues[modelData.id]
                    width: parent.width
                    RowDivider { visible: settingRow.index > 0 }
                    SwitchRow {
                        visible: settingRow.modelData.toggle
                        title: settingRow.modelData.title
                        note: settingRow.modelData.note
                        busy: settingRow.value === undefined
                        checked: settingRow.value === settingRow.modelData.on
                        onToggled: on => App.setCameraSetting(settingRow.modelData.id, settingRow.modelData.title,
                                                              on ? settingRow.modelData.on : settingRow.modelData.off)
                    }
                    ChoiceRow {
                        visible: !settingRow.modelData.toggle
                        title: settingRow.modelData.title
                        note: settingRow.modelData.note
                        busy: settingRow.value === undefined
                        options: settingRow.modelData.options.map(o => ({ value: o.value, title: o.label }))
                        current: settingRow.value
                        onChosen: value => App.setCameraSetting(settingRow.modelData.id, settingRow.modelData.title, value)
                    }
                }
            }
        }
    }

    FormSection {
        visible: root.page === 1 && App.isConnected
        header: "Stream"
        footer: "Through Herelink, a lower bitrate loses fewer packets, and lost packets freeze the picture until the next key frame. The payload does not report its setting, so the figure is the bitrate measured now. (Unverified on the Orus L: that camera 0 is the EO stream.)"
        ChoiceRow {
            title: "Video bitrate · now " + (App.metrics.kbps / 1000).toFixed(1) + " Mb/s"
            options: App.bitrates
            onChosen: value => App.setStreamBitrate(value)
        }
    }

    // Gimbal page: how the joystick and controllers drive the gimbal.
    FormSection {
        visible: root.page === 2
        header: "Sticks and on-screen joystick"
        footer: "Speed at full deflection and 1× zoom; it slows down as you zoom in. A finer curve leaves more of the stick's travel for slow, precise moves."
        SliderRow {
            title: "Pan speed"
            valueText: Math.round(App.gimbalTuning.maxYawDps) + " °/s"
            value: App.gimbalTuning.maxYawDps
            from: App.speedMin; to: App.speedMax; stepSize: 1
            onMoved: v => App.setGimbalTuning("maxYawDps", v)
        }
        SliderRow {
            title: "Tilt speed"
            valueText: Math.round(App.gimbalTuning.maxPitchDps) + " °/s"
            value: App.gimbalTuning.maxPitchDps
            from: App.speedMin; to: App.speedMax; stepSize: 1
            onMoved: v => App.setGimbalTuning("maxPitchDps", v)
        }
        SliderRow {
            readonly property real expo: App.gimbalTuning.expo
            title: "Curve"
            valueText: expo < 0.15 ? "Linear" : expo < 0.45 ? "Soft" : expo < 0.75 ? "Medium" : "Fine"
            value: expo
            from: 0; to: 1; stepSize: 0.05
            minLabel: "Linear"; maxLabel: "Fine"
            onMoved: v => App.setGimbalTuning("expo", v)
        }
        SliderRow {
            title: "Dead zone"
            valueText: Math.round(App.gimbalTuning.deadzone * 100) + " %"
            value: App.gimbalTuning.deadzone
            from: 0; to: App.deadzoneMax; stepSize: 0.01
            onMoved: v => App.setGimbalTuning("deadzone", v)
        }
    }
    FormSection {
        visible: root.page === 2
        header: "D-pad and buttons"
        footer: "A D-pad or button is either off or fully on, so it moves at this fixed speed. The gimbal smooths its own start and stop, and the link adds a delay, so it keeps turning briefly after release: a lower speed stops closer to where you let go."
        SliderRow {
            title: "Pan speed"
            valueText: Math.round(App.gimbalTuning.digitalYawDps) + " °/s"
            value: App.gimbalTuning.digitalYawDps
            from: App.speedMin; to: App.speedMax; stepSize: 1
            onMoved: v => App.setGimbalTuning("digitalYawDps", v)
        }
        SliderRow {
            title: "Tilt speed"
            valueText: Math.round(App.gimbalTuning.digitalPitchDps) + " °/s"
            value: App.gimbalTuning.digitalPitchDps
            from: App.speedMin; to: App.speedMax; stepSize: 1
            onMoved: v => App.setGimbalTuning("digitalPitchDps", v)
        }
        RowDivider {}
        FormRow {
            clickable: true
            onClicked: App.resetGimbalTuning()
            Text { text: "Restore defaults"; color: Theme.magenta; font: AppFont.body(15, 600) }
        }
    }

    GamepadSettings {
        visible: root.page === 3
        width: parent.width
    }
}
