package aero.imago.payloadcontrol.ui

import aero.imago.payloadcontrol.app.MapController
import aero.imago.payloadcontrol.map.MapSettings
import aero.imago.payloadcontrol.map.MapStyle
import aero.imago.payloadcontrol.map.TileMath
import aero.imago.payloadcontrol.map.TileStore
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import java.util.Locale

// Settings › Map: the map on the picture, and the tiles it needs, downloaded beforehand since
// the payload's Wi-Fi has no internet.

internal fun LazyListScope.mapSections() {
    formSection(
        "mapShow", "Map",
        "A small north-up map in the bottom right corner of the picture: the aircraft in the middle, turned to its heading, the camera's direction and the payload's target point. It needs the aircraft's position (Aircraft telemetry in the connection, or a forwarded link). Tap the map for its buttons.",
    ) {
        val map = LocalModel.current.map
        val settings = map.settings
        SwitchRow("Show the map", settings.visible) { map.update(settings.copy(visible = it)) }
        RowDivider()
        ChoiceRow("Style", settings.style, MapStyle.entries.map { it to it.title }) { map.update(settings.copy(style = it)) }
    }
    formSection(
        "mapDownload", "Offline map",
        "The map on the picture only uses tiles stored here. Download the flight area before the flight, on a network with internet (not the payload's Wi-Fi). Zoom levels ${TileMath.DOWNLOAD_MIN_ZOOM} to ${TileMath.DOWNLOAD_TOP_ZOOM} (${TileMath.DETAIL_TOP_ZOOM} with Closest zoom too), a few tiles a second, at most ${TileStore.MAX_DOWNLOAD} per map: a larger area leaves out its closest zooms. The area starts at where the aircraft was last seen.",
    ) {
        DownloadArea()
    }
    formSection("mapStored", "Stored on this device") {
        StoredMaps()
    }
    formSection(
        "mapServers", "Tile servers",
        "Addresses with {z}, {x} and {y}, and the credit drawn on the map. Use a provider whose terms allow keeping its tiles for offline use, and download modest areas.",
    ) {
        TileServers()
    }
}

@Composable
private fun DownloadArea() {
    val model = LocalModel.current
    val map = model.map
    val settings = map.settings
    // The aircraft's position this run, else where it was last seen.
    val start = map.aircraftPosition?.let { it.latitude to it.longitude } ?: settings.lastPosition
    var latitude by remember { mutableStateOf(start?.let { coordinate(it.first) } ?: "") }
    var longitude by remember { mutableStateOf(start?.let { coordinate(it.second) } ?: "") }
    var radius by remember { mutableStateOf(String.format(Locale.US, "%.1f", settings.downloadRadiusKm)) }
    var problem by remember { mutableStateOf<String?>(null) }

    // Finite only: "NaN" or "Infinity" must never reach the tile maths.
    val lat = latitude.trim().toDoubleOrNull()?.takeIf { it.isFinite() }
    val lon = longitude.trim().toDoubleOrNull()?.takeIf { it.isFinite() }
    val km = radius.trim().toFloatOrNull()?.takeIf { it.isFinite() }
    val areaProblem = if (lat == null || lon == null || km == null) "Enter the latitude, longitude and radius."
                      else MapController.problem(lat, lon, km)
    val detail = settings.downloadDetail
    val plan = remember(lat, lon, km, areaProblem, detail) {
        if (areaProblem == null && lat != null && lon != null && km != null) {
            TileMath.plan(lat, lon, km.toDouble(), detail, TileStore.MAX_DOWNLOAD)
        } else null
    }
    val tileCount = plan?.tiles?.size

    Row(Modifier.fillMaxWidth().padding(horizontal = 4.dp)) {
        NumberField("Latitude", latitude, Modifier.weight(1f)) { latitude = it }
        NumberField("Longitude", longitude, Modifier.weight(1f)) { longitude = it }
        NumberField("Radius (km)", radius, Modifier.width(120.dp)) { radius = it }
    }
    FormRow {
        Text(
            when {
                areaProblem != null -> areaProblem
                plan != null && tileCount != null -> String.format(Locale.US, "About %,d tiles per map (zoom %d to %d)",
                                                                  tileCount, TileMath.DOWNLOAD_MIN_ZOOM, plan.topZoom)
                else -> ""
            },
            style = AppFont.body(13, 500, if (tileCount != null && tileCount > TileStore.MAX_DOWNLOAD) Theme.warning else Theme.textSecondary),
            modifier = Modifier.weight(1f),
        )
        val known = map.aircraftPosition?.let { it.latitude to it.longitude } ?: settings.lastPosition
        if (known != null) {
            BarButton("Aircraft position") {
                latitude = coordinate(known.first)
                longitude = coordinate(known.second)
            }
        }
    }
    RowDivider()
    val running = map.download
    SwitchRow(
        "Closest zoom too (${TileMath.DETAIL_TOP_ZOOM})", detail,
        note = "Streets and buildings up close. About three times the tiles, so three times the wait: off, a download stops at zoom ${TileMath.DOWNLOAD_TOP_ZOOM}.",
        enabled = running == null,
    ) { map.update(settings.copy(downloadDetail = it)) }
    RowDivider()
    if (running != null) {
        FormRow {
            Column(Modifier.weight(1f)) {
                Text(String.format(Locale.US, "Downloading %s… %,d of %,d", running.style.title.lowercase(Locale.US),
                                   running.done, running.total) + if (running.failed > 0) " · ${running.failed} failed" else "",
                     style = AppFont.body(14, 600))
                LinearProgressIndicator(
                    progress = { if (running.total > 0) running.done.toFloat() / running.total else 0f },
                    color = Theme.magenta, trackColor = Theme.stroke,
                    modifier = Modifier.fillMaxWidth().padding(top = 6.dp),
                )
            }
            BarButton("Stop") { map.stopDownload() }
        }
    } else {
        for ((index, style) in MapStyle.entries.withIndex()) {
            if (index > 0) RowDivider()
            val enabled = areaProblem == null
            FormRow(Modifier.clickable(enabled = enabled, role = Role.Button) {
                problem = map.startDownload(style, lat ?: 0.0, lon ?: 0.0, km ?: 0f)
            }) {
                Text(if (style == MapStyle.SATELLITE) "Download satellite" else "Download street map",
                     style = AppFont.body(15, 600, if (enabled) Theme.magenta else Theme.textTertiary))
            }
        }
    }
    (problem ?: map.downloadMessage)?.let {
        RowDivider()
        FormRow { Text(it, style = AppFont.body(13, 500, if (problem != null) Theme.warning else Theme.textSecondary)) }
    }
}

@Composable
private fun StoredMaps() {
    val map = LocalModel.current.map
    var deleting by remember { mutableStateOf<MapStyle?>(null) }
    LaunchedEffect(Unit) { map.refreshStored() }
    for ((index, style) in MapStyle.entries.withIndex()) {
        if (index > 0) RowDivider()
        val stored = map.stored[style]
        FormRow {
            Text(style.title, style = AppFont.body(15, 600), modifier = Modifier.weight(1f))
            Text(MapController.storedText(stored), style = AppFont.figures(13, 500, Theme.textSecondary))
        }
        if (stored != null && stored.count > 0) {
            val busy = map.download?.style == style
            FormRow(Modifier.clickable(enabled = !busy, role = Role.Button) { deleting = style }) {
                Text(if (style == MapStyle.SATELLITE) "Delete satellite" else "Delete street map",
                     style = AppFont.body(15, 600, if (busy) Theme.textTertiary else Theme.danger))
            }
        }
    }
    deleting?.let { style ->
        AlertDialog(
            onDismissRequest = { deleting = null },
            title = { Text("Delete the stored ${style.title.lowercase(Locale.US)}?") },
            text = { Text("${MapController.storedText(map.stored[style])}. Getting it back needs internet.") },
            confirmButton = {
                TextButton(onClick = {
                    map.delete(style)
                    deleting = null
                }) { Text("Delete", color = Theme.danger) }
            },
            dismissButton = { TextButton(onClick = { deleting = null }) { Text("Cancel") } },
        )
    }
}

/**
 * Edited as a draft and applied with Apply, and only when every address is usable: a template
 * half typed is never saved or used.
 */
@Composable
private fun TileServers() {
    val map = LocalModel.current.map
    val settings = map.settings
    val saved = ServerDraft.of(settings)
    var draft by remember(saved) { mutableStateOf(saved) }
    for ((index, style) in MapStyle.entries.withIndex()) {
        if (index > 0) RowDivider()
        val url = draft.url(style)
        MapTextField("${style.title} tiles", url, MapSettings.urlProblem(url)) { draft = draft.withUrl(style, it) }
        MapTextField("${style.title} credit", draft.credit(style)) { draft = draft.withCredit(style, it) }
    }
    RowDivider()
    val valid = MapStyle.entries.all { MapSettings.urlProblem(draft.url(it)) == null }
    val changed = draft != saved
    FormRow(Modifier.clickable(enabled = valid && changed, role = Role.Button) { map.update(draft.applyTo(settings)) }) {
        Text("Apply", style = AppFont.body(15, 600, if (valid && changed) Theme.magenta else Theme.textTertiary),
             modifier = Modifier.weight(1f))
        if (changed) {
            BarButton("Cancel") { draft = saved }
        }
    }
    RowDivider()
    FormRow(Modifier.clickable(role = Role.Button) { map.update(settings.withDefaultServers()) }) {
        Text("Restore defaults", style = AppFont.body(15, 600, Theme.magenta))
    }
}

/** The tile server fields being edited. */
private data class ServerDraft(val streetUrl: String, val streetCredit: String, val satelliteUrl: String,
                               val satelliteCredit: String) {
    fun url(style: MapStyle) = if (style == MapStyle.STREET) streetUrl else satelliteUrl
    fun credit(style: MapStyle) = if (style == MapStyle.STREET) streetCredit else satelliteCredit
    fun withUrl(style: MapStyle, url: String) = if (style == MapStyle.STREET) copy(streetUrl = url) else copy(satelliteUrl = url)
    fun withCredit(style: MapStyle, credit: String) =
        if (style == MapStyle.STREET) copy(streetCredit = credit) else copy(satelliteCredit = credit)
    fun applyTo(settings: MapSettings) = settings.copy(
        streetUrl = streetUrl.trim(), streetCredit = streetCredit.trim(),
        satelliteUrl = satelliteUrl.trim(), satelliteCredit = satelliteCredit.trim(),
    )

    companion object {
        fun of(settings: MapSettings) =
            ServerDraft(settings.streetUrl, settings.streetCredit, settings.satelliteUrl, settings.satelliteCredit)
    }
}

/** Six decimals: about 0.1 m, more than a download area needs. */
private fun coordinate(degrees: Double) = String.format(Locale.US, "%.6f", degrees)

@Composable
private fun NumberField(label: String, value: String, modifier: Modifier, onChange: (String) -> Unit) {
    OutlinedTextField(
        value = value,
        onValueChange = { text -> onChange(text.filter { it.isDigit() || it == '.' || it == '-' }.take(12)) },
        singleLine = true,
        label = { Text(label) },
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
        textStyle = AppFont.figures(15, 600),
        colors = fieldColors(),
        modifier = modifier.padding(horizontal = 6.dp, vertical = 6.dp),
    )
}

@Composable
private fun MapTextField(label: String, value: String, problem: String? = null, onChange: (String) -> Unit) {
    Column(Modifier.fillMaxWidth().padding(horizontal = 10.dp, vertical = 6.dp), horizontalAlignment = Alignment.Start) {
        OutlinedTextField(
            value = value,
            onValueChange = onChange,
            singleLine = true,
            label = { Text(label) },
            isError = problem != null,
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri, autoCorrectEnabled = false),
            textStyle = AppFont.body(14, 600),
            colors = fieldColors(),
            modifier = Modifier.fillMaxWidth(),
        )
        if (problem != null) {
            Text(problem, style = AppFont.body(12, 600, Theme.warning), modifier = Modifier.padding(start = 4.dp, top = 2.dp))
        }
    }
}

@Composable
private fun fieldColors() = OutlinedTextFieldDefaults.colors(
    focusedBorderColor = Theme.magenta, unfocusedBorderColor = Theme.stroke,
    cursorColor = Theme.magenta, focusedLabelColor = Theme.magenta,
)
