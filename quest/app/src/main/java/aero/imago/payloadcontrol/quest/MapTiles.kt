package aero.imago.payloadcontrol.quest

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.util.LruCache
import kotlinx.serialization.Serializable
import kotlinx.serialization.encodeToString
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.math.PI
import kotlin.math.atan
import kotlin.math.cos
import kotlin.math.floor
import kotlin.math.ln
import kotlin.math.sinh
import kotlin.math.tan

/** One Web-Mercator tile (256 px): zoom, column, row. */
data class TileId(val z: Int, val x: Int, val y: Int)

/** The slippy-map arithmetic every tile server shares. */
object TileMath {
    const val MIN_ZOOM = 10
    const val MAX_ZOOM = 18

    /** Column of a longitude at a zoom, with its fraction. */
    fun x(lonDeg: Double, z: Int): Double = (lonDeg + 180.0) / 360.0 * (1 shl z)

    /** Row of a latitude at a zoom, with its fraction (0 at the top, 85.05 N). */
    fun y(latDeg: Double, z: Int): Double {
        val lat = latDeg.coerceIn(-85.05, 85.05) * PI / 180.0
        return (1.0 - ln(tan(lat) + 1.0 / cos(lat)) / PI) / 2.0 * (1 shl z)
    }

    fun lon(x: Double, z: Int): Double = x / (1 shl z) * 360.0 - 180.0
    fun lat(y: Double, z: Int): Double = atan(sinh(PI * (1.0 - 2.0 * y / (1 shl z)))) * 180.0 / PI

    /** Ground metres per tile pixel. */
    fun metresPerPixel(latDeg: Double, z: Int): Double = 156543.03392 * cos(latDeg * PI / 180.0) / (1 shl z)

    /** Every tile that touches the square of `radiusKm` around a point, for each zoom. */
    fun around(latDeg: Double, lonDeg: Double, radiusKm: Double, zooms: IntRange): List<TileId> {
        val dLat = radiusKm / 111.32
        val dLon = radiusKm / (111.32 * cos(latDeg * PI / 180.0).coerceAtLeast(0.05))
        val tiles = ArrayList<TileId>()
        for (z in zooms) {
            val n = 1 shl z
            val x0 = floor(x(lonDeg - dLon, z)).toInt().coerceIn(0, n - 1)
            val x1 = floor(x(lonDeg + dLon, z)).toInt().coerceIn(0, n - 1)
            val y0 = floor(y(latDeg + dLat, z)).toInt().coerceIn(0, n - 1)
            val y1 = floor(y(latDeg - dLat, z)).toInt().coerceIn(0, n - 1)
            for (tx in x0..x1) for (ty in y0..y1) tiles += TileId(z, tx, ty)
        }
        return tiles
    }
}

/**
 * The map's settings, in `map.json` beside `profiles.json` (they are the headset's, not a
 * profile's). The tile URLs take {z}, {x} and {y}; point them at a provider you have the right
 * to cache from (see quest/README.md).
 */
@Serializable
data class MapSettings(
    val visible: Boolean = true,
    /** "satellite" or "street". */
    val style: String = "satellite",
    val zoom: Int = 16,
    val streetUrl: String = "https://tile.openstreetmap.org/{z}/{x}/{y}.png",
    val streetCredit: String = "© OpenStreetMap contributors",
    val satelliteUrl: String = "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}",
    val satelliteCredit: String = "Esri, Maxar, Earthstar Geographics",
    /** Where the aircraft was last seen: what the panel offers to download around. */
    val lastLat: Double = 0.0,
    val lastLon: Double = 0.0,
    val downloadRadiusKm: Float = 3f,
) {
    val satellite: Boolean get() = !style.equals("street", ignoreCase = true)
    fun url(style: String): String = if (style.equals("street", ignoreCase = true)) streetUrl else satelliteUrl

    companion object {
        const val FILE_NAME = "map.json"
        fun load(dir: File): MapSettings =
            runCatching { Profiles.json.decodeFromString<MapSettings>(File(dir, FILE_NAME).readText()) }.getOrNull()
                ?.let { it.copy(zoom = it.zoom.coerceIn(TileMath.MIN_ZOOM, TileMath.MAX_ZOOM)) } ?: MapSettings()

        fun save(dir: File, settings: MapSettings): Boolean =
            runCatching { File(dir, FILE_NAME).writeText(Profiles.json.encodeToString(settings)) }.isSuccess
    }
}

/**
 * Map tiles kept on the headset: `files/tiles/<style>/<z>/<x>/<y>`. With the payload's Wi-Fi
 * there is no internet, so what is on disk is what there is: the 2D panel downloads an area
 * beforehand, and in VR a tile that is missing is asked for once in a while in case the
 * headset does have a way out.
 *
 * `bitmap` never blocks: it answers from memory, or returns null and loads in the background
 * (the HUD thread calls it thirty times a second).
 */
class TileStore(
    private val root: File,
    private val settings: () -> MapSettings,
    /** Fetches a URL's bytes, or null. Replaced in tests. */
    private val fetch: (String) -> ByteArray? = ::httpGet,
    private val io: ExecutorService = Executors.newFixedThreadPool(2) { r -> Thread(r, "imago-tiles").apply { isDaemon = true } },
) {
    private val memory = object : LruCache<String, Bitmap>(MEMORY_TILES) {}
    private val pending = ConcurrentHashMap.newKeySet<String>()
    private val missing = ConcurrentHashMap<String, Long>() // key -> when it may be tried again
    @Volatile private var offlineUntilMs = 0L

    private fun key(style: String, id: TileId) = "$style/${id.z}/${id.x}/${id.y}"
    fun file(style: String, id: TileId) = File(root, key(style, id))
    fun has(style: String, id: TileId) = file(style, id).let { it.isFile && it.length() > 0 }

    /** The tile if it is in memory; otherwise null, and it is loaded for a later frame. */
    fun bitmap(style: String, id: TileId): Bitmap? {
        val k = key(style, id)
        memory.get(k)?.let { return it }
        val now = System.currentTimeMillis()
        val retryAt = missing[k]
        if (retryAt != null && now < retryAt) return null
        if (!pending.add(k)) return null
        io.execute {
            try {
                val loaded = decode(file(style, id)) ?: download(style, id)?.let { decode(file(style, id)) }
                if (loaded != null) {
                    memory.put(k, loaded)
                    missing.remove(k)
                } else {
                    missing[k] = System.currentTimeMillis() + RETRY_MS
                }
            } finally {
                pending.remove(k)
            }
        }
        return null
    }

    private fun decode(f: File): Bitmap? =
        if (f.isFile && f.length() > 0) runCatching { BitmapFactory.decodeFile(f.path) }.getOrNull() else null

    /** Fetches one tile to disk. Null when there is no way out, which is then not tried for a while. */
    private fun download(style: String, id: TileId): File? {
        if (System.currentTimeMillis() < offlineUntilMs) return null
        val url = settings().url(style).replace("{z}", id.z.toString()).replace("{x}", id.x.toString()).replace("{y}", id.y.toString())
        val bytes = runCatching { fetch(url) }.getOrNull()
        if (bytes == null || bytes.isEmpty()) {
            offlineUntilMs = System.currentTimeMillis() + OFFLINE_MS
            return null
        }
        val f = file(style, id)
        return runCatching {
            f.parentFile?.mkdirs()
            val part = File(f.path + ".part")
            part.writeBytes(bytes)
            if (!part.renameTo(f)) part.delete()
            f.takeIf { it.isFile }
        }.getOrNull()
    }

    /**
     * Downloads what is not on disk yet, on the caller's thread (the panel's worker).
     * `progress(done, total, failed)` after each tile; stops when `cancelled` is set or after
     * a run of failures (no internet). Returns how many are on disk at the end.
     */
    fun downloadAll(style: String, tiles: List<TileId>, cancelled: AtomicBoolean, progress: (Int, Int, Int) -> Unit): Int {
        var done = 0
        var failed = 0
        var failedInARow = 0
        offlineUntilMs = 0
        for (id in tiles) {
            if (cancelled.get() || failedInARow >= GIVE_UP_AFTER) break
            if (has(style, id)) {
                done++
            } else {
                offlineUntilMs = 0
                if (download(style, id) != null) {
                    done++
                    failedInARow = 0
                } else {
                    failed++
                    failedInARow++
                }
                Thread.sleep(POLITE_MS) // a few tiles a second: a courtesy to the tile server
            }
            progress(done, tiles.size, failed)
        }
        return done
    }

    /** How many tiles of a style are on disk, and their size in bytes. */
    fun stored(style: String): Pair<Int, Long> {
        var count = 0
        var bytes = 0L
        File(root, style).walkTopDown().filter { it.isFile && !it.name.endsWith(".part") }.forEach {
            count++
            bytes += it.length()
        }
        return count to bytes
    }

    fun clear(style: String) {
        File(root, style).deleteRecursively()
        memory.evictAll()
        missing.clear()
    }

    companion object {
        const val MEMORY_TILES = 96
        const val RETRY_MS = 60_000L   // a tile that could not be had is asked for again after this
        const val OFFLINE_MS = 60_000L // after a failed fetch nothing is fetched for this long
        const val GIVE_UP_AFTER = 8
        const val POLITE_MS = 120L
        const val MAX_DOWNLOAD = 3000

        /** A plain GET with a generic client name (tile servers ask for one); null on any failure. */
        fun httpGet(url: String): ByteArray? {
            val connection = (URL(url).openConnection() as? HttpURLConnection) ?: return null
            return try {
                connection.connectTimeout = 4000
                connection.readTimeout = 8000
                connection.setRequestProperty("User-Agent", "ImagoPayloadVR/1.0 (offline map cache)")
                if (connection.responseCode != 200) null else connection.inputStream.use { it.readBytes() }
            } catch (e: Exception) {
                null
            } finally {
                connection.disconnect()
            }
        }
    }
}
