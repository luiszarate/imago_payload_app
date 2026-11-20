import QtQuick
import QtQuick.Controls
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

        // The Herelink's radio link, coloured by the level the core works out (amber weak, red
        // critical or lost). The SNR alone when the bar runs out of room: it is what anticipates a
        // cut (it fell to about 0 dB before each loss of the 2026-11-17 flight).
        StatusChip {
            id: radioChip
            readonly property var radio: App.radio
            readonly property string reading: radio.valid ? radio.rssi + " dBm · SNR " + radio.snr : "No radio"
            // The room the spacer and this chip share does not depend on which text it shows.
            readonly property bool fits: spacer.width + width >= fullRadioText.chipWidth
            visible: App.isConnected && radio.level !== Payload.RadioUnknown
            icon: radio.level === Payload.RadioLost ? "portable_wifi_off" : "wifi_tethering"
            text: fits || !radio.valid ? reading : "SNR " + radio.snr
            tint: radio.level === Payload.RadioWeak ? Theme.warning
                  : radio.level === Payload.RadioCritical || radio.level === Payload.RadioLost ? Theme.danger
                  : Theme.text
            Layout.minimumWidth: implicitWidth
            Accessible.role: Accessible.StaticText
            Accessible.name: "Radio link"
            Accessible.description: reading

            TextMetrics {
                id: fullRadioText
                text: radioChip.reading
                font: AppFont.figures(12, 600)
                readonly property real chipWidth: advanceWidth + 36
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

        Item {
            id: spacer
            Layout.fillWidth: true
        }

        // Where the camera points, as the payload works it out. Click for copy, map link, open
        // in Maps and mark; the coordinate gives way to "TGT" when the bar runs out of room.
        StatusChip {
            id: targetChip
            // The room the spacer and this chip share does not depend on which text it shows.
            readonly property bool fits: spacer.width + width >= fullText.chipWidth
            visible: App.isConnected && App.target.text !== undefined
            icon: "location_on"
            text: fits ? (App.target.text || "") : "TGT"
            tint: Theme.text
            border.color: targetMouse.containsMouse ? Theme.stroke : "transparent"
            Layout.minimumWidth: implicitWidth
            Accessible.role: Accessible.ButtonMenu
            Accessible.name: "Target position"
            Accessible.description: App.target.text || ""

            TextMetrics {
                id: fullText
                text: App.target.text || ""
                font: AppFont.figures(12, 600)
                // With the icon, the spacing and the padding, as StatusChip lays them out.
                readonly property real chipWidth: advanceWidth + 36
            }
            MouseArea {
                id: targetMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: targetMenu.popup(targetChip, 0, targetChip.height + 6)
            }
            Menu {
                id: targetMenu
                MenuItem {
                    enabled: false
                    text: "Target · alt " + Math.round(App.target.altitude || 0) + " m"
                    font: AppFont.body(13, 600)
                }
                MenuItem { text: "Copy coordinates"; font: AppFont.body(14, 600); onTriggered: App.copyTarget() }
                MenuItem { text: "Copy map link"; font: AppFont.body(14, 600); onTriggered: App.copyTargetLink() }
                MenuItem { text: "Open in Maps"; font: AppFont.body(14, 600); onTriggered: App.openTargetInMaps() }
                MenuSeparator {}
                MenuItem { text: "Mark point"; font: AppFont.body(14, 600); onTriggered: App.markTarget() }
            }
        }

        // The laser range finder's distance, or "LRF off" / "LRF —", always there while
        // connected: a click opens the Payload panel, where its rate is set (with the stream and
        // the OSD).
        StatusChip {
            id: rangeFinderChip
            readonly property bool off: App.cameraValues["LRF_MODE"] === 3
            visible: App.isConnected
            icon: "gps_fixed"
            text: App.camera.hasLaserRange ? App.camera.laserRangeMeters.toFixed(0) + " m" : (off ? "LRF off" : "LRF —")
            tint: App.camera.hasLaserRange ? Theme.textSecondary : Theme.textTertiary
            border.color: rangeFinderMouse.containsMouse || payloadPanel.opened ? Theme.stroke : "transparent"
            Layout.minimumWidth: implicitWidth
            Accessible.role: Accessible.Button
            Accessible.name: "Range finder and payload settings"
            Accessible.description: text
            Accessible.onPressAction: payloadPanel.open()

            MouseArea {
                id: rangeFinderMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: payloadPanel.open()
            }
            PayloadPanel {
                id: payloadPanel
                objectName: "payloadPanel"
                x: rangeFinderChip.width - width
                y: rangeFinderChip.height + 8
            }
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
        // The payload's photos and videos, in a window of their own (Ctrl+M).
        IconPill {
            icon: "photo_library"
            label: App.isConnected ? "Media (Ctrl+M)" : "Connect to the payload to see its media."
            active: App.media.open
            opacity: App.isConnected || App.media.open ? 1 : 0.45
            onClicked: if (App.isConnected) App.media.open = true
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
