package aero.imago.payloadcontrol.ui

import aero.imago.payloadcontrol.core.TrackingState
import aero.imago.payloadcontrol.video.fitRect
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.positionChanged
import androidx.compose.ui.text.TextMeasurer
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/**
 * Drawn over the picture: AI detections, the tracked target and a centre reticle.
 * A tap selects a target (or re-points the gimbal in Point mode); a pinch zooms.
 */
@Composable
fun VideoOverlay(modifier: Modifier) {
    val model = LocalModel.current
    val measurer = rememberTextMeasurer()
    Canvas(
        modifier.pointerInput(Unit) {
            awaitEachGesture {
                val down = awaitFirstDown(requireUnconsumed = true)
                val slop = viewConfiguration.touchSlop
                var pinchStart = 0f
                var pinchDirection = 0
                var moved = false
                var multiTouch = false
                try {
                    while (true) {
                        val event = awaitPointerEvent(PointerEventPass.Main)
                        val pressed = event.changes.filter { it.pressed }
                        if (pressed.isEmpty()) break
                        if (pressed.size >= 2) {
                            multiTouch = true
                            val distance = (pressed[0].position - pressed[1].position).getDistance()
                            if (pinchStart == 0f) pinchStart = distance
                            val scale = distance / pinchStart
                            val direction = if (scale > 1.04f) 1 else if (scale < 0.96f) -1 else 0
                            if (direction != pinchDirection) {
                                pinchDirection = direction
                                model.zoom(direction)
                            }
                            event.changes.forEach { it.consume() }
                        } else if (event.changes.any { it.positionChanged() } &&
                                   (pressed[0].position - down.position).getDistance() > slop) {
                            moved = true
                        }
                    }
                } finally {
                    if (pinchDirection != 0) model.zoom(0) // also when the gesture is cancelled
                }
                if (!moved && !multiTouch) {
                    val rect = fitRect(model.videoWidth, model.videoHeight, Size(size.width.toFloat(), size.height.toFloat()))
                    if (rect.contains(down.position)) {
                        model.videoTapped((down.position.x - rect.left) / rect.width,
                                          (down.position.y - rect.top) / rect.height)
                    }
                }
            }
        },
    ) {
        val rect = fitRect(model.videoWidth, model.videoHeight, size)
        for (detection in model.tracking.detections) {
            val color = Theme.classColors[detection.classId % Theme.classColors.size]
            val box = Rect(rect.left + detection.rect.x * rect.width, rect.top + detection.rect.y * rect.height,
                           rect.left + (detection.rect.x + detection.rect.w) * rect.width,
                           rect.top + (detection.rect.y + detection.rect.h) * rect.height)
            drawDetection(measurer, box, color,
                          "${detection.label} ${(detection.confidence * 100).roundToInt()}%")
        }
        model.tracking.box?.let { b ->
            val w = max(b.w * rect.width, 24.dp.toPx())
            val h = max(b.h * rect.height, 24.dp.toPx())
            val cx = rect.left + b.midX * rect.width
            val cy = rect.top + b.midY * rect.height
            drawTrackingBox(measurer, Rect(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2),
                            lost = model.tracking.state == TrackingState.LOST)
        }
        drawReticle(rect.center, model.pointMode)
    }
}

private fun DrawScope.drawDetection(measurer: TextMeasurer, box: Rect, color: Color, label: String) {
    drawRoundRect(color, box.topLeft, box.size, CornerRadius(3.dp.toPx()), style = Stroke(2.dp.toPx()))
    val text = measurer.measure(label, AppFont.display(11, 700, Color.Black))
    val padH = 5.dp.toPx()
    val padV = 2.dp.toPx()
    val tag = Rect(box.left, box.top - text.size.height - padV * 2 - 2.dp.toPx(),
                   box.left + text.size.width + padH * 2, box.top - 2.dp.toPx())
    drawRoundRect(color, tag.topLeft, tag.size, CornerRadius(3.dp.toPx()))
    drawText(text, topLeft = Offset(tag.left + padH, tag.top + padV))
}

/** Corner brackets in the brand gradient; amber while the target is lost. */
private fun DrawScope.drawTrackingBox(measurer: TextMeasurer, box: Rect, lost: Boolean) {
    val arm = min(box.width, box.height) * 0.28f
    val path = Path().apply {
        moveTo(box.left, box.top + arm); lineTo(box.left, box.top); lineTo(box.left + arm, box.top)
        moveTo(box.right - arm, box.top); lineTo(box.right, box.top); lineTo(box.right, box.top + arm)
        moveTo(box.right, box.bottom - arm); lineTo(box.right, box.bottom); lineTo(box.right - arm, box.bottom)
        moveTo(box.left + arm, box.bottom); lineTo(box.left, box.bottom); lineTo(box.left, box.bottom - arm)
    }
    val stroke = Stroke(3.dp.toPx(), cap = StrokeCap.Round, join = StrokeJoin.Round)
    if (lost) drawPath(path, Theme.warning, style = stroke)
    else drawPath(path, androidx.compose.ui.graphics.Brush.linearGradient(
        listOf(Theme.magenta, Theme.fuchsia, Theme.purple), box.topLeft, box.bottomRight), style = stroke)
    val text = measurer.measure(if (lost) "LOST" else "TRACKING",
                                AppFont.display(10, 700, if (lost) Theme.warning else Theme.magenta).copy(letterSpacing = 1.2.sp))
    drawText(text, topLeft = Offset(box.left, box.bottom + 4.dp.toPx()))
}

private fun DrawScope.drawReticle(center: Offset, pointMode: Boolean) {
    val color = Color.White.copy(alpha = 0.55f)
    val shadow = Color.Black.copy(alpha = 0.6f)
    val line = 1.dp.toPx()
    for ((c, offset) in listOf(shadow to Offset(0f, 1.dp.toPx()), color to Offset.Zero)) {
        val o = center + offset
        drawCircle(c, 17.dp.toPx(), o, style = Stroke(line))
        val inner = 17.dp.toPx()
        val outer = 29.dp.toPx()
        drawLine(c, Offset(o.x, o.y - outer), Offset(o.x, o.y - inner), line)
        drawLine(c, Offset(o.x, o.y + inner), Offset(o.x, o.y + outer), line)
        drawLine(c, Offset(o.x - outer, o.y), Offset(o.x - inner, o.y), line)
        drawLine(c, Offset(o.x + inner, o.y), Offset(o.x + outer, o.y), line)
    }
    if (pointMode) drawCircle(Theme.magenta, 2.5f.dp.toPx(), center)
}
