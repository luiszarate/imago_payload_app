package aero.imago.payloadcontrol.ui

import aero.imago.payloadcontrol.core.CameraSettingsCatalog
import aero.imago.payloadcontrol.core.StreamCamera
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.scale
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.delay
import java.util.Locale

/** The settings the Payload panel shows that the payload reports back (range finder, OSD). */
private val PAYLOAD_PANEL_SECTIONS = CameraSettingsCatalog.image.filter { it.id == "lrf" || it.id == "osd" }

/**
 * The payload settings an operator changes in flight, one tap from the top bar (the range
 * finder's chip): how often the laser range finder measures, the payload's OSD, and the stream
 * the Orus L serves itself. Each choice goes to the payload at once, through the same actions as
 * Settings. Until 2026-11-19 they were only in Settings, the range finder under eight other
 * sections.
 */
@Composable
fun PayloadPanel() {
    val model = LocalModel.current
    val camera = model.streamCamera
    LaunchedEffect(Unit) {
        model.readSettings(PAYLOAD_PANEL_SECTIONS)
        delay(2000)
        model.readSettings(PAYLOAD_PANEL_SECTIONS, onlyMissing = true)
    }
    Column(Modifier.width(360.dp).padding(horizontal = 16.dp, vertical = 8.dp),
           verticalArrangement = Arrangement.spacedBy(14.dp)) {
        Text("PAYLOAD", style = AppFont.display(13, 700, tracking = 1.2.sp))

        for (setting in PAYLOAD_PANEL_SECTIONS.flatMap { it.settings }) {
            val value = model.cameraValues[setting.id]
            Choices(
                if (setting.id == CameraSettingsCatalog.LRF_MODE) "LASER RANGE FINDER" else "PAYLOAD OSD",
                setting.options.map { it.label },
                selected = setting.options.indexOfFirst { it.value == value }.takeIf { it >= 0 },
            ) { model.set(setting, setting.options[it].value) }
        }

        Choices("THE ORUS L'S OWN STREAM", StreamCamera.entries.map { it.title },
                selected = StreamCamera.entries.indexOf(camera), needsLink = false) {
            model.streamCamera = StreamCamera.entries[it]
        }
        // The measured rate is filled while the HUD is open; without it, no "now".
        val kbps = model.metrics.kbps
        val bitrates = CameraSettingsCatalog.bitrates
        Choices(
            "BITRATE, Mb/s" + if (kbps > 0) String.format(Locale.US, " · NOW %.1f", kbps / 1000) else "",
            bitrates.map { "${it / 1_000_000}" },
            selected = model.streamBitrateAsked[camera]?.let { bitrates.indexOf(it) }?.takeIf { it >= 0 },
        ) { model.setStreamBitrate(bitrates[it]) }
        Choices("PICTURE SIZE", model.streamResolutions(camera), selected = model.streamLevelAsked[camera]) {
            model.setStreamResolution(it)
        }
        Text("Stream: for video from the Orus L itself, not a Herelink's. It reports these back to nobody: the lit choice is what was asked in this session.",
             style = AppFont.body(11, 500, Theme.textSecondary))
    }
}

/**
 * A row of choices under a caption, the current one lit; a tap sends at once. Without the link
 * the payload's choices wait (the stream's camera is only this app's).
 */
@Composable
private fun Choices(
    title: String,
    labels: List<String>,
    selected: Int?,
    needsLink: Boolean = true,
    onPick: (Int) -> Unit,
) {
    val enabled = !needsLink || LocalModel.current.link.up
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Text(title, style = AppFont.display(10, 700, Theme.textTertiary, 1.sp), maxLines = 1)
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            labels.forEachIndexed { index, label ->
                val lit = index == selected
                val source = remember { MutableInteractionSource() }
                Box(
                    Modifier
                        .weight(1f)
                        .scale(rememberPressScale(source))
                        .alpha(if (enabled) 1f else 0.5f)
                        .heightIn(min = 38.dp)
                        .fill(lit, RoundedCornerShape(10.dp))
                        .clickable(interactionSource = source, indication = null, enabled = enabled,
                                   role = Role.Button) { onPick(index) }
                        .semantics { this.selected = lit },
                    contentAlignment = Alignment.Center,
                ) {
                    Text(label, style = AppFont.display(12, 600), maxLines = 1, overflow = TextOverflow.Clip,
                         modifier = Modifier.padding(horizontal = 4.dp))
                }
            }
        }
    }
}
