import QtQuick
import Imago

// Left rail: capture mode, shutter, view source and zoom. In a short window the zoom goes in a
// second column, since one column would be taller than the window.
Panel {
    id: root

    property bool compact: false
    readonly property int pad: compact ? 6 : 8

    implicitWidth: layout.implicitWidth + pad * 2
    implicitHeight: layout.implicitHeight + pad * 2

    Grid {
        id: layout
        anchors.centerIn: parent
        columns: root.compact ? 2 : 1
        columnSpacing: 6
        rowSpacing: root.compact ? 6 : 10
        horizontalItemAlignment: Grid.AlignHCenter
        verticalItemAlignment: Grid.AlignVCenter

        Column {
            spacing: root.compact ? 6 : 10

            // Photo or video
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: modes.implicitWidth + 4
                height: modes.implicitHeight + 4
                radius: 12
                color: Theme.surfaceRaised

                Row {
                    id: modes
                    anchors.centerIn: parent
                    spacing: 2

                    Repeater {
                        model: [{ mode: Payload.PhotoMode, icon: "photo_camera", label: "Photo mode" },
                                { mode: Payload.VideoMode, icon: "videocam", label: "Video mode" }]
                        Rectangle {
                            required property var modelData
                            readonly property bool selected: App.captureMode === modelData.mode
                            width: 26
                            height: root.compact ? 26 : 30
                            radius: 10
                            color: "transparent"
                            gradient: selected ? Theme.accent : null
                            Accessible.role: Accessible.RadioButton
                            Accessible.name: modelData.label
                            Accessible.checked: selected

                            Icon {
                                anchors.centerIn: parent
                                name: parent.modelData.icon
                                size: 15
                                color: parent.selected ? Theme.text : Theme.textSecondary
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: App.chooseCaptureMode(parent.modelData.mode)
                            }
                        }
                    }
                }
            }

            // Shutter: photo, or start / stop recording.
            Item {
                id: shutter
                readonly property real size: root.compact ? 58 : 68
                readonly property bool video: App.captureMode === Payload.VideoMode
                readonly property bool recording: App.camera.recording
                anchors.horizontalCenter: parent.horizontalCenter
                width: size
                height: size
                scale: shutterMouse.pressed ? 0.94 : 1
                Accessible.role: Accessible.Button
                Accessible.name: video ? (recording ? "Stop recording" : "Start recording") : "Take photo"
                Accessible.onPressAction: App.shutter()

                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: "transparent"
                    border.color: Theme.text
                    border.width: 3
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: shutter.size * (shutter.video ? (shutter.recording ? 0.38 : 0.72) : 0.76)
                    height: width
                    radius: shutter.video && shutter.recording ? 6 : width / 2
                    color: shutter.video ? Theme.danger : Theme.text
                    Behavior on width { NumberAnimation { duration: 150 } }
                    Behavior on radius { NumberAnimation { duration: 150 } }
                }
                MouseArea {
                    id: shutterMouse
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: App.shutter()
                }
            }

            RailButton {
                anchors.horizontalCenter: parent.horizontalCenter
                height: root.compact ? 46 : 52
                icon: "layers"
                label: App.camera.viewTitle || "View"
                onClicked: viewMenu.popup(this, 0, height + 4)

                CheckMenu {
                    id: viewMenu
                    options: App.viewSources
                    current: App.camera.view
                    onChosen: value => App.setView(value)
                }
            }
        }

        ZoomControl {
            compact: root.compact
        }
    }
}
