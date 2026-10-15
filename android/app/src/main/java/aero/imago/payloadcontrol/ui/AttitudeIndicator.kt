package aero.imago.payloadcontrol.ui

import aero.imago.payloadcontrol.core.GimbalStatus
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextMeasurer
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import java.util.Locale
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sin

/**
 * Gimbal attitude against the aircraft, seen from above.
 *
 * The aircraft always points up. The cone shows where the camera looks, as wide as its field of
 * view. The ring is a compass when the aircraft heading is known (it turns as the aircraft
 * turns); otherwise its ticks are relative to the nose. The arc on the right is the camera's
 * pitch against the horizon, from 30° up to straight down.
 */
@Composable
fun AttitudeIndicator(modifier: Modifier) {
    val model = LocalModel.current
    val compact = LocalCompact.current
    val gimbal = model.gimbal
    val diameter = if (compact) 92.dp else 136.dp
    val measurer = rememberTextMeasurer()
    val pitch = if (gimbal.valid) gimbal.pitch else null
    val heading = model.vehicleHeading
    val fov = model.camera.horizontalFOV
    Column(
        modifier
            .panel(18.dp)
            .padding(horizontal = if (compact) 8.dp else 10.dp, vertical = if (compact) 6.dp else 8.dp)
            .semantics {
                contentDescription = "Gimbal attitude"
                stateDescription = accessibilityText(gimbal)
            },
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(if (compact) 4.dp else 6.dp),
    ) {
        Canvas(Modifier.size(diameter + MARGIN * 2)) {
            drawDial(measurer, pitch, gimbal.yaw, gimbal.yawNorth, heading, fov)
        }
        Row(horizontalArrangement = Arrangement.spacedBy(if (compact) 10.dp else 14.dp)) {
            Readout("PITCH", if (gimbal.valid) signed(gimbal.pitch) else "—", compact)
            // The pan runs to ±320°, so the operator sees the gimbal winding towards its stop;
            // the wrapped yaw only until the core has seen it turn.
            Readout("YAW", (gimbal.pan ?: gimbal.yaw)?.let(::signed) ?: "—", compact, warning = gimbal.panNearLimit)
            gimbal.yawNorth?.let { Readout("BRG", String.format(Locale.US, "%03d°", it.roundToInt() % 360), compact) }
        }
    }
}

@Composable
private fun Readout(label: String, value: String, compact: Boolean, warning: Boolean = false) {
    Row(horizontalArrangement = Arrangement.spacedBy(4.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label, style = AppFont.display(if (compact) 9 else 10, 700, Theme.textTertiary).copy(letterSpacing = 1.sp))
        Text(value, style = AppFont.figures(if (compact) 12 else 13, 600, if (warning) Theme.warning else Theme.text))
    }
}

private fun signed(value: Float): String {
    val degrees = value.roundToInt()
    return if (degrees == 0) "0°" else String.format(Locale.US, "%+d°", degrees)
}

private fun accessibilityText(gimbal: GimbalStatus): String {
    if (!gimbal.valid) return "No data"
    val parts = mutableListOf("pitch ${gimbal.pitch.roundToInt()} degrees")
    (gimbal.pan ?: gimbal.yaw)?.let {
        val degrees = it.roundToInt()
        parts += if (degrees == 0) "looking forward"
                 else "yaw ${abs(degrees)} degrees ${if (degrees > 0) "right" else "left"}"
    }
    if (gimbal.panNearLimit) parts += "near the pan limit"
    gimbal.yawNorth?.let { parts += "bearing ${it.roundToInt()}" }
    return parts.joinToString(", ")
}

/** Room around the compass for the pitch arc and its marker. */
private val MARGIN = 16.dp
private const val PITCH_TOP = 30f
private const val PITCH_BOTTOM = -90f

/** Fixed-wing / VTOL silhouette seen from above, nose up. Right half; x is mirrored. */
private val AIRCRAFT_OUTLINE = listOf(
    Offset(0f, 1f),          // nose
    Offset(0.07f, 0.86f),
    Offset(0.08f, 0.32f),    // wing root, leading edge
    Offset(1f, 0.07f),       // wing tip
    Offset(1f, -0.07f),
    Offset(0.08f, 0.02f),    // wing root, trailing edge
    Offset(0.06f, -0.6f),    // tail root, leading edge
    Offset(0.4f, -0.76f),    // tailplane tip
    Offset(0.4f, -0.86f),
    Offset(0.05f, -0.84f),
    Offset(0f, -0.92f),      // tail
)

/** Point at `radius` from `center`, `degrees` clockwise from straight up. */
private fun point(center: Offset, radius: Float, degrees: Float): Offset {
    val a = Math.toRadians(degrees.toDouble())
    return Offset(center.x + radius * sin(a).toFloat(), center.y - radius * cos(a).toFloat())
}

private fun DrawScope.drawDial(
    measurer: TextMeasurer, pitch: Float?, yaw: Float?, yawNorth: Float?, heading: Float?, fieldOfView: Float?,
) {
    val c = center
    val radius = min(size.width, size.height) / 2 - MARGIN.toPx()

    // Heading up when the camera's direction against the nose is known, North up when only its
    // bearing is (the aircraft's orientation is then unknown and it is not drawn).
    val showAircraft = yaw != null || yawNorth == null
    val ringRotation = if (showAircraft) heading ?: 0f else 0f
    val compass = if (showAircraft) heading != null else true
    val coneAngle = yaw ?: yawNorth

    // Face
    drawCircle(Theme.background.copy(alpha = 0.55f), radius, c)
    drawCircle(Theme.stroke, radius, c, style = Stroke(1.dp.toPx()))

    drawRing(measurer, c, radius, ringRotation, compass)
    if (coneAngle != null) drawCone(c, radius, coneAngle, fieldOfView)
    if (showAircraft) drawAircraft(c, radius * 0.42f, dimmed = pitch == null)

    // Lubber line: the aircraft's nose (or North) on the ring.
    val lubber = Path().apply {
        moveTo(c.x, c.y - radius + 7.dp.toPx())
        lineTo(c.x - 4.dp.toPx(), c.y - radius - 1.dp.toPx())
        lineTo(c.x + 4.dp.toPx(), c.y - radius - 1.dp.toPx())
        close()
    }
    drawPath(lubber, Theme.text)

    drawPitchArc(c, radius + 9.dp.toPx(), pitch)
}

private fun DrawScope.drawRing(measurer: TextMeasurer, c: Offset, radius: Float, rotation: Float, compass: Boolean) {
    for (bearing in 0 until 360 step 10) {
        if (compass && bearing % 90 == 0) continue // letters instead
        val major = bearing % 30 == 0
        val screen = bearing - rotation
        drawLine(if (major) Theme.textSecondary else Theme.textTertiary,
                 point(c, radius - 2.dp.toPx(), screen),
                 point(c, radius - (if (major) 9 else 5).dp.toPx(), screen),
                 (if (major) 1.5f else 1f).dp.toPx())
    }
    if (!compass) return
    val fontPx = max(9.dp.toPx(), radius * 0.15f)
    for ((bearing, letter) in listOf(0 to "N", 90 to "E", 180 to "S", 270 to "W")) {
        val text = measurer.measure(letter, AppFont.display(10, 700, if (bearing == 0) Theme.magenta else Theme.textSecondary)
            .copy(fontSize = (fontPx / density / fontScale).sp))
        val at = point(c, radius - fontPx * 0.8f, bearing - rotation)
        drawText(text, topLeft = Offset(at.x - text.size.width / 2f, at.y - text.size.height / 2f))
    }
}

private fun DrawScope.drawCone(c: Offset, radius: Float, angle: Float, fieldOfView: Float?) {
    val half = ((fieldOfView ?: 30f) / 2).coerceIn(3f, 40f)
    val length = radius * 0.86f
    val wedge = Path().apply {
        moveTo(c.x, c.y)
        val start = point(c, length, angle - half)
        lineTo(start.x, start.y)
        arcTo(Rect(c, length), angle - half - 90, half * 2, false)
        close()
    }
    drawPath(wedge, Brush.radialGradient(listOf(Theme.magenta.copy(alpha = 0.6f), Theme.fuchsia.copy(alpha = 0.12f)),
                                         c, length))
    drawPath(wedge, Theme.magenta.copy(alpha = 0.9f), style = Stroke(1.dp.toPx()))
    drawLine(Theme.magenta, c, point(c, radius - 3.dp.toPx(), angle), 2.dp.toPx(), StrokeCap.Round)
}

private fun DrawScope.drawAircraft(c: Offset, size: Float, dimmed: Boolean) {
    val right = AIRCRAFT_OUTLINE
    val left = right.reversed().drop(1).dropLast(1).map { Offset(-it.x, it.y) }
    val shape = Path()
    (right + left).forEachIndexed { index, p ->
        val x = c.x + p.x * size
        val y = c.y - p.y * size
        if (index == 0) shape.moveTo(x, y) else shape.lineTo(x, y)
    }
    shape.close()
    drawPath(shape, Theme.text.copy(alpha = if (dimmed) 0.35f else 0.92f))
    drawPath(shape, Theme.background.copy(alpha = 0.7f), style = Stroke(1.dp.toPx()))
}

private fun DrawScope.drawPitchArc(c: Offset, radius: Float, pitch: Float?) {
    // Pitch θ (up positive) sits at θ above the 3 o'clock direction, like a side view.
    fun arcPoint(degrees: Float, r: Float): Offset {
        val a = Math.toRadians(degrees.toDouble())
        return Offset(c.x + r * cos(a).toFloat(), c.y - r * sin(a).toFloat())
    }
    val bounds = Rect(c, radius)
    val width = 3.dp.toPx()
    drawArc(Color.White.copy(alpha = 0.18f), -PITCH_TOP, PITCH_TOP - PITCH_BOTTOM, false, bounds.topLeft, bounds.size,
            style = Stroke(width, cap = StrokeCap.Round))
    for (degrees in listOf(0f, -45f, -90f)) {
        drawLine(Theme.textSecondary, arcPoint(degrees, radius + 3.dp.toPx()),
                 arcPoint(degrees, radius + (if (degrees == 0f) 8 else 6).dp.toPx()), 1.5f.dp.toPx())
    }
    pitch ?: return
    val value = pitch.coerceIn(PITCH_BOTTOM, PITCH_TOP)
    if (value != 0f) {
        drawArc(Brush.linearGradient(listOf(Theme.magenta, Theme.purple), arcPoint(0f, radius), arcPoint(value, radius)),
                -max(value, 0f), abs(value), false, bounds.topLeft, bounds.size, style = Stroke(width, cap = StrokeCap.Round))
    }
    val marker = arcPoint(value, radius)
    drawCircle(Theme.magenta, 5.dp.toPx(), marker)
    drawCircle(Theme.text, 5.dp.toPx(), marker, style = Stroke(1.5f.dp.toPx()))
}
