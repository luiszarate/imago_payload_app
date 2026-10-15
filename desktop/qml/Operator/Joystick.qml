import QtQuick
import Imago

// On-screen joystick for gimbal rate control. Reports deflection with x right and y up, both
// -1..1, on every move, and (0, 0) on release. The release also runs when the press is
// cancelled or the joystick goes away, so the gimbal can never be left turning.
Item {
    id: root

    property real diameter: 136
    readonly property real radius: diameter / 2
    readonly property real knobRadius: diameter * 0.18
    readonly property real limit: radius - knobRadius / 2
    property bool active: false
    property real knobX: 0
    property real knobY: 0
    signal moved(real x, real y)

    width: diameter
    height: diameter
    Accessible.role: Accessible.Slider
    Accessible.name: "Gimbal joystick"

    function track(px, py) {
        let dx = px - radius
        let dy = py - radius
        const length = Math.sqrt(dx * dx + dy * dy)
        if (length > limit) {
            dx *= limit / length
            dy *= limit / length
        }
        knobX = dx
        knobY = dy
        moved(dx / limit, -dy / limit)
    }
    function release() {
        if (!active) return
        active = false
        moved(0, 0)
        knobX = 0
        knobY = 0
    }

    Behavior on knobX { enabled: !root.active; SpringAnimation { spring: 6; damping: 0.35 } }
    Behavior on knobY { enabled: !root.active; SpringAnimation { spring: 6; damping: 0.35 } }

    // Face
    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: Theme.alpha(Theme.surfaceRaised, 0.85)
        border.color: root.active ? Theme.magenta : Theme.stroke
        border.width: root.active ? 2 : 1
    }
    // Axis guides
    Rectangle {
        anchors.centerIn: parent
        width: 1
        height: root.radius * 1.4
        color: Theme.stroke
    }
    Rectangle {
        anchors.centerIn: parent
        width: root.radius * 1.4
        height: 1
        color: Theme.stroke
    }
    // Glow while held
    Rectangle {
        visible: root.active
        width: root.knobRadius * 3.6
        height: width
        radius: width / 2
        x: root.radius + root.knobX - width / 2
        y: root.radius + root.knobY - height / 2
        gradient: Gradient {
            orientation: Gradient.Vertical
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.5; color: Theme.alpha(Theme.magenta, 0.35) }
            GradientStop { position: 1.0; color: "transparent" }
        }
        opacity: 0.8
    }
    // Knob
    Rectangle {
        width: root.knobRadius * 2
        height: width
        radius: width / 2
        x: root.radius + root.knobX - width / 2
        y: root.radius + root.knobY - height / 2
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.magenta }
            GradientStop { position: 0.5; color: Theme.fuchsia }
            GradientStop { position: 1.0; color: Theme.purple }
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: root.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        onPressed: mouse => {
            root.active = true
            root.track(mouse.x, mouse.y)
        }
        onPositionChanged: mouse => { if (root.active) root.track(mouse.x, mouse.y) }
        onReleased: root.release()
        onCanceled: root.release()
    }

    Component.onDestruction: release()
}
