package aero.imago.payloadcontrol.ui

import aero.imago.payloadcontrol.app.IsothermBand
import aero.imago.payloadcontrol.core.IRPalette
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.waitForUpOrCancellation
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Autorenew
import androidx.compose.material.icons.filled.Remove
import androidx.compose.material.icons.filled.Thermostat
import androidx.compose.material.icons.filled.UnfoldMore
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.scale
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.disabled
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import java.util.Locale
import kotlin.math.abs

/**
 * Thermal controls along the bottom while the IR picture is the main one (view IR or IR + EO):
 * palette, the picture's temperature scale, IR zoom, flat-field correction and, when the camera
 * has it, the isotherm. On a tablet it sits in the black band under the 16:9 picture.
 */
@Composable
fun ThermalBar(modifier: Modifier) {
    val model = LocalModel.current
    val compact = LocalCompact.current
    val thermal = model.thermal
    val palette = model.irPalette
    val colors = remember(palette, thermal.cameraType) { paletteColors(palette ?: 0, thermal.cameraType) }
    Row(
        modifier
            .height(thermalBarHeight(compact))
            .panel(16.dp)
            .blockTouches()
            .padding(horizontal = if (compact) 5.dp else 8.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(if (compact) 6.dp else 10.dp),
    ) {
        PaletteMenu(palette, thermal.cameraType, colors, compact)
        TemperatureScale(
            colors, thermal.minC, thermal.maxC, thermal.meanC,
            band = if (model.isothermOn) model.isothermBand else null,
            compact = compact,
            modifier = Modifier.weight(1f).widthIn(min = if (compact) 90.dp else 140.dp),
        )
        ZoomStepper(compact)
        FFCButton(thermal.ffcAuto, compact)
        if (model.isothermSupported) IsothermButton(compact)
    }
}

fun thermalBarHeight(compact: Boolean): Dp = if (compact) 52.dp else 68.dp

private fun buttonHeight(compact: Boolean): Dp = if (compact) 42.dp else 52.dp

private fun paletteColors(index: Int, cameraType: Int): List<Color> {
    val colors = IRPalette.colors(index, cameraType).map { Color(it or 0xFF000000.toInt()) }
    // A gradient needs two colours.
    return if (colors.size >= 2) colors else listOf(Color.Black, Color.White)
}

// MARK: Palette

@Composable
private fun PaletteMenu(current: Int?, cameraType: Int, colors: List<Color>, compact: Boolean) {
    val model = LocalModel.current
    var open by remember { mutableStateOf(false) }
    val name = current?.let { IRPalette.name(it, cameraType) }
    Box {
        Row(
            Modifier
                .width(if (compact) 118.dp else 150.dp)
                .height(buttonHeight(compact))
                .background(Theme.surfaceRaised, RoundedCornerShape(12.dp))
                .clickable(role = Role.Button) { open = true }
                .semantics {
                    contentDescription = "IR palette"
                    stateDescription = name ?: "Unknown"
                }
                .padding(horizontal = if (compact) 8.dp else 10.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(7.dp),
        ) {
            val swatch = RoundedCornerShape(4.dp)
            Box(
                Modifier
                    .size(if (compact) 12.dp else 14.dp, if (compact) 26.dp else 32.dp)
                    // Cold at the bottom, hot at the top.
                    .background(Brush.verticalGradient(colors.reversed()), swatch)
                    .border(1.dp, Theme.stroke, swatch),
            )
            Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(1.dp)) {
                Text("PALETTE", style = AppFont.display(if (compact) 8 else 9, 700, Theme.textTertiary, 1.sp),
                     maxLines = 1)
                Text(name ?: "—", style = AppFont.display(if (compact) 12 else 14, 600), maxLines = 1,
                     overflow = TextOverflow.Ellipsis)
            }
            Icon(Icons.Filled.UnfoldMore, contentDescription = null, tint = Theme.textSecondary,
                 modifier = Modifier.size(14.dp))
        }
        DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            for (index in 0 until IRPalette.COUNT) {
                MenuItem(IRPalette.name(index, cameraType), current == index) {
                    open = false
                    model.setIRPalette(index)
                }
            }
        }
    }
}

// MARK: Zoom

@Composable
private fun ZoomStepper(compact: Boolean) {
    val model = LocalModel.current
    val zoom = model.irZoom
    Row(
        Modifier.height(buttonHeight(compact)).background(Theme.surfaceRaised, RoundedCornerShape(12.dp)),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        StepButton(Icons.Filled.Remove, "IR zoom out", (zoom ?: 1) > 1, compact) { model.stepIRZoom(-1) }
        Text(zoom?.let { "$it×" } ?: "—", style = AppFont.figures(if (compact) 13 else 15, 600), maxLines = 1,
             modifier = Modifier.semantics { contentDescription = "IR zoom ${zoom ?: 1} times" })
        StepButton(Icons.Filled.Add, "IR zoom in", (zoom ?: 1) < 8, compact) { model.stepIRZoom(1) }
    }
}

@Composable
private fun StepButton(icon: ImageVector, label: String, enabled: Boolean, compact: Boolean, onClick: () -> Unit) {
    val source = remember { MutableInteractionSource() }
    Box(
        Modifier
            .scale(rememberPressScale(source))
            .size(if (compact) 34.dp else 42.dp, buttonHeight(compact))
            .clickable(interactionSource = source, indication = null, enabled = enabled, role = Role.Button,
                       onClick = onClick)
            .semantics { contentDescription = label },
        contentAlignment = Alignment.Center,
    ) {
        Icon(icon, contentDescription = null, tint = if (enabled) Theme.text else Theme.textTertiary,
             modifier = Modifier.size(if (compact) 16.dp else 18.dp))
    }
}

// MARK: FFC

/** Tap: recalibrate now. Long press: a menu to calibrate or switch automatic FFC. */
@Composable
private fun FFCButton(auto: Boolean?, compact: Boolean) {
    val model = LocalModel.current
    var open by remember { mutableStateOf(false) }
    val source = remember { MutableInteractionSource() }
    Box {
        BarLabel(
            Icons.Filled.Autorenew, "FFC", auto?.let { if (it) "AUTO" else "MAN" } ?: " ", compact,
            modifier = Modifier
                .scale(rememberPressScale(source))
                .combinedClickable(
                    interactionSource = source, indication = null, role = Role.Button,
                    onClickLabel = "Recalibrate the thermal sensor",
                    onLongClickLabel = "Automatic or manual",
                    onLongClick = { open = true },
                    onClick = { model.triggerFFC() },
                )
                .semantics { contentDescription = "Flat-field correction" },
        )
        DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            DropdownMenuItem(
                text = { Text("Calibrate now", style = AppFont.body(15, 600)) },
                onClick = {
                    open = false
                    model.triggerFFC()
                },
                leadingIcon = { Icon(Icons.Filled.Autorenew, contentDescription = null, tint = Theme.text) },
            )
            DropdownMenuItem(
                text = { Text("Automatic FFC", style = AppFont.body(15, 600)) },
                onClick = {
                    open = false
                    model.setFFCAuto(auto != true)
                },
                trailingIcon = {
                    Switch(
                        checked = auto == true, onCheckedChange = null,
                        colors = SwitchDefaults.colors(checkedTrackColor = Theme.magenta, checkedThumbColor = Theme.text),
                    )
                },
            )
        }
    }
}

// MARK: Isotherm

@Composable
private fun IsothermButton(compact: Boolean) {
    val model = LocalModel.current
    val on = model.isothermOn
    val band = model.isothermBand
    var open by remember { mutableStateOf(false) }
    val source = remember { MutableInteractionSource() }
    Box {
        BarLabel(
            Icons.Filled.Thermostat, "ISO", if (on) "${band.first}–${band.last}°" else "OFF", compact, active = on,
            modifier = Modifier
                .scale(rememberPressScale(source))
                .clickable(interactionSource = source, indication = null, role = Role.Button) { open = true }
                .semantics {
                    contentDescription = "Isotherm"
                    stateDescription = if (on) "${band.first} to ${band.last} degrees" else "Off"
                },
        )
        DropdownMenu(expanded = open, onDismissRequest = { open = false }, containerColor = Theme.surface) {
            IsothermPanel(band)
        }
    }
}

@Composable
private fun BarLabel(
    icon: ImageVector,
    title: String,
    detail: String,
    compact: Boolean,
    modifier: Modifier,
    active: Boolean = false,
) {
    val shape = RoundedCornerShape(12.dp)
    Column(
        modifier.size(if (compact) 50.dp else 60.dp, buttonHeight(compact)).fill(active, shape),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Icon(icon, contentDescription = null, tint = Theme.text, modifier = Modifier.size(if (compact) 15.dp else 18.dp))
        Text(title, style = AppFont.display(if (compact) 9 else 10, 700), maxLines = 1)
        Text(detail, style = AppFont.figures(if (compact) 8 else 9, 600, if (active) Theme.text else Theme.textSecondary),
             maxLines = 1, overflow = TextOverflow.Clip)
    }
}

/** The picture's temperature range on the palette's colours: coldest on the left, hottest on
 *  the right, a tick at the mean and the isotherm band outlined where it falls. */
@Composable
private fun TemperatureScale(
    colors: List<Color>,
    minC: Float?,
    maxC: Float?,
    meanC: Float?,
    band: IntRange?,
    compact: Boolean,
    modifier: Modifier,
) {
    // Where a temperature sits on the scale, 0..1; null without a range.
    fun fraction(value: Float?): Float? {
        if (value == null || minC == null || maxC == null || maxC <= minC) return null
        return ((value - minC) / (maxC - minC)).coerceIn(0f, 1f)
    }
    // The isotherm band on the scale, null when it lies wholly outside the picture's range.
    val bandSpan = if (band != null && minC != null && maxC != null && maxC > minC &&
                       band.last > minC && band.first < maxC) {
        fraction(band.first.toFloat())!!..fraction(band.last.toFloat())!!
    } else null
    val mean = fraction(meanC)
    Column(
        modifier.semantics(mergeDescendants = true) {
            contentDescription = "Temperature in the picture"
            stateDescription = if (minC == null) "Not reported"
                               else "from ${celsius(minC)} to ${celsius(maxC)}, mean ${celsius(meanC)}"
        },
        verticalArrangement = Arrangement.spacedBy(if (compact) 3.dp else 5.dp),
    ) {
        Canvas(Modifier.fillMaxWidth().height(if (compact) 8.dp else 11.dp)) {
            val corner = CornerRadius(3.dp.toPx())
            drawRoundRect(Brush.horizontalGradient(colors), cornerRadius = corner)
            drawRoundRect(Theme.stroke, cornerRadius = corner, style = Stroke(1.dp.toPx()))
            if (bandSpan != null) {
                val left = bandSpan.start * size.width
                drawRoundRect(Theme.danger, topLeft = Offset(left, 0f),
                              size = Size(maxOf(4.dp.toPx(), (bandSpan.endInclusive - bandSpan.start) * size.width),
                                          size.height),
                              cornerRadius = corner, style = Stroke(2.dp.toPx()))
            }
            if (mean != null) {
                val x = mean * size.width
                val overhang = 3.dp.toPx()
                drawLine(Color.Black.copy(alpha = 0.6f), Offset(x, -overhang), Offset(x, size.height + overhang),
                         4.dp.toPx(), StrokeCap.Round)
                drawLine(Theme.text, Offset(x, -overhang), Offset(x, size.height + overhang), 2.dp.toPx(),
                         StrokeCap.Round)
            }
        }
        val style = AppFont.figures(if (compact) 10 else 12, 600)
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(celsius(minC), style = style, maxLines = 1)
            Spacer(Modifier.weight(1f).widthIn(min = 2.dp))
            Text(meanC?.let { "MEAN " + celsius(it) } ?: "", style = style.copy(color = Theme.textSecondary),
                 maxLines = 1, overflow = TextOverflow.Clip)
            Spacer(Modifier.weight(1f).widthIn(min = 2.dp))
            Text(celsius(maxC), style = style, maxLines = 1)
        }
    }
}

private fun celsius(value: Float?): String {
    if (value == null) return "—"
    return String.format(Locale.US, if (abs(value) < 100) "%.1f°" else "%.0f°", value)
}

/** Isotherm settings: the band to colour, presets, the IR gain and on/off. */
@Composable
private fun IsothermPanel(initial: IntRange) {
    val model = LocalModel.current
    var low by remember { mutableIntStateOf(initial.first) }
    var high by remember { mutableIntStateOf(initial.last) }
    Column(Modifier.width(330.dp).padding(horizontal = 16.dp, vertical = 8.dp),
           verticalArrangement = Arrangement.spacedBy(14.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("ISOTHERM", style = AppFont.display(13, 700, tracking = 1.2.sp), modifier = Modifier.weight(1f))
            Switch(
                checked = model.isothermOn,
                onCheckedChange = { on -> if (on) model.applyIsotherm(low..high) else model.disableIsotherm() },
                colors = SwitchDefaults.colors(checkedTrackColor = Theme.magenta, checkedThumbColor = Theme.text),
                modifier = Modifier.semantics { contentDescription = "Isotherm" },
            )
        }
        Text("Colours everything between the two temperatures from black to red, over the palette.",
             style = AppFont.body(12, 500, Theme.textSecondary))

        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            for (preset in IsothermBand.PRESETS) {
                val source = remember { MutableInteractionSource() }
                Column(
                    Modifier
                        .weight(1f)
                        .scale(rememberPressScale(source))
                        .heightIn(min = 46.dp)
                        .background(Theme.surfaceRaised, RoundedCornerShape(11.dp))
                        .clickable(interactionSource = source, indication = null, role = Role.Button) {
                            low = preset.range.first
                            high = preset.range.last
                            model.applyIsotherm(preset.range)
                        }
                        .padding(vertical = 6.dp),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.Center,
                ) {
                    Text(preset.name, style = AppFont.display(12, 700), maxLines = 1)
                    Text("${preset.range.first}–${preset.range.last} °C", style = AppFont.figures(10, 500, Theme.textSecondary),
                         maxLines = 1)
                }
            }
        }

        TemperatureRow("From", low, IsothermBand.LIMITS.first..(high - 1)) { low = it }
        TemperatureRow("To", high, (low + 1)..IsothermBand.LIMITS.last) { high = it }

        val lowGain = model.irLowGain
        if (lowGain != null) {
            Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Text("GAIN", style = AppFont.display(10, 700, Theme.textTertiary, 1.sp))
                Row(Modifier.fillMaxWidth().background(Theme.surfaceRaised, RoundedCornerShape(10.dp)).padding(2.dp)) {
                    GainSegment("High · to 150 °C", !lowGain) { model.setIRLowGain(false) }
                    GainSegment("Low · to 550 °C", lowGain) { model.setIRLowGain(true) }
                }
            }
        }
        if (high > IsothermBand.HIGH_GAIN_MAX && lowGain != true) {
            Text("Above 150 °C the camera switches to low gain.", style = AppFont.body(11, 500, Theme.warning))
        }

        val source = remember { MutableInteractionSource() }
        Box(
            Modifier
                .scale(rememberPressScale(source))
                .fillMaxWidth()
                .heightIn(min = 46.dp)
                .background(Theme.accent, RoundedCornerShape(12.dp))
                .clickable(interactionSource = source, indication = null, role = Role.Button) {
                    model.applyIsotherm(low..high)
                },
            contentAlignment = Alignment.Center,
        ) {
            Text(if (model.isothermOn) "Apply $low–$high °C" else "Turn on $low–$high °C",
                 style = AppFont.display(14, 700))
        }
    }
}

@Composable
private fun RowScope.GainSegment(title: String, selected: Boolean, onClick: () -> Unit) {
    Box(
        Modifier
            .weight(1f)
            .height(32.dp)
            .then(if (selected) Modifier.background(Theme.accent, RoundedCornerShape(8.dp)) else Modifier)
            .clickable(role = Role.Tab) { if (!selected) onClick() }
            .semantics { this.selected = selected },
        contentAlignment = Alignment.Center,
    ) {
        Text(title, style = AppFont.body(12, if (selected) 700 else 600, if (selected) Theme.text else Theme.textSecondary),
             maxLines = 1)
    }
}

@Composable
private fun TemperatureRow(title: String, value: Int, range: IntRange, onChange: (Int) -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(title, style = AppFont.body(14, 600, Theme.textSecondary), modifier = Modifier.weight(1f))
        RepeatButton(Icons.Filled.Remove, "$title lower", value > range.first) {
            onChange(maxOf(range.first, value - 1))
        }
        Text("$value °C", style = AppFont.figures(16, 600), maxLines = 1,
             modifier = Modifier.width(70.dp).padding(start = 12.dp))
        RepeatButton(Icons.Filled.Add, "$title higher", value < range.last) {
            onChange(minOf(range.last, value + 1))
        }
    }
}

/** A stepper button that repeats while held, faster the longer it is held. */
@Composable
private fun RepeatButton(icon: ImageVector, label: String, enabled: Boolean, onStep: () -> Unit) {
    val step by rememberUpdatedState(onStep)
    val canStep by rememberUpdatedState(enabled)
    var pressed by remember { mutableStateOf(false) }
    val scope = rememberCoroutineScope()
    Box(
        Modifier
            .scale(if (pressed) 0.94f else 1f)
            .size(44.dp, 40.dp)
            .background(Theme.surfaceRaised, RoundedCornerShape(10.dp))
            .pointerInput(Unit) {
                awaitEachGesture {
                    awaitFirstDown()
                    if (!canStep) return@awaitEachGesture
                    pressed = true
                    step()
                    val repeat = scope.launch {
                        delay(REPEAT_DELAY_MS)
                        var interval = REPEAT_INTERVAL_MS
                        while (canStep) {
                            step()
                            delay(interval)
                            interval = maxOf(REPEAT_FASTEST_MS, interval - 10)
                        }
                    }
                    try {
                        waitForUpOrCancellation()
                    } finally {
                        repeat.cancel()
                        pressed = false
                    }
                }
            }
            .semantics {
                role = Role.Button
                contentDescription = label
                if (!enabled) disabled()
                onClick {
                    if (canStep) step()
                    canStep
                }
            },
        contentAlignment = Alignment.Center,
    ) {
        Icon(icon, contentDescription = null, tint = if (enabled) Theme.text else Theme.textTertiary,
             modifier = Modifier.size(18.dp))
    }
}

private const val REPEAT_DELAY_MS = 400L
private const val REPEAT_INTERVAL_MS = 120L
private const val REPEAT_FASTEST_MS = 40L
