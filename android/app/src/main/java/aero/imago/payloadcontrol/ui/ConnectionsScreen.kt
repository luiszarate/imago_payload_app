package aero.imago.payloadcontrol.ui

import aero.imago.payloadcontrol.core.ConnectionProfile
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.RadioButtonUnchecked
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp

/** Saved connection profiles: pick one, edit, add or delete. */
@Composable
fun ConnectionsScreen(onDone: () -> Unit) {
    val model = LocalModel.current
    var editing by remember { mutableStateOf<ConnectionProfile?>(null) }
    var deleting by remember { mutableStateOf<ConnectionProfile?>(null) }

    editing?.let { profile ->
        ProfileEditor(profile, onCancel = { editing = null }) {
            model.saveProfile(it)
            editing = null
        }
        return
    }

    FormPage(
        title = "Connections",
        leading = {
            IconButton(onClick = { editing = ConnectionProfile(name = "New payload") }) {
                Icon(Icons.Filled.Add, contentDescription = "Add connection", tint = Theme.magenta)
            }
        },
        trailing = { BarButton("Done", bold = true, onClick = onDone) },
    ) {
        formSection("profiles", footer = "Changes apply the next time you connect.") {
            model.profiles.forEachIndexed { index, profile ->
                if (index > 0) RowDivider()
                val selected = profile.id == model.selectedProfileId
                FormRow(Modifier.clickable(role = Role.RadioButton) { model.selectProfile(profile.id) }) {
                    Icon(if (selected) Icons.Filled.CheckCircle else Icons.Filled.RadioButtonUnchecked, contentDescription = null,
                         tint = if (selected) Theme.magenta else Theme.textTertiary, modifier = Modifier.size(22.dp))
                    Column(Modifier.weight(1f).padding(start = 12.dp)) {
                        Text(profile.name, style = AppFont.display(16, 600))
                        Text(summary(profile), style = AppFont.figures(12, 500, Theme.textSecondary), maxLines = 1,
                             overflow = TextOverflow.Ellipsis)
                    }
                    IconButton(onClick = { editing = profile }) {
                        Icon(Icons.Filled.Edit, contentDescription = "Edit ${profile.name}", tint = Theme.text)
                    }
                    IconButton(onClick = { deleting = profile }) {
                        Icon(Icons.Filled.Delete, contentDescription = "Delete ${profile.name}", tint = Theme.textSecondary)
                    }
                }
            }
        }
    }

    deleting?.let { profile ->
        AlertDialog(
            onDismissRequest = { deleting = null },
            title = { Text("Delete ${profile.name}?") },
            confirmButton = {
                TextButton(onClick = {
                    model.deleteProfile(profile.id)
                    deleting = null
                }) { Text("Delete", color = Theme.danger) }
            },
            dismissButton = { TextButton(onClick = { deleting = null }) { Text("Cancel") } },
        )
    }
}

private fun summary(profile: ConnectionProfile): String {
    val control = if (profile.mavlinkMode == ConnectionProfile.MavlinkMode.DIRECT) "${profile.payloadIP}:${profile.mavlinkPort}"
                  else "listen ${if (profile.localPort == 0) 14550 else profile.localPort}"
    val video = profile.videoURL.ifEmpty { "video from payload" }
    return "$control · $video"
}

/** Form for one profile. Mirrors Payload Plus: one field for the payload, one for the video. */
@Composable
private fun ProfileEditor(profile: ConnectionProfile, onCancel: () -> Unit, onSave: (ConnectionProfile) -> Unit) {
    var draft by remember(profile.id) { mutableStateOf(profile) }
    val problem = draft.validationError
    FormPage(
        title = draft.name.ifEmpty { "Connection" },
        leading = { BarButton("Cancel", onClick = onCancel) },
        trailing = {
            BarButton("Save", enabled = problem == null, bold = true) {
                onSave(draft.copy(name = draft.name.trim(), payloadIP = draft.payloadIP.trim(), videoURL = draft.videoURL.trim()))
            }
        },
    ) {
        formSection("name", "Name") {
            Field("Name", draft.name) { draft = draft.copy(name = it) }
        }
        formSection(
            "payload", "Payload (MAVLink)",
            if (draft.mavlinkMode == ConnectionProfile.MavlinkMode.DIRECT)
                "The Orus L IP, as entered in Payload Plus. Through Herelink it is usually 192.168.144.x."
            else "Use when Herelink forwards the payload's MAVLink to this device. Commands take about twice as long as with the payload IP.",
        ) {
            ChoiceRow("Control", draft.mavlinkMode, ConnectionProfile.MavlinkMode.entries.map { it to it.title }) {
                draft = draft.copy(mavlinkMode = it)
            }
            RowDivider()
            if (draft.mavlinkMode == ConnectionProfile.MavlinkMode.DIRECT) {
                Field("Payload IP", draft.payloadIP, KeyboardType.Uri) { draft = draft.copy(payloadIP = it) }
                PortField("MAVLink port", draft.mavlinkPort) { draft = draft.copy(mavlinkPort = it) }
            } else {
                PortField("Listen port (0 = 14550)", draft.localPort) { draft = draft.copy(localPort = it) }
            }
        }
        formSection(
            "video", "Video (RTSP)",
            "Leave empty to use the stream the payload announces. Auto tries UDP first and falls back to TCP. If the picture stays behind live for 2 s, for example after the link degrades, the stream restarts to catch up. Smoothing shows frames that arrive in bursts at their own pace, at the cost of up to that much delay; Off shows every frame at once. Frame gaps in the network HUD shows how bursty the link is.",
        ) {
            Field("rtsp://192.168.43.1:8554/fpv_stream", draft.videoURL, KeyboardType.Uri, label = "Video URL") {
                draft = draft.copy(videoURL = it)
            }
            ChoiceRow("Transport", draft.videoTransport, ConnectionProfile.VideoTransport.entries.map { it to it.title }) {
                draft = draft.copy(videoTransport = it)
            }
            RowDivider()
            ChoiceRow("Smoothing", draft.videoSmoothingMs,
                      ConnectionProfile.smoothingChoices.map { it to if (it == 0) "Off" else "$it ms" }) {
                draft = draft.copy(videoSmoothingMs = it)
            }
            RowDivider()
            ChoiceRow("Resync when behind", draft.maxVideoLagMs, ConnectionProfile.videoLagChoices.map {
                it to when {
                    it == 0 -> "Never"
                    it < 1000 -> "$it ms"
                    else -> "${it / 1000} s"
                }
            }) { draft = draft.copy(maxVideoLagMs = it) }
        }
        formSection(
            "advanced", "Advanced",
            "Autopilot gimbal manager: the gimbal is driven through PX4 (MNT_MODE_IN and MNT_MODE_OUT set to MAVLink gimbal v2), which needs the autopilot on this link, e.g. Herelink forwarding. The app takes control once when it connects (not from a ground station that holds it) and gives it back when it disconnects. Keep Wi-Fi awake (experimental) sends a tiny packet 30 times a second to the ground unit so the device's Wi-Fi does not doze and then get video and telemetry in bursts; turn it off if the video stalls.",
        ) {
            ChoiceRow("Gimbal commands", draft.gimbalProtocol, ConnectionProfile.GimbalProtocol.entries.map { it to it.title }) {
                draft = draft.copy(gimbalProtocol = it)
            }
            RowDivider()
            SwitchRow("Set RC mode to standard", draft.setStandardRCMode) { draft = draft.copy(setStandardRCMode = it) }
            RowDivider()
            SwitchRow("Keep Wi-Fi awake (experimental)", draft.keepWifiAwake) { draft = draft.copy(keepWifiAwake = it) }
        }
        if (problem != null) {
            formSection("problem") {
                FormRow {
                    Icon(Icons.Filled.Warning, contentDescription = null, tint = Theme.warning, modifier = Modifier.size(18.dp))
                    Text(problem, style = AppFont.body(14, 600, Theme.warning), modifier = Modifier.padding(start = 8.dp))
                }
            }
        }
    }
}

@Composable
private fun Field(placeholder: String, value: String, keyboard: KeyboardType = KeyboardType.Text, label: String? = null,
                  onChange: (String) -> Unit) {
    OutlinedTextField(
        value = value,
        onValueChange = onChange,
        singleLine = true,
        label = label?.let { { Text(it) } },
        placeholder = { Text(placeholder, color = Theme.textTertiary) },
        keyboardOptions = KeyboardOptions(keyboardType = keyboard, autoCorrectEnabled = false),
        textStyle = AppFont.body(15, 600),
        colors = OutlinedTextFieldDefaults.colors(focusedBorderColor = Theme.magenta, unfocusedBorderColor = Theme.stroke,
                                                  cursorColor = Theme.magenta, focusedLabelColor = Theme.magenta),
        modifier = Modifier.fillMaxWidth().padding(horizontal = 10.dp, vertical = 6.dp),
    )
}

@Composable
private fun PortField(title: String, value: Int, onChange: (Int) -> Unit) {
    FormRow {
        Text(title, style = AppFont.body(15, 600), modifier = Modifier.weight(1f))
        OutlinedTextField(
            value = value.toString(),
            onValueChange = { text -> onChange(text.filter(Char::isDigit).take(5).toIntOrNull() ?: 0) },
            singleLine = true,
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
            textStyle = AppFont.figures(15, 600),
            colors = OutlinedTextFieldDefaults.colors(focusedBorderColor = Theme.magenta, unfocusedBorderColor = Theme.stroke,
                                                      cursorColor = Theme.magenta),
            modifier = Modifier.width(110.dp),
        )
    }
}

