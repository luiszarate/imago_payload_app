import QtQuick
import QtQuick.Effects
import Imago

// A small north-up map in the picture's bottom right corner, as on the Quest: the aircraft in
// the middle turned to its heading, the camera's cone, and the point the camera looks at when
// the payload works it out. Translucent until the pointer is over it (or it was touched), with
// its own buttons then: put away, satellite / street, closer, further. Tiles come from this
// computer only (Settings › Map): the payload's Wi-Fi has no internet. A missing tile is a grid
// cell, so the scale still reads. Images load and decode on Qt's own threads, never the video's.
Item {
    id: root

    property real size: 240
    readonly property var map: App.map
    /// A 256 px tile is drawn this large (in logical pixels).
    readonly property real tileSize: 160
    readonly property int columns: Math.ceil(size / tileSize) + 1
    readonly property int firstColumn: Math.floor(map.centreX - size / 2 / tileSize)
    readonly property int firstRow: Math.floor(map.centreY - size / 2 / tileSize)
    readonly property bool active: hover.hovered || touched.running
    property int storedTiles: 0

    implicitWidth: map.visible ? size : restore.implicitWidth
    implicitHeight: map.visible ? size : restore.implicitHeight

    function recount() {
        let count = 0
        for (let i = 0; i < tiles.count; ++i) {
            const item = tiles.itemAt(i)
            if (item && item.stored) count += 1
        }
        storedTiles = count
    }

    HoverHandler { id: hover }
    // On a touch screen there is no hover: a touch shows the buttons for a while.
    Timer { id: touched; interval: 4000 }

    // Put away: a faint button in the corner brings it back.
    IconPill {
        id: restore
        visible: !root.map.visible
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        icon: "map"
        label: "Show the map"
        opacity: hover.hovered ? 1 : 0.55
        onClicked: root.map.visible = true
    }

    Item {
        id: face
        visible: root.map.visible
        anchors.fill: parent
        opacity: root.active ? 1 : 0.8
        Behavior on opacity { NumberAnimation { duration: 150 } }

        // Drawn into a layer and cut to the rounded square below.
        Item {
            id: content
            anchors.fill: parent
            layer.enabled: true
            visible: false

            Rectangle {
                anchors.fill: parent
                color: Theme.surfaceRaised
            }

            Repeater {
                id: tiles
                model: root.map.hasPosition ? root.columns * root.columns : 0
                Item {
                    id: cell
                    required property int index
                    readonly property int column: root.firstColumn + index % root.columns
                    readonly property int row: root.firstRow + Math.floor(index / root.columns)
                    readonly property string source: {
                        // Asked again when the zoom or style changes and when a tile arrives.
                        root.map.revision; root.map.zoom; root.map.satellite
                        return root.map.tileSource(column, row)
                    }
                    readonly property bool stored: source.length > 0
                    x: root.size / 2 + (column - root.map.centreX) * root.tileSize
                    y: root.size / 2 + (row - root.map.centreY) * root.tileSize
                    width: root.tileSize + 0.5
                    height: root.tileSize + 0.5
                    onStoredChanged: root.recount()
                    Component.onCompleted: root.recount()
                    Component.onDestruction: Qt.callLater(root.recount)

                    Image {
                        anchors.fill: parent
                        source: cell.source
                        asynchronous: true
                        smooth: true
                    }
                    // A grid where there is no tile.
                    Rectangle {
                        visible: !cell.stored
                        anchors.fill: parent
                        color: "transparent"
                        border.color: Theme.stroke
                    }
                }
            }
        }

        Item {
            id: mask
            anchors.fill: parent
            layer.enabled: true
            visible: false
            Rectangle {
                anchors.fill: parent
                radius: 16
                color: "black"
            }
        }
        MultiEffect {
            anchors.fill: content
            source: content
            maskEnabled: true
            maskSource: mask
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1.0
        }
        // Marks and words over the tiles; drawn outside the layer, since a Canvas in a hidden
        // item never paints.
        Item {
            anchors.fill: parent
            clip: true

            Canvas {
                id: marks
                anchors.fill: parent
                visible: root.map.hasPosition
                renderStrategy: Canvas.Cooperative

                readonly property real heading: App.vehicleHeading
                // Where the camera looks from North: its bearing, else the aircraft's heading plus
                // the gimbal's yaw against the nose (as the attitude indicator works it out).
                readonly property real bearing: App.gimbal.hasYawNorth ? App.gimbal.yawNorth
                                                : (heading >= 0 && App.gimbal.hasYaw ? heading + App.gimbal.yaw : NaN)
                readonly property real fov: App.camera.horizontalFov
                readonly property bool hasTarget: root.map.hasTarget
                readonly property real targetX: root.size / 2 + (root.map.targetX - root.map.centreX) * root.tileSize
                readonly property real targetY: root.size / 2 + (root.map.targetY - root.map.centreY) * root.tileSize
                onHeadingChanged: requestPaint()
                onBearingChanged: requestPaint()
                onFovChanged: requestPaint()
                onHasTargetChanged: requestPaint()
                onTargetXChanged: requestPaint()
                onTargetYChanged: requestPaint()
                onVisibleChanged: requestPaint()

                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    const cx = width / 2
                    const cy = height / 2

                    if (!isNaN(bearing)) {
                        const half = Math.max(3, Math.min(40, (fov > 0 ? fov : 30) / 2))
                        const length = root.size * 0.4
                        const gradient = ctx.createRadialGradient(cx, cy, 0, cx, cy, length)
                        gradient.addColorStop(0, Theme.alpha(Theme.magenta, 0.6))
                        gradient.addColorStop(1, Theme.alpha(Theme.fuchsia, 0.08))
                        ctx.beginPath()
                        ctx.moveTo(cx, cy)
                        ctx.arc(cx, cy, length, (bearing - half - 90) * Math.PI / 180, (bearing + half - 90) * Math.PI / 180, false)
                        ctx.closePath()
                        ctx.fillStyle = gradient
                        ctx.fill()
                        ctx.strokeStyle = Theme.magenta
                        ctx.lineWidth = 1
                        ctx.stroke()
                    }

                    if (hasTarget) {
                        ctx.strokeStyle = Theme.text
                        ctx.lineWidth = 2
                        ctx.beginPath(); ctx.arc(targetX, targetY, 6, 0, 2 * Math.PI); ctx.stroke()
                        for (const [dx, dy] of [[-1, 0], [1, 0], [0, -1], [0, 1]]) {
                            ctx.beginPath()
                            ctx.moveTo(targetX + dx * 4, targetY + dy * 4)
                            ctx.lineTo(targetX + dx * 10, targetY + dy * 10)
                            ctx.stroke()
                        }
                    }

                    // The aircraft, turned to its heading; a dot when the heading is not known.
                    if (heading >= 0) {
                        const size = 16
                        const right = [[0, 1], [0.07, 0.86], [0.08, 0.32], [1, 0.07], [1, -0.07], [0.08, 0.02],
                                       [0.06, -0.6], [0.4, -0.76], [0.4, -0.86], [0.05, -0.84], [0, -0.92]]
                        const outline = right.concat(right.slice(1, -1).reverse().map(p => [-p[0], p[1]]))
                        const a = heading * Math.PI / 180
                        ctx.beginPath()
                        outline.forEach((p, i) => {
                            const x = p[0] * size
                            const y = -p[1] * size
                            const rx = cx + x * Math.cos(a) - y * Math.sin(a)
                            const ry = cy + x * Math.sin(a) + y * Math.cos(a)
                            if (i === 0) ctx.moveTo(rx, ry); else ctx.lineTo(rx, ry)
                        })
                        ctx.closePath()
                        ctx.fillStyle = Theme.text
                        ctx.fill()
                        ctx.strokeStyle = Theme.alpha(Theme.background, 0.85)
                        ctx.lineWidth = 1.5
                        ctx.stroke()
                    } else {
                        ctx.fillStyle = Theme.text
                        ctx.beginPath(); ctx.arc(cx, cy, 5, 0, 2 * Math.PI); ctx.fill()
                    }
                }
            }

            Column {
                anchors.centerIn: parent
                visible: !root.map.hasPosition || root.storedTiles === 0
                spacing: 2
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: root.map.hasPosition ? "No map stored here" : "No aircraft position"
                    color: Theme.textSecondary
                    style: Text.Outline
                    styleColor: Theme.alpha(Theme.background, 0.6)
                    font: AppFont.body(13, 600)
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: root.map.hasPosition
                    text: "Download it in Settings › Map"
                    color: Theme.textSecondary
                    style: Text.Outline
                    styleColor: Theme.alpha(Theme.background, 0.6)
                    font: AppFont.body(11, 500)
                }
            }

            // North is up; the heading as a figure.
            Row {
                x: 10
                y: 8
                spacing: 8
                Text {
                    text: "N ↑"
                    color: Theme.magenta
                    style: Text.Outline
                    styleColor: Theme.alpha(Theme.background, 0.7)
                    font: AppFont.display(12, 700)
                }
                Text {
                    visible: App.vehicleHeading >= 0
                    text: "HDG " + String(Math.round(App.vehicleHeading) % 360).padStart(3, "0") + "°"
                    color: Theme.text
                    style: Text.Outline
                    styleColor: Theme.alpha(Theme.background, 0.7)
                    font: AppFont.figures(12, 600)
                }
            }
            // Who the map is by.
            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 6
                anchors.leftMargin: 9
                text: root.map.credit
                color: Theme.textSecondary
                style: Text.Outline
                styleColor: Theme.alpha(Theme.background, 0.7)
                font: AppFont.body(9, 500)
                elide: Text.ElideRight
            }
        }
        Rectangle {
            anchors.fill: parent
            radius: 16
            color: "transparent"
            border.color: Theme.alpha(Theme.text, root.active ? 0.3 : 0.18)
        }

        // A click on the map is the map's, never a track on the picture under it.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onPressed: touched.restart()
            onWheel: wheel => {
                root.map.zoomBy(wheel.angleDelta.y > 0 ? 1 : (wheel.angleDelta.y < 0 ? -1 : 0))
                touched.restart()
            }
        }

        component MapButton: Rectangle {
            id: button
            property string icon
            property string label
            signal clicked()
            width: 30
            height: 30
            radius: 10
            color: Theme.alpha(Theme.surface, mouse.containsMouse ? 0.95 : 0.8)
            border.color: Theme.stroke
            opacity: enabled ? 1 : 0.4
            scale: mouse.pressed ? 0.94 : 1
            Accessible.role: Accessible.Button
            Accessible.name: label
            Accessible.onPressAction: clicked()
            Icon {
                anchors.centerIn: parent
                name: button.icon
                size: 17
            }
            MouseArea {
                id: mouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: button.clicked()
            }
            ToolTipText {
                visible: mouse.containsMouse
                text: button.label
            }
        }

        Row {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 6
            spacing: 5
            visible: root.active

            MapButton {
                icon: "remove"
                label: "Zoom out"
                enabled: root.map.zoom > root.map.minZoom
                onClicked: { touched.restart(); root.map.zoomBy(-1) }
            }
            MapButton {
                icon: "add"
                label: "Zoom in"
                enabled: root.map.zoom < root.map.maxZoom
                onClicked: { touched.restart(); root.map.zoomBy(1) }
            }
            MapButton {
                icon: root.map.satellite ? "map" : "satellite"
                label: root.map.satellite ? "Street map" : "Satellite"
                onClicked: { touched.restart(); root.map.toggleStyle() }
            }
            MapButton {
                icon: "close"
                label: "Hide the map"
                onClicked: root.map.visible = false
            }
        }
    }
}
