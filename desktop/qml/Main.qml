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

    // While the stream restarts to catch up, the last picture stays instead of the panel.
    ConnectPanel {
        anchors.centerIn: parent
        visible: !App.isConnected || (!App.videoActive && !App.videoResyncing)
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
            anchors.bottomMargin: window.compact ? 0 : 6
        }

        // While the view is IR or IR + EO, bottom right: from the attitude indicator (or the
        // camera rail when the indicator is hidden) to the window's edge, or to the gimbal rail
        // when the rail reaches down beside it (a short window).
        ThermalBar {
            id: thermalBar
            readonly property bool besideRail: gimbalRail.y + gimbalRail.height > parent.height - height - 8
            visible: App.showsThermalBar
            compact: window.compact
            anchors.left: attitude.visible ? attitude.right : cameraRail.right
            anchors.right: besideRail ? gimbalRail.left : parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 10
            anchors.rightMargin: besideRail ? 10 : 0
            anchors.bottomMargin: window.compact ? 0 : 4
        }
    }

    VideoLagBadge {
        visible: App.isConnected && (App.videoLagMs >= 250 || App.videoResyncing)
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: window.compact ? 46 : 58
    }

    HUDView {
        visible: App.showHud && App.isConnected
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: window.compact ? 50 : 66
        anchors.rightMargin: window.compact ? 160 : 180
    }

    // Above whatever occupies the bottom centre: the attitude indicator, or the thermal bar when
    // the indicator is hidden.
    ToastView {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: !App.isConnected ? 18
                              : App.showAttitude ? attitude.height + (window.compact ? 16 : 24)
                              : thermalBar.visible ? thermalBar.height + (window.compact ? 6 : 10) + 10 : 18
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
