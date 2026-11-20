import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtMultimedia
import Imago

// The single operating screen: full-window video with control rails floating over it.
ApplicationWindow {
    id: window

    // Small windows get smaller controls, the zoom beside the capture column.
    readonly property bool compact: height < 560
    // A large window is the desktop's tablet: the instruments in one row at the bottom centre.
    readonly property bool instrumentRow: !compact
    readonly property real bottomMargin: compact ? 0 : 6
    readonly property bool pageOpen: App.showSettings || App.showConnections

    width: 1280
    height: 800
    minimumWidth: 900
    minimumHeight: 480
    visible: true
    title: App.isConnected && App.connectedProfile.name ? App.connectedProfile.name + " — Imago Payload Control"
                                                        : "Imago Payload Control"
    color: Theme.background

    Material.theme: Material.Dark
    Material.accent: Theme.magenta
    Material.primary: Theme.magenta
    Material.background: Theme.surfaceRaised
    Material.foreground: Theme.text
    font: AppFont.body(14, 500)

    // The camera draws its OSD down the left edge of the picture, under the camera rail (field
    // report 2026-10-15). With the setting on, the picture fits right of the rail instead: a
    // little smaller, never covered. Clicks, AI boxes and the reticle follow, because the overlay
    // takes the same inset and the picture's rect inside it.
    readonly property real videoInset: App.isConnected && App.videoClearOfCameraRail
                                       ? controls.anchors.leftMargin + cameraRail.x + cameraRail.width + 8 : 0

    VideoOutput {
        id: video
        anchors.fill: parent
        anchors.leftMargin: window.videoInset
        fillMode: VideoOutput.PreserveAspectFit
        Component.onCompleted: App.setVideoSink(videoSink)
    }

    VideoOverlay {
        anchors.fill: parent
        anchors.leftMargin: window.videoInset
        visible: App.isConnected
        contentRect: video.contentRect
    }

    // Once the video has started, the last picture stays instead of the panel while the stream
    // restarts (to catch up, or after it dropped): the badge at the top says so.
    ConnectPanel {
        anchors.centerIn: parent
        visible: !App.isConnected || (!App.videoActive && !App.videoResyncing && !App.videoHadPicture)
        compact: window.compact
    }

    TopBar {
        anchors { left: parent.left; right: parent.right; top: parent.top }
        anchors.leftMargin: window.compact ? 12 : 16
        anchors.rightMargin: window.compact ? 12 : 16
        anchors.topMargin: window.compact ? 6 : 10
    }

    Item {
        id: controls
        visible: App.isConnected
        anchors.fill: parent
        anchors.leftMargin: window.compact ? 12 : 16
        anchors.rightMargin: window.compact ? 12 : 16
        anchors.topMargin: window.compact ? 44 : 56
        anchors.bottomMargin: 6

        CameraRail {
            id: cameraRail
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            compact: window.compact
        }
        GimbalRail {
            id: gimbalRail
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            compact: window.compact
        }

        // Always at the bottom centre, so nothing moves when the view turns IR.
        AttitudeIndicator {
            id: attitude
            visible: App.showAttitude
            compact: window.compact
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: window.bottomMargin
        }

        // A large window: one row of instruments on one baseline at the bottom centre, 10 px
        // apart: the aircraft's figures | the map | the attitude indicator | the thermal bar, the
        // map's card a twin of the indicator's. Beside the gimbal rail the map looked dropped
        // there at random, and beside the indicator at a size of its own, out of line (iPad,
        // operator, 2026-11-19). With the indicator hidden the map's card takes its place.
        MapCard {
            id: mapCard
            visible: window.instrumentRow
            indicator: attitude
            twinned: attitude.visible
            anchors.bottom: parent.bottom
            anchors.bottomMargin: window.bottomMargin
            // 10 px left of the indicator, or centred in its place.
            x: attitude.visible ? attitude.x - 10 - width : (parent.width - width) / 2
        }
        // Left of the map's card on its baseline; above the card where the camera rail leaves
        // too little room beside it (a narrow window).
        AircraftPanel {
            readonly property real room: mapCard.x - 10 - (cameraRail.x + cameraRail.width + 10)
            readonly property bool beside: room >= 120
            visible: window.instrumentRow && App.aircraft.length > 0
            width: beside ? Math.min(implicitWidth, room) : implicitWidth
            x: beside ? mapCard.x - 10 - width : mapCard.x + mapCard.width - width
            y: beside ? mapCard.y + mapCard.height - height : mapCard.y - 6 - height
        }

        // While the view is IR or IR + EO, bottom right: from the attitude indicator (or, in a
        // large window, the map's card in its place; in a compact one the camera rail) to the
        // window's edge, or to the gimbal rail when the rail reaches down beside it (a short
        // window). On the indicator's baseline.
        ThermalBar {
            id: thermalBar
            readonly property bool besideRail: gimbalRail.y + gimbalRail.height > parent.height - height - 8
            visible: App.showsThermalBar
            compact: window.compact
            anchors.left: attitude.visible ? attitude.right : (window.instrumentRow ? mapCard.right : cameraRail.right)
            anchors.right: besideRail ? gimbalRail.left : parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 10
            anchors.rightMargin: besideRail ? 10 : 0
            anchors.bottomMargin: window.bottomMargin
        }

        // A compact window: bottom right, left of the gimbal rail (and above the thermal bar
        // while it shows): the map, with the aircraft's figures above it. As large as the room
        // beside the attitude indicator allows.
        Column {
            id: mapCorner
            visible: !window.instrumentRow
            readonly property real roomLeft: (attitude.visible ? attitude.x + attitude.width : cameraRail.x + cameraRail.width) + 10
            readonly property real room: gimbalRail.x - 10 - roomLeft
            anchors.right: gimbalRail.left
            anchors.rightMargin: 10
            anchors.bottom: thermalBar.visible ? thermalBar.top : parent.bottom
            anchors.bottomMargin: thermalBar.visible ? 8 : window.bottomMargin
            spacing: 8

            AircraftPill {
                x: parent.width - width
            }
            MapView {
                x: parent.width - width
                size: Math.max(140, Math.min(window.compact ? 190 : 240, mapCorner.room))
            }
        }
    }

    // Top centre, under the top bar: one warning about the picture at a time.
    VideoBadge {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: window.compact ? 46 : 58
    }

    // Top right of the picture, left of the gimbal rail: the Network HUD, then the latest notice
    // under it, so neither covers the other nor the picture's centre. Without the HUD the notice
    // starts below the badge's line: in a window under about 1400 px wide a long one would reach
    // the badge at the top centre.
    Column {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: hud.visible ? (window.compact ? 50 : 66) : (window.compact ? 80 : 92)
        anchors.rightMargin: window.compact ? 160 : 180
        spacing: 6

        HUDView {
            id: hud
            visible: App.showHud && App.isConnected
            anchors.right: parent.right
        }
        ToastView {
            anchors.right: parent.right
        }
    }

    // Pages are drawn in this window (not as separate windows), so a gamepad keeps working and
    // the video keeps playing behind them.
    Loader {
        anchors.fill: parent
        active: App.showConnections
        sourceComponent: ConnectionsPage { onDone: App.showConnections = false }
    }
    Loader {
        anchors.fill: parent
        active: App.showSettings
        sourceComponent: SettingsPage { onDone: App.showSettings = false }
    }

    // Media opens in a window of its own, so the live video stays in sight here.
    Loader {
        id: mediaWindow
        active: App.media.open
        sourceComponent: MediaWindow {}
    }
    Shortcut {
        sequence: "Ctrl+M"
        onActivated: {
            if (!App.media.open) {
                if (App.isConnected) App.media.open = true
            } else if (mediaWindow.item) {
                mediaWindow.item.raise()
                mediaWindow.item.requestActivate()
            }
        }
    }

    Shortcut {
        sequences: [StandardKey.FullScreen, "F11"]
        onActivated: window.visibility = window.visibility === Window.FullScreen ? Window.Windowed
                                                                                 : Window.FullScreen
    }
    Shortcut {
        sequence: "Esc"
        onActivated: {
            if (App.showSettings) App.showSettings = false
            else if (App.showConnections) App.showConnections = false
            else if (window.visibility === Window.FullScreen) window.visibility = Window.Windowed
        }
    }

    onClosing: {
        App.media.open = false
        App.disconnectFromPayload()
    }
}
