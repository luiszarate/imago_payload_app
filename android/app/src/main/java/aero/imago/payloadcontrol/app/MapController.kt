package aero.imago.payloadcontrol.app

import aero.imago.payloadcontrol.core.GeoPoint
import aero.imago.payloadcontrol.map.DownloadOutcome
import aero.imago.payloadcontrol.map.MapSettings
import aero.imago.payloadcontrol.map.MapStyle
import aero.imago.payloadcontrol.map.StoredTiles
import aero.imago.payloadcontrol.map.TileMath
import aero.imago.payloadcontrol.map.TileStore
import android.content.Context
import android.content.SharedPreferences
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import java.io.File
import java.util.Locale
import java.util.concurrent.atomic.AtomicBoolean

/**
 * The map on the picture and its offline tiles: settings, the tile store, and area downloads
 * for Settings › Map. State is written on the main thread; downloads and folder walks run on
 * an IO thread.
 */
class MapController(context: Context, private val prefs: SharedPreferences, private val scope: CoroutineScope) {
    /** An area download under way. */
    data class Download(val style: MapStyle, val done: Int, val total: Int, val failed: Int)

    private val main = Handler(Looper.getMainLooper())

    var settings by mutableStateOf(MapSettings.load(prefs)); private set
    /** Bumped whenever a tile has been decoded, so the map redraws with it. */
    var tileVersion by mutableIntStateOf(0); private set
    var download by mutableStateOf<Download?>(null); private set
    /** How the last download ended, for Settings › Map. */
    var downloadMessage by mutableStateOf<String?>(null); private set
    var stored by mutableStateOf<Map<MapStyle, StoredTiles>>(emptyMap()); private set
    /** The aircraft's position as last reported this run; saved into the settings on disconnect. */
    var aircraftPosition by mutableStateOf<GeoPoint?>(null); private set

    val tiles = TileStore(
        root = File(context.filesDir, "tiles"),
        userAgent = "ImagoPayloadControl/${versionName(context)} (Android; offline map cache)",
        onLoaded = { main.post { tileVersion += 1 } },
    )
    private var cancel: AtomicBoolean? = null

    fun update(next: MapSettings) {
        if (next == settings) return
        settings = next
        next.save(prefs)
    }

    fun aircraftSeen(position: GeoPoint?) {
        if (position != null) aircraftPosition = position
    }

    /** Keeps where the aircraft was last seen, for the next download's area. */
    fun rememberAircraftPosition() {
        val position = aircraftPosition ?: return
        update(settings.copy(lastLat = position.latitude, lastLon = position.longitude))
    }

    /** Re-counts the stored tiles of both styles (in the background). */
    fun refreshStored() {
        scope.launch(Dispatchers.IO) {
            val counts = MapStyle.entries.associateWith { tiles.stored(it) }
            main.post { stored = counts }
        }
    }

    /** Starts downloading an area; returns what is wrong instead, if anything. */
    fun startDownload(style: MapStyle, latitude: Double, longitude: Double, radiusKm: Float): String? {
        if (download != null) return "A download is already running."
        problem(latitude, longitude, radiusKm)?.let { return it }
        MapSettings.urlProblem(settings.url(style))?.let { return "${style.title} tile server: $it" }
        val plan = TileMath.plan(latitude, longitude, radiusKm.toDouble(), settings.downloadDetail, TileStore.MAX_DOWNLOAD)
        val area = plan.tiles
        if (area.size > TileStore.MAX_DOWNLOAD) {
            return "Too large: ${area.size} tiles (at most ${TileStore.MAX_DOWNLOAD}). Use a smaller radius."
        }
        update(settings.copy(downloadRadiusKm = radiusKm))
        val flag = AtomicBoolean(false)
        cancel = flag
        downloadMessage = null
        download = Download(style, 0, area.size, 0)
        val template = settings.url(style).trim()
        scope.launch(Dispatchers.IO) {
            val outcome = tiles.downloadAll(style, template, area, flag) { done, total, failed ->
                main.post { if (cancel === flag) download = Download(style, done, total, failed) }
            }
            val counts = MapStyle.entries.associateWith { tiles.stored(it) }
            main.post {
                if (cancel === flag) {
                    cancel = null
                    download = null
                }
                stored = counts
                downloadMessage = message(style, outcome, plan.topZoom)
                tileVersion += 1
            }
        }
        return null
    }

    fun stopDownload() {
        cancel?.set(true)
    }

    fun delete(style: MapStyle) {
        if (download?.style == style) return
        scope.launch(Dispatchers.IO) {
            tiles.clear(style)
            val counts = MapStyle.entries.associateWith { tiles.stored(it) }
            main.post {
                stored = counts
                tileVersion += 1
            }
        }
    }

    companion object {
        /** What is wrong with a download area, or null. */
        fun problem(latitude: Double, longitude: Double, radiusKm: Float): String? = when {
            // Finite first: NaN or infinity must never reach the tile maths.
            !latitude.isFinite() || latitude !in -85.0..85.0 -> "Enter a latitude between -85 and 85."
            !longitude.isFinite() || longitude !in -180.0..180.0 -> "Enter a longitude between -180 and 180."
            !radiusKm.isFinite() || radiusKm !in MapSettings.RADIUS_MIN_KM..MapSettings.RADIUS_MAX_KM ->
                String.format(Locale.US, "Enter a radius from %.1f to %.0f km.", MapSettings.RADIUS_MIN_KM,
                              MapSettings.RADIUS_MAX_KM)
            else -> null
        }

        fun message(style: MapStyle, outcome: DownloadOutcome, topZoom: Int): String = when {
            outcome.gaveUp -> "${style.title}: stopped, the tile server cannot be reached (no internet?). " +
                "${outcome.stored} of ${outcome.total} tiles stored."
            outcome.cancelled -> "${style.title}: stopped. ${outcome.stored} of ${outcome.total} tiles stored."
            outcome.failed > 0 -> "${style.title}: ${outcome.stored} of ${outcome.total} tiles stored, ${outcome.failed} failed."
            else -> "${style.title}: done, all ${outcome.total} tiles stored (zoom ${TileMath.DOWNLOAD_MIN_ZOOM} to $topZoom)."
        }

        fun storedText(tiles: StoredTiles?): String {
            if (tiles == null) return "…"
            if (tiles.count == 0) return "None"
            return String.format(Locale.US, "%,d tiles · %.1f MB", tiles.count, tiles.bytes / 1_048_576.0)
        }

        private fun versionName(context: Context): String = runCatching {
            val info = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                context.packageManager.getPackageInfo(context.packageName, PackageManager.PackageInfoFlags.of(0))
            } else {
                @Suppress("DEPRECATION")
                context.packageManager.getPackageInfo(context.packageName, 0)
            }
            info.versionName
        }.getOrNull() ?: "0"
    }
}
