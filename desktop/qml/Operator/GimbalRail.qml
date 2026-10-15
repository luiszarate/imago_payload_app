import QtQuick
import Imago

// Right rail: gimbal mode, recenter, tracking tools and the joystick.
Panel {
    id: root

    property bool compact: false
    readonly property int pad: compact ? 6 : 8

    implicitWidth: compact ? 132 : 148
    implicitHeight: column.implicitHeight + pad * 2

    Column {
        id: column
        anchors.centerIn: parent
        width: root.width - root.pad * 2
        spacing: root.compact ? 6 : 10

        // Who drives the gimbal through the autopilot; a click takes or gives back control.
        Rectangle {
            readonly property bool ours: App.gimbal.control === Payload.ControlThisApp
            visible: App.usesGimbalManager
            width: parent.width
            height: 40
            radius: 11
            color: Theme.alpha(ours ? Theme.ok : Theme.warning, ours ? 0.25 : 0.3)
            scale: controlMouse.pressed ? 0.94 : 1
            Accessible.role: Accessible.Button
            Accessible.name: ours ? "Release gimbal control" : "Take gimbal control"
            Accessible.onPressAction: App.toggleGimbalControl()

            Column {
                anchors.centerIn: parent
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: parent.parent.ours ? "IN CONTROL" : "TAKE CONTROL"
                    color: Theme.text
                    font: AppFont.display(11, 700)
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: parent.parent.ours ? "Click to release" : App.gimbal.controlHolder
                    color: Theme.textSecondary
                    font: AppFont.body(10, 500)
                }
            }
            MouseArea {
                id: controlMouse
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: App.toggleGimbalControl()
            }
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: root.compact ? 6 : 8
            RailButton {
                icon: "control_camera"
                label: App.gimbal.modeTitle || "Mode"
                onClicked: modeMenu.popup(this, 0, height + 4)

                CheckMenu {
                    id: modeMenu
                    options: App.gimbalModes
                    current: App.gimbal.mode
                    onChosen: value => App.setGimbalMode(value)
                }
            }
            RailButton {
                icon: "filter_center_focus"
                label: "Center"
                onClicked: App.recenterGimbal()
            }
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: root.compact ? 6 : 8
            RailButton {
                icon: "auto_awesome"
                label: "AI"
                active: App.detectionEnabled
                onClicked: App.toggleDetection()
            }
            RailButton {
                icon: "touch_app"
                label: "Point"
                active: App.pointMode
                onClicked: App.pointMode = !App.pointMode
            }
        }

        Rectangle {
            visible: App.tracking.state !== Payload.TrackingIdle
            width: parent.width
            height: 34
            radius: 11
            color: Theme.alpha(Theme.danger, 0.85)
            scale: stopMouse.pressed ? 0.94 : 1
            Accessible.role: Accessible.Button
            Accessible.name: "Stop tracking"
            Accessible.onPressAction: App.stopTracking()

            Row {
                anchors.centerIn: parent
                spacing: 4
                Icon { name: "cancel"; size: 15; anchors.verticalCenter: parent.verticalCenter }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: App.tracking.state === Payload.TrackingLost ? "Lost · Stop" : "Stop track"
                    color: Theme.text
                    font: AppFont.display(12, 700)
                }
            }
            MouseArea {
                id: stopMouse
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: App.stopTracking()
            }
        }

        Joystick {
            anchors.horizontalCenter: parent.horizontalCenter
            diameter: root.compact ? 112 : 136
            onMoved: (x, y) => App.joystickMoved(x, y)
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 10
            Text {
                text: App.gimbal.valid ? "P " + (App.gimbal.pitch >= 0 ? "+" : "") + App.gimbal.pitch.toFixed(1) + "°" : "P —"
                color: Theme.textSecondary
                font: AppFont.figures(12, 600)
            }
            Text {
                text: App.gimbal.hasYaw ? "Y " + (App.gimbal.yaw >= 0 ? "+" : "") + App.gimbal.yaw.toFixed(1) + "°" : "Y —"
                color: Theme.textSecondary
                font: AppFont.figures(12, 600)
            }
        }
    }
}
