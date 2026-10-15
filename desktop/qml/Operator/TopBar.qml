import QtQuick
import QtQuick.Layouts
import Imago

Item {
    implicitHeight: 36

    RowLayout {
        anchors.fill: parent
        spacing: 10

        Wordmark {
            size: 16
            Layout.rightMargin: 4
        }

        // Link: profile, round trip or what it waits for.
        Rectangle {
            visible: App.isConnected
            implicitWidth: linkRow.implicitWidth + 22
            implicitHeight: 30
            radius: height / 2
            color: Theme.alpha(Theme.surface, 0.8)
            border.color: Theme.stroke

            Row {
                id: linkRow
                anchors.centerIn: parent
                spacing: 7

                Rectangle {
                    width: 8; height: 8; radius: 4
                    anchors.verticalCenter: parent.verticalCenter
                    color: App.link.up ? Theme.ok : Theme.warning
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: App.connectedProfile.name || "Payload"
                    color: Theme.text
                    font: AppFont.display(13, 600)
                    elide: Text.ElideRight
                    width: Math.min(implicitWidth, 220)
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !App.link.up || App.link.roundTripMs > 0
                    text: App.link.up ? App.link.roundTripMs + " ms" : "waiting for payload"
                    color: Theme.textSecondary
                    font: App.link.up ? AppFont.figures(12, 500) : AppFont.body(12, 500)
                }
            }
        }

        // Recording
        Rectangle {
            visible: App.isConnected && App.camera.recording
            implicitWidth: recRow.implicitWidth + 20
            implicitHeight: 28
            radius: height / 2
            color: Theme.alpha(Theme.danger, 0.25)

            Row {
                id: recRow
                anchors.centerIn: parent
                spacing: 6
                Rectangle {
                    width: 8; height: 8; radius: 4
                    color: Theme.danger
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    readonly property int seconds: App.camera.recordingSeconds
                    text: "REC " + String(Math.floor(seconds / 60)).padStart(2, "0") + ":"
                          + String(seconds % 60).padStart(2, "0")
                    color: Theme.text
                    font: AppFont.figures(12, 700)
                }
            }
        }

        Item { Layout.fillWidth: true }

        StatusChip {
            visible: App.isConnected && App.camera.hasLaserRange
            icon: "gps_fixed"
            text: App.camera.laserRangeMeters.toFixed(0) + " m"
        }
        StatusChip {
            visible: App.isConnected && App.camera.hasStorage
            icon: "sd_card"
            text: App.camera.storageAvailableGB.toFixed(0) + " GB"
        }
        StatusChip {
            visible: App.gamepadName.length > 0
            icon: "sports_esports"
            text: App.gamepadName
            tint: Theme.text
        }
        IconPill {
            visible: App.isConnected
            icon: "explore"
            label: "Gimbal attitude"
            active: App.showAttitude
            onClicked: App.toggleAttitude()
        }
        IconPill {
            visible: App.isConnected
            icon: "speed"
            label: "Network HUD"
            active: App.showHud
            onClicked: App.showHud = !App.showHud
        }
        IconPill {
            icon: "settings"
            label: "Settings"
            onClicked: App.showSettings = true
        }
        IconPill {
            icon: "tune"
            label: "Connections"
            onClicked: App.showConnections = true
        }
        IconPill {
            visible: App.isConnected
            icon: "close"
            label: "Disconnect"
            onClicked: App.disconnectFromPayload()
        }
    }
}
