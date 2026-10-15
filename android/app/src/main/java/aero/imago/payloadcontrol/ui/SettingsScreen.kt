package aero.imago.payloadcontrol.ui

import aero.imago.payloadcontrol.control.GimbalInputShaper
import aero.imago.payloadcontrol.core.CameraSetting
import aero.imago.payloadcontrol.core.CameraSettingSection
import aero.imago.payloadcontrol.core.CameraSettingsCatalog
import androidx.compose.foundation.background
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

    LaunchedEffect(page, model.link.up) {
        // Again when the link comes (back) up; then once more for what stayed silent.
        if (!model.link.up) return@LaunchedEffect
        model.readSettings(page.sections)
        delay(2000)
        model.readSettings(page.sections, onlyMissing = true)
    }
    LaunchedEffect(page) { model.learnGamepadInput(null) }
    DisposableEffect(Unit) { onDispose { model.learnGamepadInput(null) } }

    FormPage(
        title = "Settings",
        trailing = { BarButton("Done", bold = true, onClick = onDone) },
        header = { PageTabs(page) { page = it } },
    ) {
        when (page) {
            SettingsPage.CONTROLLER -> gamepadSections(model)
            else -> {
                if (model.isConnected) {
                    for (section in page.sections) payloadSection(section)
                    if (page == SettingsPage.THERMAL) streamSection()
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
