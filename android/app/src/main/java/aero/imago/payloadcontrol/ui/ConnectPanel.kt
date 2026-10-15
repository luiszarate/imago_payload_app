package aero.imago.payloadcontrol.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.UnfoldMore
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.scale
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import aero.imago.payloadcontrol.core.ConnectionProfile

/** Shown while there is no session or no picture yet. */
@Composable
fun ConnectPanel(modifier: Modifier) {
    val model = LocalModel.current
    val compact = LocalCompact.current
    Column(
        modifier.panel(22.dp).blockTouches().padding(if (compact) 20.dp else 28.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(if (compact) 12.dp else 16.dp),
    ) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Wordmark(26)
            Text("Payload Control", style = AppFont.display(15, 500, Theme.textSecondary))
        }
        if (model.isConnected) {
            CircularProgressIndicator(color = Theme.magenta, strokeWidth = 3.dp, modifier = Modifier.size(28.dp))
            Text(model.videoMessage ?: "Connecting…", style = AppFont.body(14, 500, Theme.textSecondary),
                 textAlign = TextAlign.Center, modifier = Modifier.widthIn(max = 380.dp))
            Text(if (model.link.up) "Payload link up" else "Waiting for the payload…",
                 style = AppFont.body(13, 600, if (model.link.up) Theme.ok else Theme.warning))
        } else {
            ProfilePicker()
            model.connectError?.let {
                Text(it, style = AppFont.body(13, 600, Theme.danger), textAlign = TextAlign.Center,
                     modifier = Modifier.widthIn(max = 360.dp))
            }
            val source = remember { MutableInteractionSource() }
            val enabled = model.selectedProfile != null
            Box(
                Modifier
                    .scale(rememberPressScale(source))
                    .alpha(if (enabled) 1f else 0.4f)
                    .size(220.dp, 50.dp)
                    .background(Theme.accent, RoundedCornerShape(14.dp))
                    .clickable(interactionSource = source, indication = null, enabled = enabled, role = Role.Button) {
                        model.connect()
                    },
                contentAlignment = Alignment.Center,
            ) {
                Text("Connect", style = AppFont.display(16, 700))
            }
        }
    }
}

@Composable
private fun ProfilePicker() {
    val model = LocalModel.current
    var open by remember { mutableStateOf(false) }
    Box {
        Row(
            Modifier
                .size(280.dp, 54.dp)
                .background(Theme.surfaceRaised, RoundedCornerShape(12.dp))
                .clickable(role = Role.Button) { open = true }
                .padding(horizontal = 14.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(Modifier.weight(1f)) {
                val profile = model.selectedProfile
                Text(profile?.name ?: "Choose a connection", style = AppFont.display(15, 600), maxLines = 1)
                if (profile != null) {
                    Text(if (profile.mavlinkMode == ConnectionProfile.MavlinkMode.DIRECT) profile.payloadIP
                         else "Listening for MAVLink",
                         style = AppFont.figures(12, 500, Theme.textSecondary))
                }
            }
            Icon(Icons.Filled.UnfoldMore, contentDescription = null, tint = Theme.textSecondary)
        }
        DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            for (profile in model.profiles) {
                MenuItem(profile.name, profile.id == model.selectedProfileId) {
                    open = false
                    model.selectProfile(profile.id)
                }
            }
            HorizontalDivider(color = Theme.stroke)
            DropdownMenuItem(text = { Text("Edit connections…", style = AppFont.body(15, 600)) }, onClick = {
                open = false
                model.showConnections = true
            })
        }
    }
}
