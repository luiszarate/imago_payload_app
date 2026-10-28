import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
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

    // Camera page: how this app shows the picture, the marked target points and the payload's
    // camera service.
    FormSection {
        visible: root.page === 0
        header: "Display"
        footer: "The payload draws its OSD down the left edge of the picture. With this on, the picture starts right of the camera controls: a little smaller, never covered."
        SwitchRow {
            title: "Keep the picture clear of the camera rail"
            checked: App.videoClearOfCameraRail
            onToggled: on => App.videoClearOfCameraRail = on
        }
    }
    FormSection {
        visible: root.page === 0
        header: "Marked points"
        footer: "Mark the target from its coordinates in the top bar. The list is kept on this computer and exports as CSV or KML."
        FormRow {
            visible: App.markedPoints.length === 0
            Text {
                Layout.fillWidth: true
                text: "No points yet. Click the target's coordinates in the top bar, then Mark point."
                color: Theme.textSecondary
                font: AppFont.body(14, 500)
                wrapMode: Text.WordWrap
            }
        }
        // Newest first.
        Repeater {
            model: App.markedPoints.slice().reverse()
            Column {
                id: pointRow
                required property var modelData
                required property int index
                width: parent.width
                RowDivider { visible: pointRow.index > 0 }
                FormRow {
                    Column {
                        Layout.fillWidth: true
                        spacing: 2
                        RowLayout {
                            width: parent.width
                            Text {
                                Layout.fillWidth: true
                                text: pointRow.modelData.name
                                color: Theme.text
                                font: AppFont.display(14, 600)
                            }
                            Text {
                                text: pointRow.modelData.timeText
                                color: Theme.textSecondary
                                font: AppFont.figures(12, 500)
                            }
                        }
                        TextEdit {
                            readOnly: true
                            selectByMouse: true
                            text: pointRow.modelData.text + " · alt " + Math.round(pointRow.modelData.altitude) + " m"
                            color: Theme.text
                            selectionColor: Theme.magenta
                            font: AppFont.figures(13, 500)
                        }
                    }
                    ToolButton {
                        text: "delete"
                        font.family: AppFont.iconFamily
                        font.pixelSize: 20
                        Material.foreground: Theme.textSecondary
                        Accessible.name: "Delete " + pointRow.modelData.name
                        onClicked: {
                            deletePoint.point = pointRow.modelData
                            deletePoint.open()
                        }
                    }
                }
            }
        }
        RowDivider { visible: App.markedPoints.length > 0 }
        FormRow {
            visible: App.markedPoints.length > 0
            clickable: true
            onClicked: App.exportMarkedPoints()
            Text { text: "Save as KML (maps) and CSV (spreadsheet)"; color: Theme.magenta; font: AppFont.body(15, 600) }
        }
    }
    FormSection {
        visible: root.page === 0 && App.isConnected
        header: "Payload"
        footer: "When tracking stops obeying after a long session. Video, tracking and control drop for a while and come back by themselves."
        FormRow {
            enabled: !App.webBusy
            clickable: true
            onClicked: restartService.open()
            Text {
                text: "Restart camera service…"
                color: parent.enabled ? Theme.danger : Theme.textTertiary
                font: AppFont.body(15, 600)
            }
        }
    }

    // Thermal page: how much the MAVLink log keeps.
    FormSection {
        visible: root.page === 1
        header: "MAVLink log"
        footer: "Off: the log keeps what the payload says and is told, its streams once a second, and stays small. On: every message on the link and the app's video and link measurements, for chasing link problems; it grows quickly. Record a log from the Network HUD."
        SwitchRow {
            title: "Detailed log (link debugging)"
            checked: App.detailedLog
            onToggled: on => App.detailedLog = on
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

    // The isotherm on the payload's web settings page (our Orus L has it only there). Although
    // the page speaks of a restart, enabling it and changing the threshold show at once on saving
    // (field test 2026-10-19), so there is no restart here.
    FormSection {
        id: webIsotherm
        readonly property bool shown: root.page === 1 && App.isConnected
        readonly property bool read: App.webIsotherm.enabled !== undefined
        property bool draftEnabled: false
        property int draftThreshold: 35
        property int draftRange: 0
        readonly property bool changed: read && (draftEnabled !== App.webIsotherm.enabled
                                                 || draftThreshold !== App.webIsotherm.threshold
                                                 || draftRange !== App.webIsotherm.range)
        function reset() {
            if (!read) return
            draftEnabled = App.webIsotherm.enabled
            draftThreshold = App.webIsotherm.threshold
            draftRange = App.webIsotherm.range
        }
        onShownChanged: if (shown) {
            if (read) reset()
            else App.readWebIsotherm()
        }
        Component.onCompleted: if (shown && !read) App.readWebIsotherm()
        // Only when the payload's values change, not when a request starts or ends.
        readonly property string current: JSON.stringify(App.webIsotherm)
        onCurrentChanged: reset()

        visible: shown
        header: "Isotherm (payload web settings)"
        footer: "The payload's own isotherm: one threshold in °C, e.g. 30 for people. Saved on the payload's web page; it shows in the picture right away."
        FormRow {
            visible: !webIsotherm.read
            Text {
                Layout.fillWidth: true
                text: App.webBusy ? "Reading the payload's web page…" : "Not read"
                color: Theme.textSecondary
                font: AppFont.body(15, 500)
            }
            BusyIndicatorSmall { visible: App.webBusy }
            Button {
                visible: !App.webBusy
                text: "Read"
                flat: true
                Material.foreground: Theme.magenta
                onClicked: App.readWebIsotherm()
            }
        }
        SwitchRow {
            visible: webIsotherm.read
            title: "Isotherm"
            checked: webIsotherm.draftEnabled
            onToggled: on => webIsotherm.draftEnabled = on
        }
        RowDivider { visible: webIsotherm.read }
        FormRow {
            visible: webIsotherm.read
            Text { Layout.fillWidth: true; text: "Threshold"; color: Theme.text; font: AppFont.body(15, 600) }
            SpinBox {
                from: 0
                to: 550
                editable: true
                value: webIsotherm.draftThreshold
                textFromValue: (value, locale) => value + " °C"
                valueFromText: (text, locale) => parseInt(text) || 0
                onValueModified: webIsotherm.draftThreshold = value
                Accessible.name: "Threshold"
            }
        }
        RowDivider { visible: webIsotherm.read }
        ChoiceRow {
            visible: webIsotherm.read
            title: "Measurement range"
            options: [{ value: 0, title: "-20 to 150 °C" }, { value: 1, title: "-20 to 550 °C" }]
            current: webIsotherm.draftRange
            onChosen: value => webIsotherm.draftRange = value
        }
        RowDivider { visible: webIsotherm.read }
        FormRow {
            visible: webIsotherm.read
            enabled: !App.webBusy && webIsotherm.changed
            clickable: true
            onClicked: App.saveWebIsotherm(webIsotherm.draftEnabled, webIsotherm.draftThreshold, webIsotherm.draftRange)
            Text {
                Layout.fillWidth: true
                text: "Save to payload"
                color: parent.enabled ? Theme.magenta : Theme.textTertiary
                font: AppFont.body(15, 600)
            }
            BusyIndicatorSmall { visible: App.webBusy }
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
        footer: "Speed at full deflection and 1× zoom. Zoom compensation sets how much it slows down as you zoom in: None keeps the same speed at any zoom, Full keeps the picture moving at the same pace on screen. A finer curve leaves more of the stick's travel for slow, precise moves."
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
            readonly property real compensation: App.gimbalTuning.zoomCompensation
            title: "Zoom compensation"
            // Full-stick pan speed at 10× zoom, as the shaper works it out.
            valueText: Math.round(compensation * 100) + " % · "
                       + Math.round(App.gimbalTuning.maxYawDps * Math.pow(10, -compensation)) + " °/s at 10×"
            value: compensation
            from: 0; to: 1; stepSize: 0.05
            minLabel: "None"; maxLabel: "Full"
            onMoved: v => App.setGimbalTuning("zoomCompensation", v)
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

    Dialog {
        id: deletePoint
        property var point: ({})
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: "Delete " + (point.name || "") + "?"
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: App.deleteMarkedPoint(point.id)
        Component.onCompleted: standardButton(Dialog.Ok).text = "Delete"
    }
    Dialog {
        id: restartService
        parent: Overlay.overlay
        anchors.centerIn: parent
        contentWidth: 380
        modal: true
        title: "Restart the camera service?"
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: App.restartPayloadService()
        Component.onCompleted: standardButton(Dialog.Ok).text = "Restart now"
        Label {
            anchors.left: parent.left
            anchors.right: parent.right
            text: "Video and control drop while it restarts. Better with the aircraft in a safe state."
            wrapMode: Text.WordWrap
        }
    }
}
