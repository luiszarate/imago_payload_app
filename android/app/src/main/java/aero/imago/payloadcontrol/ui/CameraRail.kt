package aero.imago.payloadcontrol.ui

import aero.imago.payloadcontrol.core.CameraMode
import aero.imago.payloadcontrol.core.ViewSource
import androidx.compose.animation.core.animateDpAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Layers
import androidx.compose.material.icons.filled.PhotoCamera
import androidx.compose.material.icons.filled.Videocam
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
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
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

/**
 * Left rail: capture mode, shutter, view source and zoom. On a phone in landscape the zoom goes
 * in a second column, since one column is taller than the screen.
 */
@Composable
fun CameraRail() {
    val compact = LocalCompact.current
    val modifier = Modifier.panel().blockTouches().padding(if (compact) 6.dp else 8.dp)
    if (compact) {
        Row(modifier, verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            CaptureColumn(compact)
            ZoomControl(compact = true)
        }
    } else {
        Column(modifier, horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(10.dp)) {
            CaptureColumn(compact)
            ZoomControl(compact = false)
        }
    }
}

@Composable
private fun CaptureColumn(compact: Boolean) {
    Column(horizontalAlignment = Alignment.CenterHorizontally,
           verticalArrangement = Arrangement.spacedBy(if (compact) 6.dp else 10.dp)) {
        ModeToggle(compact)
        Shutter(compact)
        ViewMenu(compact)
    }
}

@Composable
private fun ModeToggle(compact: Boolean) {
    Row(
        Modifier.background(Theme.surfaceRaised, RoundedCornerShape(12.dp)).padding(2.dp),
        horizontalArrangement = Arrangement.spacedBy(2.dp),
    ) {
        ModeButton(CameraMode.PHOTO, Icons.Filled.PhotoCamera, "Photo mode", compact)
        ModeButton(CameraMode.VIDEO, Icons.Filled.Videocam, "Video mode", compact)
    }
}

@Composable
private fun ModeButton(mode: CameraMode, icon: ImageVector, label: String, compact: Boolean) {
    val model = LocalModel.current
    val selected = model.captureMode == mode
    val shape = RoundedCornerShape(10.dp)
    Box(
        Modifier
            .size(26.dp, if (compact) 26.dp else 30.dp)
            .then(if (selected) Modifier.background(Theme.accent, shape) else Modifier)
            .clickable(role = Role.Button) { model.chooseCaptureMode(mode) }
            .semantics {
                contentDescription = label
                this.selected = selected
            },
        contentAlignment = Alignment.Center,
    ) {
        Icon(icon, contentDescription = null, tint = if (selected) Theme.text else Theme.textSecondary,
             modifier = Modifier.size(14.dp))
    }
}

@Composable
private fun Shutter(compact: Boolean) {
    val model = LocalModel.current
    val size: Dp = if (compact) 58.dp else 68.dp
    val recording = model.camera.recording
    val video = model.captureMode == CameraMode.VIDEO
    val source = remember { MutableInteractionSource() }
    val inner by animateDpAsState(
        if (video) (if (recording) size * 0.38f else size * 0.72f) else size * 0.76f, tween(150), label = "shutter")
    val corner by animateDpAsState(if (video && recording) 6.dp else size, tween(150), label = "corner")
    Box(
        Modifier
            .scale(rememberPressScale(source))
            .size(size)
            .border(3.dp, Theme.text, CircleShape)
            .clickable(interactionSource = source, indication = null, role = Role.Button) { model.shutter() }
            .semantics {
                contentDescription = if (video) (if (recording) "Stop recording" else "Start recording") else "Take photo"
            },
        contentAlignment = Alignment.Center,
    ) {
        Box(Modifier.size(inner).background(if (video) Theme.danger else Theme.text, RoundedCornerShape(corner)))
    }
}

@Composable
private fun ViewMenu(compact: Boolean) {
    val model = LocalModel.current
    var open by remember { mutableStateOf(false) }
    Box {
        RailButton(Icons.Filled.Layers, model.camera.view?.title ?: "View", height = if (compact) 46.dp else 52.dp) {
            open = true
        }
        DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            for (source in ViewSource.entries) {
                MenuItem(source.title, model.camera.view == source) {
                    open = false
                    model.setView(source)
                }
            }
        }
    }
}

/** A menu row with a check mark on the current value. */
@Composable
fun MenuItem(title: String, checked: Boolean, onClick: () -> Unit) {
    DropdownMenuItem(
        text = { Text(title, style = AppFont.body(15, 600), maxLines = 1, overflow = TextOverflow.Ellipsis) },
        onClick = onClick,
        leadingIcon = {
            if (checked) Icon(Icons.Filled.Check, contentDescription = null, tint = Theme.magenta)
            else Box(Modifier.size(24.dp))
        },
    )
}

