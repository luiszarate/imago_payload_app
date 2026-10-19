package aero.imago.payloadcontrol.ui

import aero.imago.payloadcontrol.control.GimbalInputShaper
import aero.imago.payloadcontrol.core.CameraSetting
import aero.imago.payloadcontrol.core.CameraSettingSection
import aero.imago.payloadcontrol.core.CameraSettingsCatalog
import aero.imago.payloadcontrol.app.MarkedPoint
import aero.imago.payloadcontrol.core.WebIsotherm
import android.content.Context
import android.content.Intent
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Map
import androidx.compose.material.icons.filled.TableChart
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.TextButton
import androidx.compose.runtime.remember
import androidx.compose.ui.platform.LocalContext
import androidx.core.content.FileProvider
import java.io.File
import java.text.DateFormat
import java.text.SimpleDateFormat
import java.util.TimeZone
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.delay
import java.util.Locale
import kotlin.math.roundToInt

private enum class SettingsPage(val title: String, val sections: List<CameraSettingSection>) {
    CAMERA("Camera", CameraSettingsCatalog.image),
    THERMAL("Thermal & stream", CameraSettingsCatalog.infrared),
    GIMBAL("Gimbal", CameraSettingsCatalog.gimbal),
    CONTROLLER("Controller", emptyList()),
}

/**
 * Camera, thermal, stream and gimbal settings of the payload, plus the controls of this app.
 * Payload settings are read when a page opens and written the moment they change.
 */
@Composable
fun SettingsScreen(onDone: () -> Unit) {
    val model = LocalModel.current
    var page by rememberSaveable { mutableStateOf(SettingsPage.CAMERA) }
    var showMarkedPoints by rememberSaveable { mutableStateOf(false) }

    LaunchedEffect(page, model.link.up) {
        // Again when the link comes (back) up; then once more for what stayed silent.
        if (!model.link.up) return@LaunchedEffect
        model.readSettings(page.sections)
        delay(2000)
        model.readSettings(page.sections, onlyMissing = true)
    }
    LaunchedEffect(page) { model.learnGamepadInput(null) }
    DisposableEffect(Unit) { onDispose { model.learnGamepadInput(null) } }

    if (showMarkedPoints) {
        BackHandler { showMarkedPoints = false }
        MarkedPointsPage(onBack = { showMarkedPoints = false })
        return
    }

    FormPage(
        title = "Settings",
        trailing = { BarButton("Done", bold = true, onClick = onDone) },
        header = { PageTabs(page) { page = it } },
    ) {
        when (page) {
            SettingsPage.CONTROLLER -> gamepadSections(model)
            else -> {
                if (page == SettingsPage.CAMERA) {
                    displaySection()
                    markedPointsSection { showMarkedPoints = true }
                    if (model.isConnected) payloadServiceSection()
                }
                if (page == SettingsPage.THERMAL) logSection()
                if (model.isConnected) {
                    for (section in page.sections) payloadSection(section)
                    if (page == SettingsPage.THERMAL) {
                        webIsothermSection()
                        streamSection()
                    }
                } else {
                    formSection("offline") {
                        FormRow {
                            Text("Connect to the payload to see and change its settings.",
                                 style = AppFont.body(14, 500, Theme.textSecondary))
                        }
                    }
                }
                if (page == SettingsPage.GIMBAL) joystickSections()
            }
        }
    }
}

/** Segmented control for the pages. */
@Composable
private fun PageTabs(selected: SettingsPage, onSelect: (SettingsPage) -> Unit) {
    Row(Modifier.fillMaxWidth().background(Theme.surfaceRaised, RoundedCornerShape(10.dp)).padding(2.dp)) {
        for (page in SettingsPage.entries) {
            val active = page == selected
            Box(
                Modifier
                    .weight(1f)
                    .height(34.dp)
                    .then(if (active) Modifier.background(Theme.accent, RoundedCornerShape(8.dp)) else Modifier)
                    .clickable(role = Role.Tab) { onSelect(page) },
                contentAlignment = Alignment.Center,
            ) {
                Text(page.title, style = AppFont.body(13, if (active) 700 else 600, if (active) Theme.text else Theme.textSecondary),
                     textAlign = TextAlign.Center, maxLines = 1)
            }
        }
    }
}

/** One group of payload settings. Until the payload answers, a setting shows a dash. */
private fun LazyListScope.payloadSection(section: CameraSettingSection) {
    formSection(section.id, section.title, section.footer) {
        val model = LocalModel.current
        section.settings.forEachIndexed { index, setting ->
            if (index > 0) RowDivider()
            PayloadSettingRow(setting, model.cameraValues[setting.id])
        }
    }
}

@Composable
private fun PayloadSettingRow(setting: CameraSetting, value: Int?) {
    val model = LocalModel.current
    val reading: @Composable () -> Unit = {
        if (value == null) {
            CircularProgressIndicator(color = Theme.textSecondary, strokeWidth = 1.5.dp,
                                      modifier = Modifier.padding(start = 8.dp).size(12.dp))
        }
    }
    when (val kind = setting.kind) {
        is CameraSetting.Kind.Toggle ->
            SwitchRow(setting.title, value == kind.on, setting.note, titleExtra = reading) {
                model.set(setting, if (it) kind.on else kind.off)
            }
        is CameraSetting.Kind.Choice ->
            ChoiceRow(setting.title, value, kind.options.map { it.value to it.label }, setting.note, reading) {
                model.set(setting, it)
            }
    }
}

private fun LazyListScope.displaySection() {
    formSection(
        "display", "Display",
        "The payload draws its OSD down the left edge of the picture. With this on, the picture starts right of the camera controls: a little smaller on a tablet, only moved over on a phone.",
    ) {
        val model = LocalModel.current
        SwitchRow("Keep the picture clear of the camera rail", model.videoClearOfCameraRail) {
            model.updateVideoClearOfCameraRail(it)
        }
    }
}

private fun LazyListScope.markedPointsSection(onOpen: () -> Unit) {
    formSection(
        "markedPoints",
        footer = "Mark the target from its coordinates in the top bar. The list is kept on this device and exports as CSV or KML.",
    ) {
        val model = LocalModel.current
        FormRow(Modifier.clickable(role = Role.Button, onClick = onOpen)) {
            Text("Marked points", style = AppFont.body(15, 600), modifier = Modifier.weight(1f))
            Text("${model.markedPoints.size}", style = AppFont.figures(13, 500, Theme.textSecondary))
            Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, contentDescription = null, tint = Theme.textTertiary,
                 modifier = Modifier.padding(start = 4.dp).size(18.dp))
        }
    }
}

private fun LazyListScope.logSection() {
    formSection(
        "log", "MAVLink log",
        "Off: the log keeps what the payload says and is told, its streams once a second, and stays small. On: every message on the link and the app's video and link measurements, for chasing link problems; it grows quickly. Record a log from the Network HUD.",
    ) {
        val model = LocalModel.current
        SwitchRow("Detailed log (link debugging)", model.detailedLog) { model.updateDetailedLog(it) }
    }
}

/**
 * The isotherm on the payload's web settings page (our Orus L has it only there). Although the
 * page speaks of a restart, enabling it and changing the threshold show at once on saving
 * (field test 2026-10-19), so there is no restart here.
 */
private fun LazyListScope.webIsothermSection() {
    formSection(
        "webIsotherm", "Isotherm (payload web settings)",
        "The payload's own isotherm: one threshold in °C, e.g. 30 for people. Saved on the payload's web page; it shows in the picture right away.",
    ) {
        val model = LocalModel.current
        val current = model.webIsotherm
        var draft by remember { mutableStateOf(current ?: WebIsotherm(false, 35, 0)) }
        LaunchedEffect(Unit) { if (model.webIsotherm == null) model.readWebIsotherm() }
        LaunchedEffect(current) { if (current != null) draft = current }
        if (current == null) {
            FormRow {
                Text(if (model.webBusy) "Reading the payload's web page…" else "Not read",
                     style = AppFont.body(15, 500, Theme.textSecondary), modifier = Modifier.weight(1f))
                if (model.webBusy) {
                    CircularProgressIndicator(color = Theme.textSecondary, strokeWidth = 2.dp, modifier = Modifier.size(18.dp))
                } else {
                    BarButton("Read") { model.readWebIsotherm() }
                }
            }
        } else {
            SwitchRow("Isotherm", draft.enabled) { draft = draft.copy(enabled = it) }
            RowDivider()
            Box(Modifier.padding(horizontal = 14.dp, vertical = 6.dp)) {
                TemperatureRow("Threshold", draft.thresholdC, WebIsotherm.THRESHOLD_LIMITS) {
                    draft = draft.copy(thresholdC = it)
                }
            }
            RowDivider()
            ChoiceRow("Measurement range", draft.measurementRange, listOf(0 to "-20 to 150 °C", 1 to "-20 to 550 °C")) {
                draft = draft.copy(measurementRange = it)
            }
            RowDivider()
            val enabled = !model.webBusy && draft != current
            FormRow(Modifier.clickable(enabled = enabled, role = Role.Button) { model.saveWebIsotherm(draft) }) {
                Text("Save to payload", style = AppFont.body(15, 600, if (enabled) Theme.magenta else Theme.textTertiary),
                     modifier = Modifier.weight(1f))
                if (model.webBusy) {
                    CircularProgressIndicator(color = Theme.textSecondary, strokeWidth = 2.dp, modifier = Modifier.size(18.dp))
                }
            }
        }
    }
}

/**
 * Restarts the payload's camera software through its web page, behind a confirmation: for a
 * tracker that stops obeying in long sessions.
 */
private fun LazyListScope.payloadServiceSection() {
    formSection(
        "payloadService", "Payload",
        "When tracking stops obeying after a long session. Video, tracking and control drop for a while and come back by themselves.",
    ) {
        val model = LocalModel.current
        var confirm by remember { mutableStateOf(false) }
        FormRow(Modifier.clickable(enabled = !model.webBusy, role = Role.Button) { confirm = true }) {
            Text("Restart camera service…", style = AppFont.body(15, 600, if (model.webBusy) Theme.textTertiary else Theme.danger))
        }
        if (confirm) {
            AlertDialog(
                onDismissRequest = { confirm = false },
                title = { Text("Restart the camera service?") },
                text = { Text("Video and control drop while it restarts. Better with the aircraft in a safe state.") },
                confirmButton = {
                    TextButton(onClick = {
                        confirm = false
                        model.restartPayloadService()
                    }) { Text("Restart now", color = Theme.danger) }
                },
                dismissButton = { TextButton(onClick = { confirm = false }) { Text("Cancel") } },
            )
        }
    }
}

/** The marked target points: newest first, delete with a confirmation, share as CSV or KML. */
@Composable
private fun MarkedPointsPage(onBack: () -> Unit) {
    val model = LocalModel.current
    val context = LocalContext.current
    var deleting by remember { mutableStateOf<MarkedPoint?>(null) }
    FormPage(
        title = "Marked points",
        leading = { BarButton("Settings", onClick = onBack) },
        trailing = {},
    ) {
        if (model.markedPoints.isEmpty()) {
            formSection("empty") {
                FormRow {
                    Text("No points yet. Tap the target's coordinates in the top bar, then Mark point.",
                         style = AppFont.body(14, 500, Theme.textSecondary))
                }
            }
        } else {
            formSection("points") {
                model.markedPoints.asReversed().forEachIndexed { index, point ->
                    if (index > 0) RowDivider()
                    FormRow {
                        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(3.dp)) {
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Text(point.name, style = AppFont.display(14, 600), modifier = Modifier.weight(1f))
                                Text(localTime(point.time), style = AppFont.figures(12, 500, Theme.textSecondary))
                            }
                            SelectionContainer {
                                Text(String.format(Locale.US, "%s · alt %.0f m", point.position.coordinateText,
                                                   point.position.altitude),
                                     style = AppFont.figures(13, 500))
                            }
                        }
                        IconButton(onClick = { deleting = point }) {
                            Icon(Icons.Filled.Delete, contentDescription = "Delete ${point.name}", tint = Theme.textSecondary)
                        }
                    }
                }
            }
            formSection("share") {
                FormRow(Modifier.clickable(role = Role.Button) { shareMarkedPoints(context, model.exportMarkedPoints(kml = true)) }) {
                    Icon(Icons.Filled.Map, contentDescription = null, tint = Theme.magenta, modifier = Modifier.size(20.dp))
                    Text("Share as KML (maps)", style = AppFont.body(15, 600, Theme.magenta), modifier = Modifier.padding(start = 12.dp))
                }
                RowDivider()
                FormRow(Modifier.clickable(role = Role.Button) { shareMarkedPoints(context, model.exportMarkedPoints(kml = false)) }) {
                    Icon(Icons.Filled.TableChart, contentDescription = null, tint = Theme.magenta, modifier = Modifier.size(20.dp))
                    Text("Share as CSV (spreadsheet)", style = AppFont.body(15, 600, Theme.magenta), modifier = Modifier.padding(start = 12.dp))
                }
            }
        }
    }
    deleting?.let { point ->
        AlertDialog(
            onDismissRequest = { deleting = null },
            title = { Text("Delete ${point.name}?") },
            confirmButton = {
                TextButton(onClick = {
                    model.deleteMarkedPoint(point.id)
                    deleting = null
                }) { Text("Delete", color = Theme.danger) }
            },
            dismissButton = { TextButton(onClick = { deleting = null }) { Text("Cancel") } },
        )
    }
}

/** The stored UTC time, shown in the device's time zone. */
private fun localTime(iso: String): String {
    val parser = SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss'Z'", Locale.US).apply { timeZone = TimeZone.getTimeZone("UTC") }
    val date = runCatching { parser.parse(iso) }.getOrNull() ?: return iso
    return DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.MEDIUM).format(date)
}

private fun shareMarkedPoints(context: Context, file: File?) {
    if (file == null) return
    val uri = FileProvider.getUriForFile(context, "${context.packageName}.files", file)
    val send = Intent(Intent.ACTION_SEND).apply {
        type = if (file.extension == "kml") "application/vnd.google-earth.kml+xml" else "text/csv"
        putExtra(Intent.EXTRA_STREAM, uri)
        putExtra(Intent.EXTRA_SUBJECT, file.name)
        addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
    }
    context.startActivity(Intent.createChooser(send, "Share marked points"))
}

private fun LazyListScope.streamSection() {
    formSection(
        "stream", "Stream",
        "Through Herelink, a lower bitrate loses fewer packets, and lost packets freeze the picture until the next key frame. The payload does not report its setting, so the figure is the bitrate measured now. (Unverified on the Orus L: that camera 0 is the EO stream.)",
    ) {
        val model = LocalModel.current
        ChoiceRow(
            "Video bitrate · now ${String.format(Locale.US, "%.1f", model.metrics.kbps / 1000)} Mb/s",
            null as Int?,
            CameraSettingsCatalog.bitrates.map { it to "${it / 1_000_000} Mb/s" },
        ) { model.setStreamBitrate(it) }
    }
}

private fun LazyListScope.joystickSections() {
    formSection(
        "sticks", "Sticks and on-screen joystick",
        "Speed at full deflection and 1× zoom; it slows down as you zoom in. A finer curve leaves more of the stick's travel for slow, precise moves.",
    ) {
        val model = LocalModel.current
        val t = model.gimbalTuning
        SpeedRow("Pan speed", t.maxYawDps) { model.updateGimbalTuning(t.copy(maxYawDps = it)) }
        SpeedRow("Tilt speed", t.maxPitchDps) { model.updateGimbalTuning(t.copy(maxPitchDps = it)) }
        SliderRow("Curve", curveLabel(t.expo), t.expo, 0f..1f, 0.05f, "Linear", "Fine") {
            model.updateGimbalTuning(t.copy(expo = it))
        }
        SliderRow("Dead zone", "${(t.deadzone * 100).roundToInt()} %", t.deadzone, GimbalInputShaper.deadzoneRange, 0.01f) {
            model.updateGimbalTuning(t.copy(deadzone = it))
        }
    }
    formSection(
        "digital", "D-pad and buttons",
        "A D-pad or button is either off or fully on, so it moves at this fixed speed. The gimbal smooths its own start and stop, and the link adds a delay, so it keeps turning briefly after release: a lower speed stops closer to where you let go.",
    ) {
        val model = LocalModel.current
        val t = model.gimbalTuning
        SpeedRow("Pan speed", t.digitalYawDps) { model.updateGimbalTuning(t.copy(digitalYawDps = it)) }
        SpeedRow("Tilt speed", t.digitalPitchDps) { model.updateGimbalTuning(t.copy(digitalPitchDps = it)) }
        RowDivider()
        FormRow(Modifier.clickable(role = Role.Button) { model.updateGimbalTuning(GimbalInputShaper()) }) {
            Text("Restore defaults", style = AppFont.body(15, 600, Theme.magenta))
        }
    }
}

@Composable
private fun SpeedRow(title: String, value: Float, onChange: (Float) -> Unit) {
    SliderRow(title, "${value.roundToInt()} °/s", value, GimbalInputShaper.speedRange, 1f, onChange = onChange)
}

private fun curveLabel(expo: Float) = when {
    expo < 0.15f -> "Linear"
    expo < 0.45f -> "Soft"
    expo < 0.75f -> "Medium"
    else -> "Fine"
}
