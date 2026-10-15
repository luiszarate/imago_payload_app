package aero.imago.payloadcontrol.ui

import aero.imago.payloadcontrol.core.GimbalControl
import aero.imago.payloadcontrol.core.GimbalMode
import aero.imago.payloadcontrol.core.TrackingState
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AutoAwesome
import androidx.compose.material.icons.filled.Cancel
import androidx.compose.material.icons.filled.ControlCamera
import androidx.compose.material.icons.filled.FilterCenterFocus
import androidx.compose.material.icons.filled.TouchApp
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.scale
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import java.util.Locale

/** Right rail: gimbal mode, recenter, tracking tools and the joystick. */
@Composable
fun GimbalRail() {
    val model = LocalModel.current
    val compact = LocalCompact.current
    val spacing = if (compact) 6.dp else 10.dp
    Column(
        Modifier.panel().blockTouches().padding(if (compact) 6.dp else 8.dp).width(if (compact) 132.dp else 148.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(spacing),
    ) {
        if (model.usesGimbalManager) ControlButton()
        Row(horizontalArrangement = Arrangement.spacedBy(if (compact) 6.dp else 8.dp)) {
            ModeMenu()
            RailButton(Icons.Filled.FilterCenterFocus, "Center") { model.recenterGimbal() }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(if (compact) 6.dp else 8.dp)) {
            RailButton(Icons.Filled.AutoAwesome, "AI", active = model.detectionEnabled) { model.toggleDetection() }
            RailButton(Icons.Filled.TouchApp, "Point", active = model.pointMode) { model.pointMode = !model.pointMode }
        }
        if (model.tracking.state != TrackingState.IDLE) {
            val source = remember { MutableInteractionSource() }
            Row(
                Modifier
                    .scale(rememberPressScale(source))
                    .fillMaxWidth()
                    .heightIn(min = 34.dp)
                    .background(Theme.danger.copy(alpha = 0.85f), RoundedCornerShape(11.dp))
                    .clickable(interactionSource = source, indication = null, role = Role.Button) { model.stopTracking() },
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.Center,
            ) {
                Icon(Icons.Filled.Cancel, contentDescription = null, tint = Theme.text, modifier = Modifier.size(15.dp))
                Text(if (model.tracking.state == TrackingState.LOST) " Lost · Stop" else " Stop track",
                     style = AppFont.display(12, 700))
            }
        }
        Box(Modifier.padding(top = 2.dp)) {
            JoystickView(if (compact) 112.dp else 136.dp) { x, y -> model.joystickMoved(x, y) }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            val gimbal = model.gimbal
            Text(if (gimbal.valid) String.format(Locale.US, "P %+.1f°", gimbal.pitch) else "P —",
                 style = AppFont.figures(12, 600, Theme.textSecondary))
            Text(gimbal.yaw?.let { String.format(Locale.US, "Y %+.1f°", it) } ?: "Y —",
                 style = AppFont.figures(12, 600, Theme.textSecondary))
        }
    }
}

@Composable
private fun ModeMenu() {
    val model = LocalModel.current
    var open by remember { mutableStateOf(false) }
    Box {
        RailButton(Icons.Filled.ControlCamera, model.gimbal.mode?.title ?: "Mode") { open = true }
        DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            for (mode in listOf(GimbalMode.LOCK, GimbalMode.FOLLOW, GimbalMode.MAPPING, GimbalMode.OFF)) {
                MenuItem(mode.title, model.gimbal.mode == mode) {
                    open = false
                    model.setGimbalMode(mode)
                }
            }
        }
    }
}

/** Who drives the gimbal through the autopilot; a tap takes or gives back control. */
@Composable
private fun ControlButton() {
    val model = LocalModel.current
    val ours = model.gimbal.control == GimbalControl.ThisApp
    val source = remember { MutableInteractionSource() }
    Column(
        Modifier
            .scale(rememberPressScale(source))
            .fillMaxWidth()
            .heightIn(min = 38.dp)
            .background((if (ours) Theme.ok else Theme.warning).copy(alpha = if (ours) 0.25f else 0.3f),
                        RoundedCornerShape(11.dp))
            .clickable(interactionSource = source, indication = null, role = Role.Button) { model.toggleGimbalControl() }
            .semantics { contentDescription = if (ours) "Release gimbal control" else "Take gimbal control" }
            .padding(vertical = 3.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Text(if (ours) "IN CONTROL" else "TAKE CONTROL", style = AppFont.display(11, 700))
        Text(if (ours) "Tap to release" else model.gimbal.control.holder,
             style = AppFont.body(10, 500, Theme.textSecondary), maxLines = 1)
    }
}
