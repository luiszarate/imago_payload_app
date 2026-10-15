import QtQuick
import Imago

// Zoom level, hold buttons and a slider between them.
//
// The slider is logarithmic, so every doubling gets the same travel and 1–25× keeps most of
// the length even when the combined range reaches 300×. Its range follows the camera's zoom
// mode; the digital part, above 25×, is marked. Dragging moves the thumb with the pointer (a
// click does not jump), and the thumb shows the level asked for while a marker beside the
// track shows where the lens actually is until it gets there. The mouse wheel over the slider
// steps through the camera's levels.
Column {
    id: root

    property bool compact: false

    readonly property real opticalMax: 25
    readonly property real maxLevel: Math.max(App.camera.zoomMax > 0 ? App.camera.zoomMax : 300, 2)
    readonly property var levels: App.zoomLevels(maxLevel <= opticalMax)
    readonly property real actual: App.camera.zoom > 0 ? App.camera.zoom : 1
    /// Level asked for that the lens has not reached yet; 0 = none.
    property real target: 0
    /// Level when the current drag began; 0 = not dragging.
    property real dragStart: 0
    readonly property real shown: target > 0 ? target : actual
    readonly property real thumb: 24
    /// When the current press of + or − began.
    property double pressedAt: 0

    spacing: compact ? 6 : 8

    function position(level) {
        return Math.max(0, Math.min(1, Math.log(Math.max(level, 1)) / Math.log(maxLevel)))
    }
    function levelAt(p) {
        return Math.exp(p * Math.log(maxLevel))
    }
    /// Nearest on a log scale, as the core snaps it.
    function nearest(level) {
        let best = 0
        for (let i = 1; i < levels.length; ++i) {
            if (Math.abs(Math.log(levels[i]) - Math.log(level)) < Math.abs(Math.log(levels[best]) - Math.log(level)))
                best = i
        }
        return best
    }
    function format(level) {
        return level >= 100 ? level.toFixed(0) + "×" : level.toFixed(1) + "×"
    }
    function send(level) {
        target = level
        giveUp.restart()
        App.zoomTo(level)
    }

    // A tap starts and stops within a few ms, too soon for the lens to move: the stop waits
    // until the zoom has run this long.
    readonly property int minimumPulseMs: 300

    function startZoom(direction) {
        pendingStop.stop()
        pressedAt = Date.now()
        App.zoom(direction)
    }
    function stopZoom() {
        const held = Date.now() - pressedAt
        if (held >= minimumPulseMs) {
            App.zoom(0)
        } else {
            pendingStop.interval = minimumPulseMs - held
            pendingStop.restart()
        }
    }

    Timer {
        id: pendingStop
        onTriggered: App.zoom(0)
    }
    // A lens that never gets there (refused, clamped, lost): follow the camera again.
    Timer {
        id: giveUp
        interval: 5000
        onTriggered: if (root.dragStart === 0) root.target = 0
    }
    Connections {
        target: App
        function onZoomDirectionChanged() {
            if (App.zoomDirection !== 0) root.target = 0 // the buttons or the wheel took over
        }
        function onCameraChanged() {
            const t = root.target
            if (t > 0 && root.dragStart === 0 && Math.abs(App.camera.zoom - t) <= Math.max(0.1, t * 0.03))
                root.target = 0
        }
    }

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        height: 16
        text: App.camera.zoom > 0 || root.target > 0 ? root.format(root.shown) : "—"
        color: root.shown > root.opticalMax + 0.05 ? Theme.warning : Theme.text
        font: AppFont.figures(13, 600)
    }

    HoldButton {
        anchors.horizontalCenter: parent.horizontalCenter
        height: root.compact ? 44 : 52
        icon: "zoom_in"
        description: "Zoom in"
        onPressed: root.startZoom(1)
        onReleased: root.stopZoom()
    }

    Item {
        id: slider
        anchors.horizontalCenter: parent.horizontalCenter
        width: 56
        height: root.compact ? 140 : 190
        Accessible.role: Accessible.Slider
        Accessible.name: "Zoom level"
        Accessible.description: root.format(root.shown)

        readonly property real trackX: width / 2 + 9
        readonly property real travel: height - root.thumb
        property real thumbPosition: root.position(root.shown)
        Behavior on thumbPosition {
            enabled: root.dragStart === 0
            NumberAnimation { duration: 200 }
        }

        Canvas {
            id: track
            anchors.fill: parent
            property real level: root.shown
            property real lens: root.target > 0 && App.camera.zoom > 0 ? root.actual : 0
            onLevelChanged: requestPaint()
            onLensChanged: requestPaint()
            Connections {
                target: root
                function onMaxLevelChanged() { track.requestPaint() }
                function onLevelsChanged() { track.requestPaint() }
            }

            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const x = slider.trackX
                const inset = root.thumb / 2
                const bottom = height - inset
                const top = inset
                const y = v => bottom - root.position(v) * (bottom - top)
                const hasDigital = root.maxLevel > root.opticalMax
                const opticalTop = hasDigital ? y(root.opticalMax) : top

                ctx.lineCap = "round"
                ctx.lineWidth = 4
                // Optical range, then the digital range dashed in amber.
                ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.18)
                ctx.beginPath(); ctx.moveTo(x, bottom); ctx.lineTo(x, opticalTop); ctx.stroke()
                if (hasDigital) {
                    ctx.lineCap = "butt"
                    ctx.strokeStyle = Theme.alpha(Theme.warning, 0.55)
                    for (let dy = opticalTop - 3; dy > top; dy -= 6) {
                        ctx.beginPath(); ctx.moveTo(x, dy); ctx.lineTo(x, Math.max(top, dy - 3)); ctx.stroke()
                    }
                    ctx.lineCap = "round"
                }
                // Fill up to the level asked for.
                const levelY = y(level)
                const fill = ctx.createLinearGradient(x, bottom, x, levelY)
                fill.addColorStop(0, Theme.purple)
                fill.addColorStop(1, Theme.magenta)
                ctx.strokeStyle = fill
                ctx.beginPath(); ctx.moveTo(x, bottom); ctx.lineTo(x, levelY); ctx.stroke()

                // Scale
                const marks = hasDigital ? [1, 5, root.opticalMax, 100, root.maxLevel] : [1, 2, 5, 10, root.opticalMax]
                ctx.font = "600 9px '" + AppFont.displayFamily + "'"
                ctx.textAlign = "right"
                ctx.textBaseline = "middle"
                for (const mark of marks) {
                    if (mark > root.maxLevel) continue
                    const my = y(mark)
                    const boundary = hasDigital && mark === root.opticalMax
                    ctx.strokeStyle = boundary ? Theme.warning : Theme.textTertiary
                    ctx.lineWidth = boundary ? 1.5 : 1
                    ctx.lineCap = "butt"
                    ctx.beginPath(); ctx.moveTo(x - (boundary ? 7 : 5), my); ctx.lineTo(x + (boundary ? 7 : -3), my); ctx.stroke()
                    ctx.fillStyle = boundary ? Theme.warning : Theme.textSecondary
                    ctx.fillText(mark.toFixed(0), x - 9, my)
                }
                // A dot at each level the thumb can stop on.
                ctx.fillStyle = Theme.textTertiary
                for (const detent of root.levels) {
                    if (detent > root.maxLevel) continue
                    ctx.beginPath(); ctx.arc(x + 5, y(detent), 1, 0, 2 * Math.PI); ctx.fill()
                }
                // Where the lens is while it travels.
                if (lens > 0) {
                    const ay = y(lens)
                    ctx.fillStyle = Theme.text
                    ctx.beginPath(); ctx.moveTo(x + 5, ay); ctx.lineTo(x + 12, ay - 4.5); ctx.lineTo(x + 12, ay + 4.5)
                    ctx.closePath(); ctx.fill()
                }
            }
        }

        // Thumb
        Rectangle {
            readonly property real grow: root.dragStart > 0 ? 1.15 : 1
            width: root.thumb * grow
            height: width
            radius: width / 2
            x: slider.trackX - width / 2
            y: root.thumb / 2 + (1 - slider.thumbPosition) * slider.travel - height / 2
            color: Theme.text
            border.color: Theme.magenta
            border.width: 2.5
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.SizeVerCursor
            property real startY: 0

            onPressed: mouse => {
                startY = mouse.y
                root.dragStart = root.target > 0 ? root.target : root.actual
                if (pendingStop.running) {
                    // A button pulse is still zooming: stop it now, not in the middle of the drag.
                    pendingStop.stop()
                    App.zoom(0)
                }
            }
            onPositionChanged: mouse => {
                if (root.dragStart === 0) return
                const p = Math.max(0, Math.min(1, root.position(root.dragStart) - (mouse.y - startY) / slider.travel))
                // The camera only lands on its own levels, so the thumb snaps to them.
                const level = root.levels[root.nearest(root.levelAt(p))]
                if (level !== undefined && level !== (root.target > 0 ? root.target : root.actual))
                    root.send(level)
            }
            onReleased: {
                root.dragStart = 0
                giveUp.restart()
            }
            onCanceled: {
                root.dragStart = 0
                giveUp.restart()
            }
            onWheel: wheel => {
                const step = wheel.angleDelta.y > 0 ? 1 : (wheel.angleDelta.y < 0 ? -1 : 0)
                if (step === 0) return
                const from = root.nearest(root.target > 0 ? root.target : root.actual)
                const index = Math.max(0, Math.min(root.levels.length - 1, from + step))
                if (root.levels[index] <= root.maxLevel) root.send(root.levels[index])
            }
        }
    }

    HoldButton {
        anchors.horizontalCenter: parent.horizontalCenter
        height: root.compact ? 44 : 52
        icon: "zoom_out"
        description: "Zoom out"
        onPressed: root.startZoom(-1)
        onReleased: root.stopZoom()
    }
}
