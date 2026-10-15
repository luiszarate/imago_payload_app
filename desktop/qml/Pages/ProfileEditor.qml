import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Imago

// Form for one profile. Mirrors Payload Plus: one field for the payload, one for the video.
FormPage {
    id: root

    property var profile: ({})
    /// The copy being edited; saved only on Save.
    property var draft: Object.assign({}, profile)
    readonly property string problem: App.validateProfile(draft)
    readonly property bool direct: draft.mavlinkMode !== "listen"
    signal cancelled()
    signal saved(var draft)

    function set(key, value) {
        let copy = Object.assign({}, draft)
        copy[key] = value
        draft = copy
    }

    title: draft.name || "Connection"
    leading: [BarButton { text: "Cancel"; onClicked: root.cancelled() }]
    trailing: [BarButton {
        text: "Save"
        bold: true
        enabled: root.problem.length === 0
        onClicked: root.saved(root.draft)
    }]

    FormSection {
        header: "Name"
        TextFieldRow {
            text: root.profile.name
            placeholder: "Name"
            onEdited: text => root.set("name", text)
        }
    }

    FormSection {
        header: "Payload (MAVLink)"
        footer: root.direct ? "The Orus L IP, as entered in Payload Plus. Through Herelink it is usually 192.168.144.x."
                            : "Use when Herelink forwards the payload's MAVLink to this computer. Commands take about twice as long as with the payload IP."
        ChoiceRow {
            title: "Control"
            options: [{ value: "direct", title: "Payload IP" }, { value: "listen", title: "Listen (forwarded)" }]
            current: root.draft.mavlinkMode
            onChosen: value => root.set("mavlinkMode", value)
        }
        RowDivider {}
        TextFieldRow {
            visible: root.direct
            text: root.profile.payloadIP
            label: "Payload IP"
            onEdited: text => root.set("payloadIP", text)
        }
        PortFieldRow {
            visible: root.direct
            title: "MAVLink port"
            value: root.profile.mavlinkPort
            onEdited: value => root.set("mavlinkPort", value)
        }
        PortFieldRow {
            visible: !root.direct
            title: "Listen port (0 = 14550)"
            value: root.profile.localPort
            onEdited: value => root.set("localPort", value)
        }
    }

    FormSection {
        header: "Video (RTSP)"
        footer: "Leave empty to use the stream the payload announces. Auto tries UDP first and falls back to TCP. If the picture stays behind live for 2 s, for example after the link degrades, the stream restarts to catch up. Smoothing shows frames that arrive in bursts at their own pace, at the cost of up to that much delay; Off shows every frame at once. Frame gaps in the network HUD shows how bursty the link is."
        TextFieldRow {
            text: root.profile.videoURL
            label: "Video URL, e.g. rtsp://192.168.43.1:8554/fpv_stream"
            onEdited: text => root.set("videoURL", text)
        }
        ChoiceRow {
            title: "Transport"
            options: [{ value: "auto", title: "Auto" }, { value: "udp", title: "UDP" }, { value: "tcp", title: "TCP" }]
            current: root.draft.videoTransport
            onChosen: value => root.set("videoTransport", value)
        }
        RowDivider {}
        ChoiceRow {
            title: "Smoothing"
            options: App.smoothingChoices
            current: root.draft.videoSmoothingMs
            onChosen: value => root.set("videoSmoothingMs", value)
        }
        RowDivider {}
        ChoiceRow {
            title: "Resync when behind"
            options: App.videoLagChoices
            current: root.draft.maxVideoLagMs
            onChosen: value => root.set("maxVideoLagMs", value)
        }
    }

    FormSection {
        header: "Advanced"
        footer: "Autopilot gimbal manager: the gimbal is driven through PX4 (MNT_MODE_IN and MNT_MODE_OUT set to MAVLink gimbal v2), which needs the autopilot on this link, e.g. Herelink forwarding. The app takes control once when it connects (not from a ground station that holds it) and gives it back when it disconnects. Keep Wi-Fi awake (experimental) sends a tiny packet 30 times a second so the Wi-Fi adapter does not doze between video packets; turn it off if the video stalls."
        ChoiceRow {
            title: "Gimbal commands"
            options: [{ value: "attitude", title: "Gimbal v2 (direct)" },
                      { value: "mountControl", title: "Mount control" },
                      { value: "manager", title: "Autopilot gimbal manager" }]
            current: root.draft.gimbalProtocol
            onChosen: value => root.set("gimbalProtocol", value)
        }
        RowDivider {}
        SwitchRow {
            title: "Set RC mode to standard"
            checked: root.draft.setStandardRCMode
            onToggled: on => root.set("setStandardRCMode", on)
        }
        RowDivider {}
        SwitchRow {
            title: "Keep Wi-Fi awake (experimental)"
            checked: root.draft.keepWifiAwake
            onToggled: on => root.set("keepWifiAwake", on)
        }
    }

    FormSection {
        visible: root.problem.length > 0
        FormRow {
            Icon { name: "warning"; size: 18; color: Theme.warning }
            Text {
                Layout.fillWidth: true
                text: root.problem
                color: Theme.warning
                font: AppFont.body(14, 600)
                wrapMode: Text.WordWrap
            }
        }
    }
}
