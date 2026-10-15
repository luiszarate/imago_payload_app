import QtQuick
import Imago

// An IR palette's colours as a gradient: cold to hot, left to right or (vertical) bottom to top.
Canvas {
    id: root

    property var colors: []
    property bool vertical: false
    property real radius: 3

    renderStrategy: Canvas.Cooperative
    onColorsChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        if (width <= 0 || height <= 0) return
        const fill = vertical ? ctx.createLinearGradient(0, height, 0, 0) : ctx.createLinearGradient(0, 0, width, 0)
        const count = colors.length
        for (let i = 0; i < count; ++i) {
            fill.addColorStop(count > 1 ? i / (count - 1) : 0, colors[i])
        }
        const r = Math.min(radius, width / 2, height / 2)
        ctx.beginPath()
        ctx.roundedRect(0.5, 0.5, width - 1, height - 1, r, r)
        ctx.fillStyle = count > 0 ? fill : Theme.surfaceRaised
        ctx.fill()
        ctx.strokeStyle = Theme.stroke
        ctx.lineWidth = 1
        ctx.stroke()
    }
}
