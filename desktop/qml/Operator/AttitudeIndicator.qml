import QtQuick
import Imago

// Gimbal attitude against the aircraft, seen from above.
//
// The aircraft always points up. The cone shows where the camera looks, as wide as its field of
// view. The ring is a compass when the aircraft heading is known (it turns as the aircraft
// turns); otherwise its ticks are relative to the nose. The arc on the right is the camera's
// pitch against the horizon, from 30° up to straight down.
Panel {
    id: root

    property bool compact: false
    readonly property real diameter: compact ? 92 : 136
    /// Room around the compass for the pitch arc and its marker.
    readonly property real margin: 16
    readonly property var gimbal: App.gimbal

    radius: 18
    implicitWidth: Math.max(dial.width, readouts.implicitWidth) + (compact ? 16 : 20)
    implicitHeight: dial.height + readouts.implicitHeight + (compact ? 4 : 6) + (compact ? 12 : 16)
    Accessible.role: Accessible.Indicator
    Accessible.name: "Gimbal attitude"
    Accessible.description: {
        if (!gimbal.valid) return "No data"
        let parts = ["pitch " + Math.round(gimbal.pitch) + " degrees"]
        if (gimbal.hasYaw) {
            const d = Math.round(gimbal.yaw)
            parts.push(d === 0 ? "looking forward" : "yaw " + Math.abs(d) + " degrees " + (d > 0 ? "right" : "left"))
        }
        if (gimbal.hasYawNorth) parts.push("bearing " + Math.round(gimbal.yawNorth))
        return parts.join(", ")
    }

    function signed(value) {
        const d = Math.round(value)
        return d === 0 ? "0°" : (d > 0 ? "+" : "") + d + "°"
    }

    Column {
        anchors.centerIn: parent
        spacing: root.compact ? 4 : 6

        Canvas {
            id: dial
            anchors.horizontalCenter: parent.horizontalCenter
            width: root.diameter + root.margin * 2
            height: width
            renderStrategy: Canvas.Cooperative

            property real pitch: root.gimbal.valid ? root.gimbal.pitch : NaN
            property real yaw: root.gimbal.hasYaw ? root.gimbal.yaw : NaN
            property real yawNorth: root.gimbal.hasYawNorth ? root.gimbal.yawNorth : NaN
            property real heading: App.vehicleHeading
            property real fov: App.camera.horizontalFov
            onPitchChanged: requestPaint()
            onYawChanged: requestPaint()
            onYawNorthChanged: requestPaint()
            onHeadingChanged: requestPaint()
            onFovChanged: requestPaint()

            /// Point at `r` from the centre, `degrees` clockwise from straight up.
            function point(cx, cy, r, degrees) {
                const a = degrees * Math.PI / 180
                return { x: cx + r * Math.sin(a), y: cy - r * Math.cos(a) }
            }

            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const cx = width / 2
                const cy = height / 2
                const radius = Math.min(width, height) / 2 - root.margin
                const hasPitch = !isNaN(pitch)
                const hasYaw = !isNaN(yaw)
                const hasNorth = !isNaN(yawNorth)
                const hasHeading = heading >= 0

                // Heading up when the camera's direction against the nose is known, North up when
                // only its bearing is (the aircraft's orientation is then unknown and not drawn).
                const showAircraft = hasYaw || !hasNorth
                const ringRotation = showAircraft && hasHeading ? heading : 0
                const compass = showAircraft ? hasHeading : true
                const coneAngle = hasYaw ? yaw : (hasNorth ? yawNorth : NaN)

                // Face
                ctx.fillStyle = Theme.alpha(Theme.background, 0.55)
                ctx.beginPath(); ctx.arc(cx, cy, radius, 0, 2 * Math.PI); ctx.fill()
                ctx.strokeStyle = Theme.stroke
                ctx.lineWidth = 1
                ctx.stroke()

                // Ring
                for (let bearing = 0; bearing < 360; bearing += 10) {
                    if (compass && bearing % 90 === 0) continue // letters instead
                    const major = bearing % 30 === 0
                    const a = point(cx, cy, radius - 2, bearing - ringRotation)
                    const b = point(cx, cy, radius - (major ? 9 : 5), bearing - ringRotation)
                    ctx.strokeStyle = major ? Theme.textSecondary : Theme.textTertiary
                    ctx.lineWidth = major ? 1.5 : 1
                    ctx.beginPath(); ctx.moveTo(a.x, a.y); ctx.lineTo(b.x, b.y); ctx.stroke()
                }
                if (compass) {
                    const fontPx = Math.max(9, radius * 0.15)
                    ctx.font = "700 " + fontPx + "px '" + AppFont.displayFamily + "'"
                    ctx.textAlign = "center"
                    ctx.textBaseline = "middle"
                    const letters = [[0, "N"], [90, "E"], [180, "S"], [270, "W"]]
                    for (const [bearing, letter] of letters) {
                        const at = point(cx, cy, radius - fontPx * 0.8, bearing - ringRotation)
                        ctx.fillStyle = bearing === 0 ? Theme.magenta : Theme.textSecondary
                        ctx.fillText(letter, at.x, at.y)
                    }
                }

                // Cone: where the camera looks.
                if (!isNaN(coneAngle)) {
                    const half = Math.max(3, Math.min(40, (fov > 0 ? fov : 30) / 2))
                    const length = radius * 0.86
                    const start = point(cx, cy, length, coneAngle - half)
                    const gradient = ctx.createRadialGradient(cx, cy, 0, cx, cy, length)
                    gradient.addColorStop(0, Theme.alpha(Theme.magenta, 0.6))
                    gradient.addColorStop(1, Theme.alpha(Theme.fuchsia, 0.12))
                    ctx.beginPath()
                    ctx.moveTo(cx, cy)
                    ctx.lineTo(start.x, start.y)
                    ctx.arc(cx, cy, length, (coneAngle - half - 90) * Math.PI / 180, (coneAngle + half - 90) * Math.PI / 180, false)
                    ctx.closePath()
                    ctx.fillStyle = gradient
                    ctx.fill()
                    ctx.strokeStyle = Theme.alpha(Theme.magenta, 0.9)
                    ctx.lineWidth = 1
                    ctx.stroke()
                    const tip = point(cx, cy, radius - 3, coneAngle)
                    ctx.strokeStyle = Theme.magenta
                    ctx.lineWidth = 2
                    ctx.lineCap = "round"
                    ctx.beginPath(); ctx.moveTo(cx, cy); ctx.lineTo(tip.x, tip.y); ctx.stroke()
                    ctx.lineCap = "butt"
                }

                // Fixed-wing / VTOL silhouette seen from above, nose up. Right half; x is mirrored.
                if (showAircraft) {
                    const size = radius * 0.42
                    const right = [[0, 1], [0.07, 0.86], [0.08, 0.32], [1, 0.07], [1, -0.07], [0.08, 0.02],
                                   [0.06, -0.6], [0.4, -0.76], [0.4, -0.86], [0.05, -0.84], [0, -0.92]]
                    const outline = right.concat(right.slice(1, -1).reverse().map(p => [-p[0], p[1]]))
                    ctx.beginPath()
                    outline.forEach((p, i) => {
                        const x = cx + p[0] * size
                        const y = cy - p[1] * size
                        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                    })
                    ctx.closePath()
                    ctx.fillStyle = Theme.alpha(Theme.text, hasPitch ? 0.92 : 0.35)
                    ctx.fill()
                    ctx.strokeStyle = Theme.alpha(Theme.background, 0.7)
                    ctx.lineWidth = 1
                    ctx.stroke()
                }

                // Lubber line: the aircraft's nose (or North) on the ring.
                ctx.fillStyle = Theme.text
                ctx.beginPath()
                ctx.moveTo(cx, cy - radius + 7)
                ctx.lineTo(cx - 4, cy - radius - 1)
                ctx.lineTo(cx + 4, cy - radius - 1)
                ctx.closePath()
                ctx.fill()

                // Pitch arc: θ (up positive) sits θ above the 3 o'clock direction, like a side view.
                const arcRadius = radius + 9
                const arcPoint = (degrees, r) => ({ x: cx + r * Math.cos(degrees * Math.PI / 180),
                                                    y: cy - r * Math.sin(degrees * Math.PI / 180) })
                const top = 30
                const bottom = -90
                ctx.lineWidth = 3
                ctx.lineCap = "round"
                ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.18)
                ctx.beginPath(); ctx.arc(cx, cy, arcRadius, -top * Math.PI / 180, -bottom * Math.PI / 180, false); ctx.stroke()
                ctx.lineCap = "butt"
                for (const degrees of [0, -45, -90]) {
                    const a = arcPoint(degrees, arcRadius + 3)
                    const b = arcPoint(degrees, arcRadius + (degrees === 0 ? 8 : 6))
                    ctx.strokeStyle = Theme.textSecondary
                    ctx.lineWidth = 1.5
                    ctx.beginPath(); ctx.moveTo(a.x, a.y); ctx.lineTo(b.x, b.y); ctx.stroke()
                }
                if (!hasPitch) return
                const value = Math.max(bottom, Math.min(top, pitch))
                if (value !== 0) {
                    const from = arcPoint(0, arcRadius)
                    const to = arcPoint(value, arcRadius)
                    const fill = ctx.createLinearGradient(from.x, from.y, to.x, to.y)
                    fill.addColorStop(0, Theme.magenta)
                    fill.addColorStop(1, Theme.purple)
                    ctx.strokeStyle = fill
                    ctx.lineWidth = 3
                    ctx.lineCap = "round"
                    ctx.beginPath()
                    ctx.arc(cx, cy, arcRadius, -Math.max(value, 0) * Math.PI / 180, -Math.min(value, 0) * Math.PI / 180, false)
                    ctx.stroke()
                    ctx.lineCap = "butt"
                }
                const marker = arcPoint(value, arcRadius)
                ctx.fillStyle = Theme.magenta
                ctx.beginPath(); ctx.arc(marker.x, marker.y, 5, 0, 2 * Math.PI); ctx.fill()
                ctx.strokeStyle = Theme.text
                ctx.lineWidth = 1.5
                ctx.stroke()
            }
        }

        Row {
            id: readouts
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: root.compact ? 10 : 14

            component Readout: Row {
                property string label
                property string value
                spacing: 4
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: parent.label
                    color: Theme.textTertiary
                    font: AppFont.display(root.compact ? 9 : 10, 700, 1)
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: parent.value
                    color: Theme.text
                    font: AppFont.figures(root.compact ? 12 : 13, 600)
                }
            }

            Readout { label: "PITCH"; value: root.gimbal.valid ? root.signed(root.gimbal.pitch) : "—" }
            Readout { label: "YAW"; value: root.gimbal.hasYaw ? root.signed(root.gimbal.yaw) : "—" }
            Readout {
                visible: root.gimbal.hasYawNorth
                label: "BRG"
                value: String(Math.round(root.gimbal.yawNorth) % 360).padStart(3, "0") + "°"
            }
        }
    }
}
