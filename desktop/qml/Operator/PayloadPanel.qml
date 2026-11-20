import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Imago

// The payload settings an operator changes in flight, one click from the top bar (the range
// finder's chip): how often the laser range finder measures, the payload's OSD, and the stream
// the Orus L serves itself. Each choice goes to the payload at once, through the same actions as
// Settings. Until 2026-11-19 they were only in Settings, the range finder among eight other
// sections (iOS PayloadPanel).
Popup {
    id: root

    /// The settings this panel shows that the payload reports back (range finder, OSD).
    readonly property var sections: App.settingsSections(0).filter(s => s.id === "rangeFinder" || s.id === "osd")
    readonly property var settings: sections.reduce((all, s) => all.concat(Array.from(s.settings)), [])
    readonly property var settingIds: settings.map(setting => setting.id)
    property int camera: App.streamCameras[0].value
    readonly property var asked: App.streamAsked[String(camera)] || ({})

    width: 360
    padding: 16
    margins: 8
    // Modal without dimming, so the click that closes it does not reach the video (tracking).
    modal: true
    dim: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onAboutToShow: {
        App.readCameraSettings(settingIds, false)
        reread.restart()
    }
    // Once more for what stayed silent.
    Timer {
        id: reread
        interval: 2000
        onTriggered: App.readCameraSettings(root.settingIds, true)
    }
    Connections {
        target: App
        function onConnectionChanged() { if (!App.isConnected) root.close() }
    }

    background: Rectangle {
        color: Theme.surface
        radius: 16
        border.color: Theme.stroke
    }

    component Caption: Text {
        color: Theme.textTertiary
        font: AppFont.display(10, 700, 1)
    }

    // A row of choices, the current one lit; a click sends at once.
    component Choices: Column {
        id: choices
        property string title
        property var options: []   // [{value, title}]
        property var current
        signal chosen(var value)

        width: parent.width
        spacing: 6
        Caption { text: choices.title }
        Row {
            id: buttons
            width: parent.width
            spacing: 6
            Repeater {
                model: choices.options
                Rectangle {
                    id: choice
                    required property var modelData
                    readonly property bool lit: modelData.value === choices.current
                    width: (buttons.width - buttons.spacing * (choices.options.length - 1)) / choices.options.length
                    height: 38
                    radius: 10
                    color: Theme.surfaceRaised
                    gradient: lit ? Theme.accent : null
                    border.color: choiceMouse.containsMouse && !lit && enabled ? Theme.stroke : "transparent"
                    enabled: App.link.up
                    opacity: enabled ? 1 : 0.5
                    scale: choiceMouse.pressed ? 0.94 : 1
                    Accessible.role: Accessible.RadioButton
                    Accessible.name: choices.title + " " + modelData.title
                    Accessible.checked: lit
                    Accessible.onPressAction: if (enabled) choices.chosen(modelData.value)

                    Text {
                        anchors.fill: parent
                        anchors.margins: 4
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        text: choice.modelData.title
                        color: Theme.text
                        font: AppFont.display(12, 600)
                        fontSizeMode: Text.HorizontalFit
                        minimumPixelSize: 9
                    }
                    MouseArea {
                        id: choiceMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: choices.chosen(choice.modelData.value)
                    }
                }
            }
        }
    }

    contentItem: Column {
        spacing: 14

        Text {
            text: "PAYLOAD"
            color: Theme.text
            font: AppFont.display(13, 700, 1.2)
        }

        Repeater {
            model: root.settings
            Choices {
                required property var modelData
                title: modelData.id === "LRF_MODE" ? "LASER RANGE FINDER" : "PAYLOAD OSD"
                options: modelData.options.map(o => ({ value: o.value, title: o.label }))
                current: App.cameraValues[modelData.id]
                onChosen: value => App.setCameraSetting(modelData.id, modelData.title, value)
            }
        }

        Choices {
            title: "THE ORUS L'S OWN STREAM"
            options: App.streamCameras
            current: root.camera
            onChosen: value => root.camera = value
        }
        // The measured rate is filled while the HUD is open; without it, no "now".
        Choices {
            title: "BITRATE, Mb/s" + (App.metrics.kbps > 0 ? " · NOW " + (App.metrics.kbps / 1000).toFixed(1) : "")
            options: App.bitrates.map(b => ({ value: b.value, title: String(b.value / 1000000) }))
            current: root.asked.bitrate
            onChosen: value => App.setStreamBitrate(root.camera, value)
        }
        Choices {
            title: "PICTURE SIZE"
            options: App.streamSizes(root.camera)
            current: root.asked.level
            onChosen: value => App.setStreamResolution(root.camera, value)
        }
        Text {
            width: parent.width
            text: "Stream: for video from the Orus L itself, not a Herelink's. It reports these back to nobody: the lit choice is what was asked in this session."
            color: Theme.textSecondary
            font: AppFont.body(11, 500)
            wrapMode: Text.WordWrap
        }
    }
}
