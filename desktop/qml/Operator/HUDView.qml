import QtQuick
import QtQuick.Layouts
import Imago

// Network and latency figures, to tell app delays from datalink delays in the field.
// Clicks on the figures reach the video below (click to track); only the log buttons take them.
Rectangle {
    id: root

    readonly property var m: App.metrics

    function fixed(value, digits) { return Number(value).toFixed(digits) }

    width: 290
    implicitHeight: column.implicitHeight + 24
    radius: 14
    color: Theme.alpha(Theme.surface, 0.78)
    border.color: Theme.stroke

    component MetricRow: RowLayout {
        property alias label: labelText.text
        property alias value: valueText.text
        width: parent.width
        spacing: 14
        Text {
            id: labelText
            Layout.fillWidth: true
            color: Theme.textSecondary
            font: AppFont.body(12, 500)
            elide: Text.ElideRight
        }
        Text {
            id: valueText
            color: Theme.text
            font: AppFont.figures(12, 600)
        }
    }
    component Divider: Rectangle {
        width: parent.width
        height: 7
        color: "transparent"
        Rectangle { anchors.centerIn: parent; width: parent.width; height: 1; color: Theme.stroke }
    }

    Column {
        id: column
        anchors.fill: parent
        anchors.margins: 12
        spacing: 3

        RowLayout {
            width: parent.width
            height: 24
            Text {
                text: "LINK"
                color: Theme.magenta
                font: AppFont.display(10, 700, 1.5)
            }
            Item { Layout.fillWidth: true }
            // Record all MAVLink to a .tlog, and find the logs.
            Icon {
                visible: App.lastLogFile.length > 0 && !App.isRecordingLog
                name: "folder_open"
                size: 18
                Accessible.role: Accessible.Button
                Accessible.name: "Show MAVLink logs"
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: App.showLogFolder()
                }
            }
            Rectangle {
                implicitWidth: logRow.implicitWidth + 16
                implicitHeight: 24
                radius: height / 2
                color: Theme.surfaceRaised
                Accessible.role: Accessible.Button
                Accessible.name: App.isRecordingLog ? "Stop MAVLink log" : "Record MAVLink log"
                Row {
                    id: logRow
                    anchors.centerIn: parent
                    spacing: 5
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        anchors.verticalCenter: parent.verticalCenter
                        color: App.isRecordingLog ? Theme.danger : Theme.textTertiary
                    }
                    Text {
                        text: App.isRecordingLog ? "Stop log" : "Record log"
                        color: Theme.text
                        font: AppFont.body(11, 600)
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: App.toggleLogRecording()
                }
            }
        }
        MetricRow { label: "Video"; value: root.m.active ? root.m.codec + " · " + root.m.transport : "off" }
        MetricRow { label: "Resolution"; value: App.videoWidth > 0 ? App.videoWidth + " × " + App.videoHeight : "—" }
        MetricRow { label: "Frame rate"; value: root.fixed(root.m.fps, 1) + " fps" }
        MetricRow { label: "Bitrate"; value: root.fixed(root.m.kbps / 1000, 1) + " Mb/s" }
        MetricRow { label: "Lost / late"; value: root.m.packetsLost + " / " + root.m.packetsLate }
        MetricRow { label: "Jitter"; value: root.fixed(root.m.jitterMs, 1) + " ms" }
        MetricRow { label: "Frame transfer"; value: root.fixed(root.m.transferMs, 1) + " ms" }
        // About one frame period on a smooth link; bursts show as gaps of several.
        MetricRow { label: "Frame gaps p95 / max"; value: root.fixed(root.m.frameGapP95Ms, 0) + " / " + root.fixed(root.m.frameGapMaxMs, 0) + " ms" }
        MetricRow { label: "Behind live"; value: Math.round(root.m.lagMs) + " ms · " + root.m.resyncs + (root.m.resyncs === 1 ? " resync" : " resyncs") }
        Divider {}
        MetricRow { label: "Decoder"; value: root.m.decoder }
        MetricRow { label: "App: assemble"; value: root.fixed(root.m.assemblyMs, 2) + " ms" }
        MetricRow { label: "App: decode"; value: root.fixed(root.m.decodeMs, 1) + " ms" }
        MetricRow { label: "App: on screen"; value: root.fixed(root.m.presentMs, 1) + " ms" }
        // Where frozen pictures come from: lost packets, the decoder, or the display.
        MetricRow { label: "Corrupt / failed / skipped"; value: root.m.framesCorrupt + " / " + root.m.decodeFailures + " / " + root.m.framesSkipped }
        Divider {}
        MetricRow { label: "Command RTT"; value: App.link.roundTripMs + " ms (last " + Math.round(root.m.roundTripLastMs) + ")" }
        MetricRow { label: "MAVLink rx / gaps"; value: root.m.mavlinkReceived + " / " + root.m.mavlinkGaps }
        // Acknowledgements the payload sent to the autopilot or a GCS: they command it too.
        MetricRow { label: "Others' commands"; value: String(App.foreignCommandCount) }
        MetricRow {
            visible: App.info.model.length > 0
            label: "Payload"
            value: App.info.model + " " + App.info.softwareVersion
        }
        // Which value means which thermal core (palette names) is still to be confirmed.
        MetricRow {
            visible: App.thermal.cameraType >= 0
            label: "IR sensor type"
            value: String(App.thermal.cameraType)
        }
    }
}
