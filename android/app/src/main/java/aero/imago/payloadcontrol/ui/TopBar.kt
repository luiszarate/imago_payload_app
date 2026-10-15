package aero.imago.payloadcontrol.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Explore
import androidx.compose.material.icons.filled.GpsFixed
import androidx.compose.material.icons.filled.SdCard
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Speed
import androidx.compose.material.icons.filled.SportsEsports
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material.icons.outlined.Explore
import androidx.compose.material.icons.outlined.Speed
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.scale
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import java.util.Locale

@Composable
fun TopBar(modifier: Modifier) {
    val model = LocalModel.current
    Row(modifier, verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
        Box(Modifier.padding(end = 4.dp)) { Wordmark(16) }
        if (model.isConnected) {
            LinkPill()
            if (model.camera.recording) RecordingBadge(model.camera.recordingSeconds)
        }
        Spacer(Modifier.weight(1f))
        if (model.isConnected) {
            model.camera.laserRangeMeters?.let {
                StatusChip(String.format(Locale.US, "%.0f m", it), Icons.Filled.GpsFixed)
            }
            model.camera.storageAvailableGB?.let {
                StatusChip(String.format(Locale.US, "%.0f GB", it), Icons.Filled.SdCard)
            }
        }
        model.gamepadName?.let { StatusChip(it, Icons.Filled.SportsEsports, Theme.text) }
        if (model.isConnected) {
            IconPill(if (model.showAttitude) Icons.Filled.Explore else Icons.Outlined.Explore, model.showAttitude,
                     "Gimbal attitude") { model.toggleAttitude() }
            IconPill(if (model.showHUD) Icons.Filled.Speed else Icons.Outlined.Speed, model.showHUD,
                     "Network HUD") { model.showHUD = !model.showHUD }
        }
        IconPill(Icons.Filled.Settings, false, "Settings") { model.showSettings = true }
        IconPill(Icons.Filled.Tune, false, "Connections") { model.showConnections = true }
        if (model.isConnected) {
            IconPill(Icons.Filled.Close, false, "Disconnect") { model.disconnect() }
        }
    }
}

@Composable
private fun LinkPill() {
    val model = LocalModel.current
    val up = model.link.up
    Row(
        Modifier
            .background(Theme.surface.copy(alpha = 0.8f), CircleShape)
            .border(1.dp, Theme.stroke, CircleShape)
            .padding(horizontal = 11.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(7.dp),
    ) {
        Box(Modifier.size(8.dp).background(if (up) Theme.ok else Theme.warning, CircleShape))
        Text(model.connectedProfile?.name ?: "Payload", style = AppFont.display(13, 600), maxLines = 1,
             overflow = TextOverflow.Ellipsis, modifier = Modifier.widthIn(max = 180.dp))
        if (up && model.link.roundTripMs > 0) {
            Text("${model.link.roundTripMs} ms", style = AppFont.figures(12, 500, Theme.textSecondary))
        } else if (!up) {
            Text("waiting for payload", style = AppFont.body(12, 500, Theme.textSecondary), maxLines = 1)
        }
    }
}

@Composable
private fun RecordingBadge(seconds: Int) {
    Row(
        Modifier.background(Theme.danger.copy(alpha = 0.25f), CircleShape).padding(horizontal = 10.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Box(Modifier.size(8.dp).background(Theme.danger, CircleShape))
        Text(String.format(Locale.US, "REC %02d:%02d", seconds / 60, seconds % 60), style = AppFont.figures(12, 700))
    }
}

@Composable
private fun IconPill(icon: ImageVector, active: Boolean, label: String, onClick: () -> Unit) {
    val source = remember { MutableInteractionSource() }
    Box(
        Modifier
            .scale(rememberPressScale(source))
            .size(38.dp, 34.dp)
            .then(if (active) Modifier.background(Theme.accent, CircleShape)
                  else Modifier.background(Theme.surface.copy(alpha = 0.8f), CircleShape))
            .border(1.dp, Theme.stroke, CircleShape)
            .clickable(interactionSource = source, indication = null, role = Role.Button, onClick = onClick)
            .semantics { contentDescription = label },
        contentAlignment = Alignment.Center,
    ) {
        Icon(icon, contentDescription = null, tint = Theme.text, modifier = Modifier.size(17.dp))
    }
}
