package aero.imago.payloadcontrol.quest

import android.content.Context
import android.content.Intent
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.toMutableStateList
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import android.os.Handler
import android.os.Looper
import java.io.File
import java.util.Locale
import java.util.concurrent.atomic.AtomicBoolean

/**
 * The app as a 2D panel: the setup screen. Connection profiles are edited here with the
 * system keyboard (they live in profiles.json, which adb can still reach), and "Enter VR"
 * starts the immersive activity on the selected profile. The link to the payload exists only
 * in VR: this panel never opens a session.
 */
class PanelActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val dir = getExternalFilesDir(null) ?: filesDir
        setContent {
            ImagoTheme {
                PanelScreen(dir, Selection.load(this)) { index ->
                    Selection.save(this, index)
                    startActivity(Intent(this, QuestActivity::class.java)
                        .putExtra(QuestActivity.EXTRA_PROFILE, index)
                        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
                }
            }
        }
    }
}

/** Which profile "Enter VR" uses, remembered between launches. */
internal object Selection {
    private const val PREFS = "panel"
    private const val KEY = "profile"
    fun load(context: Context): Int = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getInt(KEY, 0)
    fun save(context: Context, index: Int) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putInt(KEY, index).apply()
    }
}

@Composable
private fun PanelScreen(dir: File, initialSelection: Int, enterVr: (Int) -> Unit) {
    val profiles = remember { Profiles.load(dir).toMutableStateList() }
    var selected by remember { mutableIntStateOf(initialSelection.coerceIn(0, (profiles.size - 1).coerceAtLeast(0))) }
    var saveFailed by remember { mutableStateOf(false) }

    fun update(profile: ConnectionProfile) {
        profiles[selected] = profile
        // Saved as typed; a profile that is not valid yet is kept in the file's list only once
        // it is (Profiles.parse drops invalid ones), so the others are never lost.
        saveFailed = !Profiles.save(dir, profiles.filter { it.validationError == null })
    }

    val header: @Composable () -> Unit = {
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            Text("IMAGO", style = AppFont.display(24, 700, Theme.magenta))
            Text("Payload VR", style = AppFont.display(18, 500, Theme.textSecondary))
        }
        Spacer(Modifier.height(6.dp))
        Text("CONNECTIONS", style = AppFont.display(11, 600, Theme.textTertiary))
    }
    val list: @Composable () -> Unit = {
        profiles.forEachIndexed { index, profile ->
            ProfileRow(profile, index == selected) { selected = index }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            SmallButton("Add profile") {
                profiles.add(ConnectionProfile(name = "New profile", payloadIP = "192.168.144.240"))
                selected = profiles.size - 1
            }
            if (profiles.size > 1) {
                SmallButton("Delete", Theme.danger) {
                    profiles.removeAt(selected)
                    selected = selected.coerceAtMost(profiles.size - 1)
                    saveFailed = !Profiles.save(dir, profiles.filter { it.validationError == null })
                }
            }
        }
    }
    val enter: @Composable () -> Unit = {
        val current = profiles.getOrNull(selected)
        val error = current?.validationError
        if (saveFailed) Text("Could not save profiles.json", style = AppFont.body(13, 600, Theme.danger))
        if (error != null) Text(error, style = AppFont.body(13, 600, Theme.warning))
        val enabled = current != null && error == null
        Box(
            Modifier.fillMaxWidth().height(64.dp)
                .then(if (enabled) Modifier.background(Theme.accent, RoundedCornerShape(16.dp))
                      else Modifier.background(Theme.surfaceRaised, RoundedCornerShape(16.dp)))
                .clickable(enabled = enabled) {
                    // The immersive activity reads the saved list: its index is among the valid ones.
                    val valid = profiles.filter { it.validationError == null }
                    enterVr(valid.indexOf(current).coerceAtLeast(0))
                },
            contentAlignment = Alignment.Center,
        ) {
            Text("ENTER VR", style = AppFont.display(18, 700, if (enabled) Theme.text else Theme.textTertiary))
        }
        Text("The gimbal can follow your head in VR: A on the right controller, or HEAD on the right rail.",
            style = AppFont.body(12, 500, Theme.textSecondary))
    }
    // The selected profile's settings.
    val settings: @Composable (Modifier, Boolean) -> Unit = { modifier, scrolls ->
        val profile = profiles.getOrNull(selected)
        Column(
            modifier
                .background(Theme.surface, RoundedCornerShape(18.dp))
                .border(1.dp, Theme.stroke, RoundedCornerShape(18.dp))
                .padding(20.dp)
                .then(if (scrolls) Modifier.verticalScroll(rememberScrollState()) else Modifier),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            if (profile == null) {
                Text("No profile", style = AppFont.body(15))
            } else {
                // Keyed by the selection: the fields keep their own text while typing.
                key(selected) { ProfileEditor(profile, ::update) }
            }
            MapSection(dir)
        }
    }

    BoxWithConstraints(Modifier.fillMaxSize().background(Theme.background)) {
        if (maxWidth >= WIDE) {
            // Wide: the profiles and Enter VR at the left, the selected profile's settings beside.
            Row(Modifier.fillMaxSize().padding(20.dp), horizontalArrangement = Arrangement.spacedBy(20.dp)) {
                Column(Modifier.width(320.dp).fillMaxHeight(), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    header()
                    Column(Modifier.weight(1f).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(8.dp)) { list() }
                    enter()
                }
                settings(Modifier.weight(1f).fillMaxHeight(), true)
            }
        } else {
            // Narrow (the headset opened the window phone-sized, or the operator made it so):
            // one column that scrolls, the settings under the list, Enter VR always in view.
            Column(Modifier.fillMaxSize().padding(14.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Column(Modifier.weight(1f).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    header()
                    list()
                    settings(Modifier.fillMaxWidth(), false)
                }
                enter()
            }
        }
    }
}

/** Under this width the settings go under the profile list instead of beside it. */
private val WIDE = 720.dp

@Composable
private fun ProfileRow(profile: ConnectionProfile, selected: Boolean, onClick: () -> Unit) {
    val shape = RoundedCornerShape(13.dp)
    Row(
        Modifier.fillMaxWidth()
            .then(if (selected) Modifier.background(Theme.accent, shape) else Modifier.background(Theme.surfaceRaised, shape))
            .clickable(onClick = onClick)
            .padding(horizontal = 14.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        Box(Modifier.size(8.dp).background(if (profile.validationError == null) Theme.ok else Theme.warning, CircleShape))
        Column {
            Text(profile.name, style = AppFont.display(15, 600), maxLines = 1, overflow = TextOverflow.Ellipsis)
            Text(if (profile.isListen) "Listen (forwarded)" else profile.payloadIP.ifEmpty { "no address" },
                style = AppFont.body(12, 500, if (selected) Theme.text else Theme.textSecondary), maxLines = 1)
        }
    }
}

@Composable
private fun ProfileEditor(p: ConnectionProfile, update: (ConnectionProfile) -> Unit) {
    Section("CONNECTION")
    Field("Name", p.name) { update(p.copy(name = it)) }
    Choice("MAVLink", listOf("direct" to "Payload IP", "listen" to "Listen (forwarded)"), p.mavlinkMode.lowercase()) {
        update(p.copy(mavlinkMode = it))
    }
    if (!p.isListen) Field("Payload IP", p.payloadIP, KeyboardType.Decimal) { update(p.copy(payloadIP = it.trim())) }
    Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        Field("MAVLink port", p.mavlinkPort.toString(), KeyboardType.Number, Modifier.weight(1f)) {
            update(p.copy(mavlinkPort = it.toIntOrNull() ?: 0))
        }
        Field("Local port (0 = default)", p.localPort.toString(), KeyboardType.Number, Modifier.weight(1f)) {
            update(p.copy(localPort = it.toIntOrNull() ?: 0))
        }
    }
    Field("Video URL (empty = the one the payload announces)", p.videoURL, KeyboardType.Uri) { update(p.copy(videoURL = it.trim())) }
    Choice("Video transport", listOf("auto" to "Auto", "udp" to "UDP", "tcp" to "TCP"), p.videoTransport.lowercase()) {
        update(p.copy(videoTransport = it))
    }
    Choice("Gimbal commands", listOf("attitude" to "Gimbal v2 (direct)", "mountControl" to "Mount control",
        "manager" to "Autopilot gimbal manager"), p.gimbalProtocol.let { if (it.equals("mountcontrol", true)) "mountControl" else it.lowercase() }) {
        update(p.copy(gimbalProtocol = it))
    }
    Toggle("Keep Wi-Fi awake", "Steady traffic keeps the headset's Wi-Fi out of power save", p.keepWifiAwake) {
        update(p.copy(keepWifiAwake = it))
    }

    Section("LOG")
    Toggle("Record log", "Each VR session records its MAVLink to a .tlog on the headset (files/logs, the last 12)", p.recordLog) {
        update(p.copy(recordLog = it))
    }
    Toggle("Detailed log", "Every message and the app's measurements; off keeps the compact log", p.detailedLog) {
        update(p.copy(detailedLog = it))
    }

    Section("VIDEO")
    Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        Field("Restart when this far behind, ms (0 = never)", p.maxVideoLagMs.toString(), KeyboardType.Number, Modifier.weight(1f)) {
            update(p.copy(maxVideoLagMs = (it.toIntOrNull() ?: 0).coerceIn(0, 10000)))
        }
        Field("Smoothing, ms (0 = lowest latency)", p.videoSmoothingMs.toString(), KeyboardType.Number, Modifier.weight(1f)) {
            update(p.copy(videoSmoothingMs = (it.toIntOrNull() ?: 0).coerceIn(0, 1000)))
        }
    }

    Section("HEAD TRACKING")
    val head = p.headTracking
    Choice("Mode", listOf("angle" to "Angle (recommended)", "rate" to "Rate (closed loop)"),
        if (head.mode.equals("rate", true)) "rate" else "angle") {
        update(p.copy(headTracking = head.copy(mode = it)))
    }
    Toggle("On when the link comes up", "Otherwise A, or HEAD on the right rail, switches it on", head.onAtStart) {
        update(p.copy(headTracking = head.copy(onAtStart = it)))
    }
    Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        Field("Rate mode gain, °/s per degree (0 = 0.6)", trim(head.gainDpsPerDeg), KeyboardType.Decimal, Modifier.weight(1f)) {
            update(p.copy(headTracking = head.copy(gainDpsPerDeg = it.toFloatOrNull() ?: 0f)))
        }
        Field("Max speed, °/s (0 = 90)", trim(head.maxRateDps), KeyboardType.Decimal, Modifier.weight(1f)) {
            update(p.copy(headTracking = head.copy(maxRateDps = it.toFloatOrNull() ?: 0f)))
        }
    }
    Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        Field("Slack / deadband, ° (0 = 0.7)", trim(head.deadbandDeg), KeyboardType.Decimal, Modifier.weight(1f)) {
            update(p.copy(headTracking = head.copy(deadbandDeg = it.toFloatOrNull() ?: 0f)))
        }
        Field("Smoothing, ms (0 = 80)", head.smoothingMs.toString(), KeyboardType.Number, Modifier.weight(1f)) {
            update(p.copy(headTracking = head.copy(smoothingMs = it.toIntOrNull() ?: 0)))
        }
    }

    Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        Field("Scaling with zoom, 0 (none) to 1 (full)", trim(head.zoomCompensation), KeyboardType.Decimal, Modifier.weight(1f)) {
            update(p.copy(headTracking = head.copy(zoomCompensation = (it.toFloatOrNull() ?: 1f).coerceIn(0f, 1f))))
        }
        Field("Yaw range each side, ° (0 = 150)", trim(head.yawRangeDeg), KeyboardType.Decimal, Modifier.weight(1f)) {
            update(p.copy(headTracking = head.copy(yawRangeDeg = (it.toFloatOrNull() ?: 0f).coerceIn(0f, 180f))))
        }
    }

    Toggle("Clutch", "The camera follows your head only while you hold the left grip", head.clutch) {
        update(p.copy(headTracking = head.copy(clutch = it)))
    }

    Section("COMFORT")
    Toggle("Vignette", "The picture's edge darkens while your head turns fast", p.panel.vignette) {
        update(p.copy(panel = p.panel.copy(vignette = it)))
    }

    Section("JOYSTICK")
    val controls = p.controls
    Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        Field("Max yaw speed, °/s", trim(controls.maxYawDps), KeyboardType.Decimal, Modifier.weight(1f)) {
            update(p.copy(controls = controls.copy(maxYawDps = (it.toFloatOrNull() ?: 60f).coerceIn(1f, 200f))))
        }
        Field("Max pitch speed, °/s", trim(controls.maxPitchDps), KeyboardType.Decimal, Modifier.weight(1f)) {
            update(p.copy(controls = controls.copy(maxPitchDps = (it.toFloatOrNull() ?: 60f).coerceIn(1f, 200f))))
        }
    }
    Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        Field("Deadzone, 0 to 0.5", trim(controls.deadzone), KeyboardType.Decimal, Modifier.weight(1f)) {
            update(p.copy(controls = controls.copy(deadzone = (it.toFloatOrNull() ?: 0.08f).coerceIn(0f, 0.5f))))
        }
        Field("Expo, 0 (linear) to 1", trim(controls.expo), KeyboardType.Decimal, Modifier.weight(1f)) {
            update(p.copy(controls = controls.copy(expo = (it.toFloatOrNull() ?: 0.55f).coerceIn(0f, 1f))))
        }
    }
    Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        Field("Slower with zoom, 0 (no) to 1 (full)", trim(controls.zoomCompensation), KeyboardType.Decimal, Modifier.weight(1f)) {
            update(p.copy(controls = controls.copy(zoomCompensation = (it.toFloatOrNull() ?: 0.5f).coerceIn(0f, 1f))))
        }
        Field("Trim speed with head tracking on, 0 to 1", trim(controls.trimFactor), KeyboardType.Decimal, Modifier.weight(1f)) {
            update(p.copy(controls = controls.copy(trimFactor = (it.toFloatOrNull() ?: 0.25f).coerceIn(0f, 1f))))
        }
    }
    Toggle("Fine aim", "Slow stick moves go as angles: the Orus L ignores rates under 2°/s", p.controls.fineAim) {
        update(p.copy(controls = p.controls.copy(fineAim = it)))
    }
    if (p.controls.fineAim) {
        Field("Fine aim below, °/s (2 = the gimbal's own floor)", trim(p.controls.fineAimDps), KeyboardType.Decimal) {
            update(p.copy(controls = controls.copy(fineAimDps = (it.toFloatOrNull() ?: 2f).coerceIn(0.5f, 20f))))
        }
    }

    Toggle("Labels on the controllers", "In VR, a card over each controller says what its buttons do while you look at it", controls.controllerLabels) {
        update(p.copy(controls = controls.copy(controllerLabels = it)))
    }

    Section("SCREEN")
    Toggle("Follows the head", "With head tracking on, the screen goes with your head", p.panel.followHead) {
        update(p.copy(panel = p.panel.copy(followHead = it)))
    }
    Toggle("Passthrough", "The room around the virtual screen", p.panel.passthrough) {
        update(p.copy(panel = p.panel.copy(passthrough = it)))
    }
    Field("Room dimming with passthrough, 0 (none) to 1 (black)", trim(p.panel.roomDim), KeyboardType.Decimal) {
        update(p.copy(panel = p.panel.copy(roomDim = (it.toFloatOrNull() ?: 0.35f).coerceIn(0f, 1f))))
    }
    Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        Field("Distance, m", trim(p.panel.distanceM), KeyboardType.Decimal, Modifier.weight(1f)) {
            update(p.copy(panel = p.panel.copy(distanceM = (it.toFloatOrNull() ?: 2.2f).coerceIn(0.5f, 10f))))
        }
        Field("Width, m", trim(p.panel.widthM), KeyboardType.Decimal, Modifier.weight(1f)) {
            update(p.copy(panel = p.panel.copy(widthM = (it.toFloatOrNull() ?: 2.6f).coerceIn(0.5f, 10f))))
        }
        Field("Below eye level, °", trim(p.panel.pitchDownDeg), KeyboardType.Decimal, Modifier.weight(1f)) {
            update(p.copy(panel = p.panel.copy(pitchDownDeg = (it.toFloatOrNull() ?: 8f).coerceIn(-30f, 45f))))
        }
    }
}

/**
 * The map's tiles are downloaded here, with internet, before going to the aircraft: on the
 * payload's Wi-Fi there is none. They belong to the headset, not to a profile.
 */
@Composable
private fun MapSection(dir: File) {
    var settings by remember { mutableStateOf(MapSettings.load(dir)) }
    val store = remember { TileStore(File(dir, "tiles"), { settings }) }
    val main = remember { Handler(Looper.getMainLooper()) }
    var lat by remember { mutableStateOf(if (settings.lastLat != 0.0) String.format(Locale.US, "%.5f", settings.lastLat) else "") }
    var lon by remember { mutableStateOf(if (settings.lastLon != 0.0) String.format(Locale.US, "%.5f", settings.lastLon) else "") }
    var radius by remember { mutableStateOf(trim(settings.downloadRadiusKm)) }
    var status by remember { mutableStateOf("") }
    var running by remember { mutableStateOf<AtomicBoolean?>(null) }
    var stored by remember { mutableStateOf("") }
    fun megabytes(bytes: Long) = String.format(Locale.US, "%.0f MB", bytes / 1e6)
    fun refresh() {
        Thread {
            val (satellite, satelliteBytes) = store.stored("satellite")
            val (street, streetBytes) = store.stored("street")
            main.post { stored = "On the headset: satellite $satellite tiles (${megabytes(satelliteBytes)}) · street $street tiles (${megabytes(streetBytes)})" }
        }.start()
    }
    androidx.compose.runtime.LaunchedEffect(Unit) { refresh() }

    fun download(style: String) {
        val latitude = lat.trim().toDoubleOrNull()
        val longitude = lon.trim().toDoubleOrNull()
        val km = (radius.trim().toFloatOrNull() ?: 3f).coerceIn(0.5f, 15f)
        if (latitude == null || longitude == null || latitude !in -85.0..85.0 || longitude !in -180.0..180.0) {
            status = "Type the latitude and longitude of the flight area, in degrees (19.4326, -99.1332)"
            return
        }
        settings = settings.copy(lastLat = latitude, lastLon = longitude, downloadRadiusKm = km)
        MapSettings.save(dir, settings)
        // Every zoom the map shows; the closest ones are dropped if the area is too large.
        var top = 17
        var tiles = TileMath.around(latitude, longitude, km.toDouble(), 11..top)
        while (tiles.size > TileStore.MAX_DOWNLOAD && top > 13) {
            top--
            tiles = TileMath.around(latitude, longitude, km.toDouble(), 11..top)
        }
        val cancelled = AtomicBoolean(false)
        running = cancelled
        status = "Downloading $style: 0 / ${tiles.size}"
        Thread {
            val done = store.downloadAll(style, tiles, cancelled) { d, total, failed ->
                main.post { status = "Downloading $style: $d / $total" + if (failed > 0) " · $failed failed" else "" }
            }
            main.post {
                status = when {
                    done == tiles.size -> "Done: $done $style tiles around $latitude, $longitude (zoom 11 to $top)"
                    cancelled.get() -> "Stopped at $done of ${tiles.size}"
                    else -> "Stopped at $done of ${tiles.size}: no internet? Connect the headset to a network with internet and try again"
                }
                running = null
                refresh()
            }
        }.start()
    }

    Section("OFFLINE MAP (ALL PROFILES)")
    Text("The map in VR shows what is stored on the headset: on the payload's Wi-Fi there is no internet. " +
        "With internet, download the flight area here first. After a flight, the aircraft's last position is filled in.",
        style = AppFont.body(13, 500, Theme.textSecondary))
    Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        Field("Latitude, °", lat, KeyboardType.Text, Modifier.weight(1f)) { lat = it }
        Field("Longitude, °", lon, KeyboardType.Text, Modifier.weight(1f)) { lon = it }
        Field("Radius, km", radius, KeyboardType.Decimal, Modifier.weight(1f)) { radius = it }
    }
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        if (running == null) {
            SmallButton("Download satellite") { download("satellite") }
            SmallButton("Download street map") { download("street") }
        } else {
            SmallButton("Stop", Theme.danger) { running?.set(true) }
        }
    }
    if (status.isNotEmpty()) Text(status, style = AppFont.body(13, 600, Theme.text))
    if (stored.isNotEmpty()) Text(stored, style = AppFont.body(13, 500, Theme.textSecondary))
    if (running == null) {
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            SmallButton("Delete satellite", Theme.danger) { Thread { store.clear("satellite"); main.post { refresh() } }.start() }
            SmallButton("Delete street map", Theme.danger) { Thread { store.clear("street"); main.post { refresh() } }.start() }
        }
    }
    Toggle("Show the map in VR", "In the picture's bottom right corner; its own buttons hide it, zoom it and switch satellite / street", settings.visible) {
        settings = settings.copy(visible = it)
        MapSettings.save(dir, settings)
    }
}

private fun trim(value: Float): String = if (value == value.toInt().toFloat()) value.toInt().toString() else value.toString()

@Composable
private fun Section(title: String) {
    Text(title, style = AppFont.display(11, 600, Theme.textTertiary), modifier = Modifier.padding(top = 6.dp))
}

@Composable
private fun Field(label: String, value: String, keyboard: KeyboardType = KeyboardType.Text, modifier: Modifier = Modifier.fillMaxWidth(), onChange: (String) -> Unit) {
    // The text being typed is kept here, so a number half entered ("192.") is not rewritten.
    var text by remember(label) { mutableStateOf(value) }
    OutlinedTextField(
        value = text,
        onValueChange = { text = it; onChange(it) },
        label = { Text(label, style = AppFont.body(12, 500, Theme.textSecondary)) },
        singleLine = true,
        textStyle = AppFont.body(15, 600),
        keyboardOptions = KeyboardOptions(keyboardType = keyboard),
        colors = OutlinedTextFieldDefaults.colors(
            focusedBorderColor = Theme.magenta, unfocusedBorderColor = Theme.stroke,
            cursorColor = Theme.magenta, focusedContainerColor = Theme.surfaceRaised,
            unfocusedContainerColor = Theme.surfaceRaised,
        ),
        shape = RoundedCornerShape(12.dp),
        modifier = modifier,
    )
}

@Composable
private fun Choice(label: String, options: List<Pair<String, String>>, selected: String, onSelect: (String) -> Unit) {
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Text(label, style = AppFont.body(12, 500, Theme.textSecondary))
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            options.forEach { (value, title) ->
                val active = value == selected
                val shape = RoundedCornerShape(12.dp)
                Box(
                    Modifier
                        .then(if (active) Modifier.background(Theme.accent, shape) else Modifier.background(Theme.surfaceRaised, shape))
                        .clickable { onSelect(value) }
                        .padding(horizontal = 14.dp, vertical = 10.dp),
                ) {
                    Text(title, style = AppFont.display(13, 600))
                }
            }
        }
    }
}

@Composable
private fun Toggle(title: String, subtitle: String, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text(title, style = AppFont.body(15, 600))
            Text(subtitle, style = AppFont.body(12, 500, Theme.textSecondary))
        }
        Switch(checked, onChange, colors = SwitchDefaults.colors(checkedTrackColor = Theme.magenta, checkedThumbColor = Theme.text))
    }
}

@Composable
private fun SmallButton(title: String, color: Color = Theme.text, onClick: () -> Unit) {
    Box(
        Modifier.background(Theme.surfaceRaised, RoundedCornerShape(12.dp))
            .border(1.dp, Theme.stroke, RoundedCornerShape(12.dp))
            .clickable(onClick = onClick)
            .padding(horizontal = 14.dp, vertical = 10.dp),
    ) {
        Text(title, style = AppFont.display(13, 600, color))
    }
}
