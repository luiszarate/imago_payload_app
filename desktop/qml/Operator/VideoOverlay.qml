import QtQuick
import QtQuick.Shapes
import Imago

// Drawn over the picture: AI detections, the tracked target and a centre reticle.
// A click selects a target (or re-points the gimbal in Point mode); the wheel or a pinch zooms.
Item {
    id: root

    /// Where the picture is, in this item's coordinates (VideoOutput.contentRect).
    property rect contentRect
    readonly property rect picture: contentRect.width > 0 ? contentRect : Qt.rect(0, 0, width, height)

    // Detections
    Repeater {
        model: App.tracking.detections
        Item {
            required property var modelData
            readonly property color tint: Theme.classColors[modelData.classId % Theme.classColors.length]
            x: root.picture.x + modelData.x * root.picture.width
            y: root.picture.y + modelData.y * root.picture.height
            width: modelData.w * root.picture.width
            height: modelData.h * root.picture.height

            Rectangle {
                anchors.fill: parent
                color: "transparent"
                radius: 3
                border.color: parent.tint
                border.width: 2
            }
            Rectangle {
                y: -height - 2
                width: tag.implicitWidth + 10
                height: tag.implicitHeight + 4
                radius: 3
                color: parent.tint
                Text {
                    id: tag
                    anchors.centerIn: parent
                    text: parent.parent.modelData.label + " " + Math.round(parent.parent.modelData.confidence * 100) + "%"
                    color: "black"
                    font: AppFont.display(11, 700)
                }
            }
        }
    }

    // Tracked target: corner brackets, amber while the target is lost.
    Item {
        id: target
        readonly property bool lost: App.tracking.state === Payload.TrackingLost
        readonly property rect box: App.tracking.box
        visible: App.tracking.hasBox
        width: Math.max(box.width * root.picture.width, 24)
        height: Math.max(box.height * root.picture.height, 24)
        x: root.picture.x + (box.x + box.width / 2) * root.picture.width - width / 2
        y: root.picture.y + (box.y + box.height / 2) * root.picture.height - height / 2

        Shape {
            anchors.fill: parent
            ShapePath {
                id: bracket
                strokeColor: target.lost ? Theme.warning : Theme.magenta
                strokeWidth: 3
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                readonly property real w: target.width
                readonly property real h: target.height
                readonly property real a: Math.min(w, h) * 0.28
                PathMove { x: 0; y: bracket.a }
                PathLine { x: 0; y: 0 }
                PathLine { x: bracket.a; y: 0 }
                PathMove { x: bracket.w - bracket.a; y: 0 }
                PathLine { x: bracket.w; y: 0 }
                PathLine { x: bracket.w; y: bracket.a }
                PathMove { x: bracket.w; y: bracket.h - bracket.a }
                PathLine { x: bracket.w; y: bracket.h }
                PathLine { x: bracket.w - bracket.a; y: bracket.h }
                PathMove { x: bracket.a; y: bracket.h }
                PathLine { x: 0; y: bracket.h }
                PathLine { x: 0; y: bracket.h - bracket.a }
            }
        }
        Text {
            y: parent.height + 4
            text: target.lost ? "LOST" : "TRACKING"
            color: target.lost ? Theme.warning : Theme.magenta
            font: AppFont.display(10, 700, 1.2)
        }
    }

    // Centre reticle
    Item {
        x: root.picture.x + root.picture.width / 2
        y: root.picture.y + root.picture.height / 2

        Repeater {
            // Shadow first, then the reticle itself.
            model: [{ dy: 1, color: Qt.rgba(0, 0, 0, 0.6) }, { dy: 0, color: Qt.rgba(1, 1, 1, 0.55) }]
            Shape {
                required property var modelData
                y: modelData.dy
                ShapePath {
                    strokeColor: modelData.color
                    strokeWidth: 1
                    fillColor: "transparent"
                    PathAngleArc { centerX: 0; centerY: 0; radiusX: 17; radiusY: 17; startAngle: 0; sweepAngle: 360 }
                    PathMove { x: 0; y: -29 }
                    PathLine { x: 0; y: -17 }
                    PathMove { x: 0; y: 17 }
                    PathLine { x: 0; y: 29 }
                    PathMove { x: -29; y: 0 }
                    PathLine { x: -17; y: 0 }
                    PathMove { x: 17; y: 0 }
                    PathLine { x: 29; y: 0 }
                }
            }
        }
        Rectangle {
            visible: App.pointMode
            width: 5; height: 5; radius: 2.5
            x: -2.5; y: -2.5
            color: Theme.magenta
        }
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        onTapped: point => {
            const p = point.position
            const r = root.picture
            if (p.x >= r.x && p.x <= r.x + r.width && p.y >= r.y && p.y <= r.y + r.height)
                App.videoTapped((p.x - r.x) / r.width, (p.y - r.y) / r.height)
        }
    }

    // Pinch on a touch screen: zoom while the fingers spread or close.
    PinchHandler {
        id: pinch
        target: null
        property int direction: 0
        onScaleChanged: {
            const d = activeScale > 1.04 ? 1 : (activeScale < 0.96 ? -1 : 0)
            if (d !== direction) {
                direction = d
                App.zoom(d)
            }
        }
        onActiveChanged: {
            if (!active && direction !== 0) {
                direction = 0
                App.zoom(0) // also when the gesture is cancelled
            }
        }
    }

    // Mouse wheel: zoom while it turns.
    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: event => {
            const d = event.angleDelta.y > 0 ? 1 : (event.angleDelta.y < 0 ? -1 : 0)
            if (d === 0) return
            if (App.zoomDirection !== d) App.zoom(d)
            wheelStop.restart()
        }
    }
    Timer {
        id: wheelStop
        interval: 250
        onTriggered: App.zoom(0)
    }
}
