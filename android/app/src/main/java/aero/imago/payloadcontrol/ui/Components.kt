package aero.imago.payloadcontrol.ui

import aero.imago.payloadcontrol.app.AppModel
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.waitForUpOrCancellation
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.clickable
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.scale
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/** The app model, provided once at the root (like SwiftUI's environment on iOS). */
val LocalModel = staticCompositionLocalOf<AppModel> { error("No AppModel") }

/** Background of a control: the brand gradient when active, raised surface otherwise. */
fun Modifier.fill(active: Boolean, shape: androidx.compose.ui.graphics.Shape): Modifier =
    if (active) background(Theme.accent, shape) else background(Theme.surfaceRaised, shape)

/**
 * Takes every touch on this element, so a tap between the buttons of a panel does not reach
 * the video below it (where it would start tracking).
 */
fun Modifier.blockTouches(): Modifier = pointerInput(Unit) {
    awaitPointerEventScope {
        while (true) awaitPointerEvent()
    }
}

/** Shrinks slightly while pressed. */
@Composable
fun rememberPressScale(source: MutableInteractionSource): Float {
    val pressed by source.collectIsPressedAsState()
    return if (pressed) 0.94f else 1f
}

/** Square icon button for the side rails. Targets are at least 52 dp for gloved use in the field. */
@Composable
fun RailButton(
    icon: ImageVector,
    label: String?,
    modifier: Modifier = Modifier,
    active: Boolean = false,
    tint: Color = Theme.text,
    width: Dp = 56.dp,
    height: Dp = 52.dp,
    onClick: () -> Unit,
) {
    val source = remember { MutableInteractionSource() }
    val shape = RoundedCornerShape(13.dp)
    Column(
        modifier = modifier
            .scale(rememberPressScale(source))
            .size(width, height)
            .fill(active, shape)
            .clickable(interactionSource = source, indication = null, role = Role.Button, onClick = onClick)
            .semantics { if (label != null) contentDescription = label },
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Icon(icon, contentDescription = null, tint = if (active) Theme.text else tint,
             modifier = Modifier.size(20.dp))
        if (label != null) {
            Text(label.uppercase(), style = AppFont.display(10, 600), maxLines = 1,
                 overflow = TextOverflow.Clip, modifier = Modifier.padding(top = 3.dp, start = 2.dp, end = 2.dp))
        }
    }
}

/**
 * Acts while held: zoom in/out. Fires onPress once and onRelease once, also when the gesture is
 * cancelled or the button leaves the screen, so a zoom can never be left running.
 */
@Composable
fun HoldButton(
    icon: ImageVector,
    description: String,
    onPress: () -> Unit,
    onRelease: () -> Unit,
    height: Dp = 52.dp,
) {
    val model = LocalModel.current
    var pressed by remember { mutableStateOf(false) }
    val press by rememberUpdatedState(onPress)
    val release by rememberUpdatedState(onRelease)
    val scope = rememberCoroutineScope()
    DisposableEffect(Unit) {
        onDispose { if (pressed) release() }
    }
    val shape = RoundedCornerShape(13.dp)
    Box(
        modifier = Modifier
            .scale(if (pressed) 0.94f else 1f)
            .size(56.dp, height)
            .fill(pressed, shape)
            .pointerInput(Unit) {
                awaitEachGesture {
                    awaitFirstDown(requireUnconsumed = false)
                    pressed = true
                    model.haptics(AppModel.Haptic.TAP)
                    press()
                    try {
                        waitForUpOrCancellation()
                    } finally {
                        pressed = false
                        release()
                    }
                }
            }
            .semantics {
                role = Role.Button
                contentDescription = description
                // Accessibility services cannot hold: act for half a second.
                onClick {
                    press()
                    scope.launch {
                        delay(500)
                        release()
                    }
                    true
                }
            },
        contentAlignment = Alignment.Center,
    ) {
        Icon(icon, contentDescription = null, tint = Theme.text, modifier = Modifier.size(20.dp))
    }
}

/** Small rounded label for status values in the top bar. */
@Composable
fun StatusChip(text: String, icon: ImageVector? = null, color: Color = Theme.textSecondary) {
    Row(
        modifier = Modifier
            .background(Theme.surfaceRaised.copy(alpha = 0.9f), CircleShape)
            .padding(horizontal = 9.dp, vertical = 5.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (icon != null) {
            Icon(icon, contentDescription = null, tint = color, modifier = Modifier.size(12.dp))
            Box(Modifier.size(5.dp))
        }
        Text(text, style = AppFont.figures(12, 600, color), maxLines = 1)
    }
}

/** "IMAGO" wordmark in the brand gradient, until the official logo is added. */
@Composable
fun Wordmark(size: Int = 17) {
    Text(
        "IMAGO",
        style = AppFont.display(size, 700).copy(brush = Theme.accent, letterSpacing = (size * 0.22f).sp),
        modifier = Modifier.semantics { contentDescription = "Imago Aerospace" },
    )
}
