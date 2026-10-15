package aero.imago.payloadcontrol.ui

import aero.imago.payloadcontrol.app.AppModel
import aero.imago.payloadcontrol.core.CameraStatus
import aero.imago.payloadcontrol.core.PayloadSession
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.snap
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ZoomIn
import androidx.compose.material.icons.filled.ZoomOut
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextMeasurer
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import java.util.Locale
import kotlin.math.abs
import kotlin.math.exp
import kotlin.math.ln

/**
 * Zoom level, hold buttons and a slider between them.
 *
 * The slider is logarithmic, so every doubling gets the same travel and 1–25× keeps most of
 * the length even when the combined range reaches 300×. Its range follows the camera's zoom
 * mode; the digital part, above 25×, is marked. Dragging moves the thumb with the finger (a tap
 * does not jump), and the thumb shows the level asked for while a marker beside the track shows
 * where the lens actually is until it gets there.
 */
@Composable
fun ZoomControl(compact: Boolean) {
    val model = LocalModel.current
    val scope = rememberCoroutineScope()

    /** Level asked for that the lens has not reached yet. */
    var target by remember { mutableStateOf<Float?>(null) }
    /** Level when the current drag began. */
    var dragStart by remember { mutableStateOf<Float?>(null) }
    /** Bumped on every release, to restart the timeout that gives up on an unreached target. */
    var releases by remember { mutableIntStateOf(0) }
    /** When the current press of + or − began. */
    var pressedAt by remember { mutableStateOf<Long?>(null) }
    /** Stop held back until a short press has zoomed for MINIMUM_PULSE_MS. */
    var pendingStop by remember { mutableStateOf<Job?>(null) }

    val maxLevel = maxOf(model.camera.zoomMax ?: FALLBACK_MAX, 2f)
    val levels = remember(maxLevel) {
        PayloadSession.zoomLevels(superResolution = maxLevel <= CameraStatus.OPTICAL_ZOOM_MAX)
    }
    val actual = if (model.camera.zoom > 0) model.camera.zoom else 1f
    val shown = target ?: actual
    val trackLength = if (compact) 140.dp else 190.dp

    LaunchedEffect(model.zoomDirection) {
        if (model.zoomDirection != 0) target = null // the buttons or a pinch took over
    }
    LaunchedEffect(model.camera.zoom) {
        val t = target
        if (t != null && dragStart == null && abs(model.camera.zoom - t) <= maxOf(0.1f, t * 0.03f)) target = null
    }
    LaunchedEffect(releases) {
        // A lens that never gets there (refused, clamped, lost): follow the camera again.
        delay(5000)
        if (dragStart == null) target = null
    }

    fun send(level: Float) {
        target = level
        model.zoomTo(level)
    }

    fun startZoom(direction: Int) {
        pendingStop?.cancel() // a new press takes over from a pulse still running
        pendingStop = null
        pressedAt = System.nanoTime()
        model.zoom(direction)
    }

    fun stopZoom() {
        val heldMs = pressedAt?.let { (System.nanoTime() - it) / 1_000_000 } ?: MINIMUM_PULSE_MS
        pressedAt = null
        val remaining = MINIMUM_PULSE_MS - heldMs
        if (remaining <= 0) {
            model.zoom(0)
            return
        }
        // A tap starts and stops within a few ms, too soon for the lens to move.
        pendingStop = scope.launch {
            delay(remaining)
            model.zoom(0)
        }
    }

    Column(horizontalAlignment = Alignment.CenterHorizontally,
           verticalArrangement = Arrangement.spacedBy(if (compact) 6.dp else 8.dp)) {
        val digital = shown > CameraStatus.OPTICAL_ZOOM_MAX + 0.05f
        Text(
            if (model.camera.zoom > 0 || target != null) format(shown) else "—",
            style = AppFont.figures(13, 600, if (digital) Theme.warning else Theme.text),
            modifier = Modifier.height(16.dp),
        )
        HoldButton(Icons.Filled.ZoomIn, "Zoom in", { startZoom(1) }, ::stopZoom, if (compact) 44.dp else 52.dp)

        // Slider
        val textMeasurer = rememberTextMeasurer()
        val thumbPosition by animateFloatAsState(
            position(shown, maxLevel), if (dragStart == null) tween(200) else snap(), label = "zoom")
        val dragging = dragStart != null
        Box(
            Modifier
                .size(56.dp, trackLength)
                .semantics {
                    contentDescription = "Zoom level"
                    stateDescription = format(shown)
                }
                .pointerInput(maxLevel, levels) {
                    val thumb = THUMB.toPx()
                    awaitEachGesture {
                        val down = awaitFirstDown(requireUnconsumed = false)
                        val start = target ?: actualLevel(model)
                        dragStart = start
                        model.haptics(AppModel.Haptic.TAP)
                        pendingStop?.let {
                            // A button pulse is still zooming: stop it now, not in the middle of the drag.
                            it.cancel()
                            pendingStop = null
                            model.zoom(0)
                        }
                        val travel = size.height - thumb
                        try {
                            while (true) {
                                val event = awaitPointerEvent()
                                val change = event.changes.firstOrNull { it.id == down.id } ?: break
                                if (!change.pressed) break
                                change.consume()
                                val dy = change.position.y - down.position.y
                                val p = (position(start, maxLevel) - dy / travel).coerceIn(0f, 1f)
                                // The camera only lands on its own levels, so the thumb snaps to them.
                                val level = levels.getOrNull(nearestIndex(level(p, maxLevel), levels)) ?: continue
                                if (level != (target ?: actualLevel(model))) {
                                    model.haptics(AppModel.Haptic.TAP)
                                    send(level)
                                }
                            }
                        } finally {
                            dragStart = null
                            releases += 1
                        }
                    }
                },
        ) {
            Canvas(Modifier.fillMaxSize()) {
                drawZoomTrack(
                    textMeasurer, level = shown,
                    actual = if (target != null && model.camera.zoom > 0) actual else null,
                    maxLevel = maxLevel, detents = levels, inset = THUMB.toPx() / 2,
                )
                // Thumb
                val x = size.width / 2 + TRACK_OFFSET.toPx()
                val radius = THUMB.toPx() / 2 * (if (dragging) 1.15f else 1f)
                val y = THUMB.toPx() / 2 + (1 - thumbPosition) * (size.height - THUMB.toPx())
                drawCircle(Theme.text, radius, Offset(x, y))
                drawCircle(Theme.magenta, radius - 1.25.dp.toPx(), Offset(x, y), style = Stroke(2.5.dp.toPx()))
            }
        }

        HoldButton(Icons.Filled.ZoomOut, "Zoom out", { startZoom(-1) }, ::stopZoom, if (compact) 44.dp else 52.dp)
    }
}

private const val MINIMUM_PULSE_MS = 300L
/** Range used until the camera reports its zoom mode. */
private const val FALLBACK_MAX = 300f
private val THUMB = 24.dp
/** The track sits right of centre, leaving room for the labels on its left. */
private val TRACK_OFFSET = 9.dp

private fun actualLevel(model: AppModel) = if (model.camera.zoom > 0) model.camera.zoom else 1f

/** 0 at 1×, 1 at the top of the range. */
private fun position(level: Float, maxLevel: Float): Float =
    (ln(maxOf(level, 1f)) / ln(maxLevel)).coerceIn(0f, 1f)

private fun level(position: Float, maxLevel: Float): Float = exp(position * ln(maxLevel))

/** Nearest on a log scale, as the core snaps it. */
private fun nearestIndex(level: Float, levels: List<Float>): Int {
    val target = ln(maxOf(level, 0.001f))
    return levels.indices.minByOrNull { abs(ln(levels[it]) - target) } ?: 0
}

private fun format(level: Float): String =
    if (level >= 100) String.format(Locale.US, "%.0f×", level) else String.format(Locale.US, "%.1f×", level)

/** Track, scale and the lens marker. */
private fun DrawScope.drawZoomTrack(
    measurer: TextMeasurer, level: Float, actual: Float?, maxLevel: Float, detents: List<Float>, inset: Float,
) {
    val x = size.width / 2 + TRACK_OFFSET.toPx()
    val bottom = size.height - inset
    val top = inset
    fun y(value: Float) = bottom - position(value, maxLevel) * (bottom - top)
    val optical = CameraStatus.OPTICAL_ZOOM_MAX
    val hasDigital = maxLevel > optical
    val width = 4.dp.toPx()

    // Optical range, then the digital range dashed in amber.
    val opticalTop = if (hasDigital) y(optical) else top
    drawLine(Color.White.copy(alpha = 0.18f), Offset(x, bottom), Offset(x, opticalTop), width, StrokeCap.Round)
    if (hasDigital) {
        val dash = 3.dp.toPx()
        drawLine(Theme.warning.copy(alpha = 0.55f), Offset(x, opticalTop - dash), Offset(x, top), width,
                 pathEffect = PathEffect.dashPathEffect(floatArrayOf(dash, dash)))
    }

    // Fill up to the level asked for.
    val levelY = y(level)
    drawLine(Brush.linearGradient(listOf(Theme.purple, Theme.magenta), Offset(x, bottom), Offset(x, levelY)),
             Offset(x, bottom), Offset(x, levelY), width, StrokeCap.Round)

    // Scale
    val marks = if (hasDigital) listOf(1f, 5f, optical, 100f, maxLevel) else listOf(1f, 2f, 5f, 10f, optical)
    for (mark in marks.filter { it <= maxLevel }) {
        val markY = y(mark)
        val boundary = hasDigital && mark == optical
        val color = if (boundary) Theme.warning else Theme.textTertiary
        drawLine(color, Offset(x - (if (boundary) 7 else 5).dp.toPx(), markY),
                 Offset(x + (if (boundary) 7 else -3).dp.toPx(), markY), (if (boundary) 1.5f else 1f).dp.toPx())
        val label = measurer.measure(String.format(Locale.US, "%.0f", mark),
                                     AppFont.figures(9, 600, if (boundary) Theme.warning else Theme.textSecondary))
        drawText(label, topLeft = Offset(x - 9.dp.toPx() - label.size.width, markY - label.size.height / 2f))
    }

    // A dot at each level the thumb can stop on.
    for (detent in detents.filter { it <= maxLevel }) {
        drawCircle(Theme.textTertiary, 1.dp.toPx(), Offset(x + 5.dp.toPx(), y(detent)))
    }

    // Where the lens is while it travels.
    if (actual != null) {
        val ay = y(actual)
        val marker = Path().apply {
            moveTo(x + 5.dp.toPx(), ay)
            lineTo(x + 12.dp.toPx(), ay - 4.5f.dp.toPx())
            lineTo(x + 12.dp.toPx(), ay + 4.5f.dp.toPx())
            close()
        }
        drawPath(marker, Theme.text)
    }
}
