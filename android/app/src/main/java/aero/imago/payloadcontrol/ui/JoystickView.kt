package aero.imago.payloadcontrol.ui

import aero.imago.payloadcontrol.app.AppModel
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.VectorConverter
import androidx.compose.animation.core.spring
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.launch
import kotlin.math.sqrt

/**
 * On-screen joystick for gimbal rate control. Reports deflection with x right and y up, both
 * -1..1, on every touch move, and (0, 0) on release. The release also runs when the gesture is
 * cancelled or the joystick leaves the screen, so the gimbal can never be left turning.
 */
@Composable
fun JoystickView(diameter: Dp, onChange: (x: Float, y: Float) -> Unit) {
    val model = LocalModel.current
    val scope = rememberCoroutineScope()
    val report by rememberUpdatedState(onChange)
    val knob = remember { Animatable(Offset.Zero, Offset.VectorConverter) }
    var active by remember { mutableStateOf(false) }

    DisposableEffect(Unit) {
        onDispose { if (active) report(0f, 0f) }
    }

    Canvas(
        Modifier
            .size(diameter)
            .semantics { contentDescription = "Gimbal joystick" }
            .pointerInput(Unit) {
                val radius = size.width / 2f
                val knobRadius = size.width * 0.18f
                val limit = radius - knobRadius / 2
                awaitEachGesture {
                    val down = awaitFirstDown(requireUnconsumed = false)
                    active = true
                    model.haptics(AppModel.Haptic.TAP)
                    var position = down.position
                    try {
                        while (true) {
                            var dx = position.x - radius
                            var dy = position.y - radius
                            val length = sqrt(dx * dx + dy * dy)
                            if (length > limit) {
                                dx *= limit / length
                                dy *= limit / length
                            }
                            scope.launch { knob.snapTo(Offset(dx, dy)) }
                            report(dx / limit, -dy / limit)
                            val event = awaitPointerEvent()
                            val change = event.changes.firstOrNull { it.id == down.id } ?: break
                            if (!change.pressed) break
                            change.consume()
                            position = change.position
                        }
                    } finally {
                        active = false
                        report(0f, 0f)
                        scope.launch { knob.animateTo(Offset.Zero, spring(Spring.DampingRatioMediumBouncy, Spring.StiffnessHigh)) }
                    }
                }
            },
    ) {
        val radius = size.minDimension / 2
        val c = center
        drawCircle(Theme.surfaceRaised.copy(alpha = 0.85f), radius, c)
        if (active) {
            drawCircle(Theme.accent, radius - 1.dp.toPx(), c, style = Stroke(2.dp.toPx()))
        } else {
            drawCircle(Theme.stroke, radius - 0.5f.dp.toPx(), c, style = Stroke(1.dp.toPx()))
        }
        // Axis guides
        val guide = radius * 0.7f
        drawLine(Theme.stroke, Offset(c.x, c.y - guide), Offset(c.x, c.y + guide), 1.dp.toPx())
        drawLine(Theme.stroke, Offset(c.x - guide, c.y), Offset(c.x + guide, c.y), 1.dp.toPx())
        val knobCenter = c + knob.value
        val knobRadius = size.minDimension * 0.18f
        if (active) {
            drawCircle(Brush.radialGradient(listOf(Theme.magenta.copy(alpha = 0.5f), Theme.magenta.copy(alpha = 0f)),
                                            knobCenter, knobRadius * 1.8f), knobRadius * 1.8f, knobCenter)
        }
        drawCircle(Brush.linearGradient(listOf(Theme.magenta, Theme.fuchsia, Theme.purple),
                                        knobCenter - Offset(knobRadius, knobRadius),
                                        knobCenter + Offset(knobRadius, knobRadius)), knobRadius, knobCenter)
    }
}
