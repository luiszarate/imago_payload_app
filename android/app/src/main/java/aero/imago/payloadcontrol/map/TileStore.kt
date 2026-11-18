package aero.imago.payloadcontrol.map

import android.graphics.BitmapFactory
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.Executor
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

/** Tiles of one style on disk: how many and how many bytes. */
data class StoredTiles(val count: Int, val bytes: Long)

/** How an area download ended. */
data class DownloadOutcome(val stored: Int, val total: Int, val failed: Int, val cancelled: Boolean, val gaveUp: Boolean)

/**
 * Map tiles kept on this device: `tiles/<style>/<z>/<x>/<y>` in the app's files. With the
 * payload's Wi-Fi there is no internet, so what is on disk is what there is: Settings › Map
 * downloads an area beforehand, and the map on the picture never fetches anything.
 *
 * [bitmap] never blocks: it answers from memory, or returns null and decodes the tile on a
 * background thread, then calls `onLoaded` (from that thread) so the map redraws.
 */
class TileStore(
    private val root: File,
    private val userAgent: String,
    /** Fetches a URL's bytes, or null. Replaced in tests. */
    private val fetch: (String) -> ByteArray? = { url -> httpGet(url, userAgent) },
    private val decode: (File) -> ImageBitmap? = { file -> decodeTile(file) },
    private val io: Executor = Executors.newFixedThreadPool(2) { r -> Thread(r, "imago-tiles").apply { isDaemon = true } },
    /** Pause between two fetched tiles: a few tiles a second, a courtesy to the tile server. */
    private val politeMs: Long = POLITE_MS,
    private val onLoaded: () -> Unit = {},
) {
    /** Decoded tiles, least recently drawn first out. Guarded by itself. */
    private val memory = object : LinkedHashMap<String, ImageBitmap>(MEMORY_TILES, 0.75f, true) {
        override fun removeEldestEntry(eldest: MutableMap.MutableEntry<String, ImageBitmap>?) = size > MEMORY_TILES
    }
    private val pending: MutableSet<String> = ConcurrentHashMap.newKeySet()
    /** Tiles not on disk -> when to look again (a download may bring them meanwhile). */
    private val missing = ConcurrentHashMap<String, Long>()

    private fun key(style: MapStyle, id: TileId) = "${style.key}/${id.z}/${id.x}/${id.y}"
    fun file(style: MapStyle, id: TileId) = File(root, key(style, id))
    fun has(style: MapStyle, id: TileId) = file(style, id).let { it.isFile && it.length() > 0 }

    /** The tile if it is in memory; otherwise null, and it is read from disk for a later frame. */
    fun bitmap(style: MapStyle, id: TileId): ImageBitmap? {
        val k = key(style, id)
        synchronized(memory) { memory[k] }?.let { return it }
        val retryAt = missing[k]
        if (retryAt != null && System.currentTimeMillis() < retryAt) return null
        if (!pending.add(k)) return null
        io.execute {
            try {
                val f = file(style, id)
                val loaded = if (f.isFile && f.length() > 0) runCatching { decode(f) }.getOrNull() else null
                if (loaded != null) {
                    synchronized(memory) { memory[k] = loaded }
                    missing.remove(k)
                    onLoaded()
                } else {
                    missing[k] = System.currentTimeMillis() + RETRY_MS
                }
            } finally {
                pending.remove(k)
            }
        }
        return null
    }

    /**
     * Fetches one tile to disk; false when it could not be had. Only a picture is kept: a Wi-Fi
     * login page answers 200 too, and stored it would be a tile that never draws.
     */
    private fun download(style: MapStyle, template: String, id: TileId): Boolean {
        val bytes = runCatching { fetch(TileMath.url(template, id)) }.getOrNull()
        if (bytes == null || !isPicture(bytes)) return false
        val f = file(style, id)
        return try {
            f.parentFile?.mkdirs()
            // Written aside and renamed, so a tile cut short is never taken for a whole one.
            val part = File(f.path + ".part")
            part.writeBytes(bytes)
            if (!part.renameTo(f)) part.delete()
            f.isFile
        } catch (_: IOException) {
            false
        }
    }

    /**
     * Downloads what is not on disk yet, on the caller's thread (a background one).
     * `progress(done, total, failed)` after each tile; stops when `cancelled` is set or after a
     * run of failures (no internet).
     */
    fun downloadAll(
        style: MapStyle, template: String, tiles: List<TileId>, cancelled: AtomicBoolean,
        progress: (Int, Int, Int) -> Unit,
    ): DownloadOutcome {
        var done = 0
        var failed = 0
        var failedInARow = 0
        var gaveUp = false
        for (id in tiles) {
            if (cancelled.get()) break
            // Set here, not from the count after the loop: a complete run whose last few tiles
            // failed did not give up.
            if (failedInARow >= GIVE_UP_AFTER) {
                gaveUp = true
                break
            }
            if (has(style, id)) {
                done++
            } else {
                if (download(style, template, id)) {
                    done++
                    failedInARow = 0
                } else {
                    failed++
                    failedInARow++
                }
                if (politeMs > 0) Thread.sleep(politeMs)
            }
            progress(done, tiles.size, failed)
        }
        // What was missing may be there now.
        missing.clear()
        return DownloadOutcome(done, tiles.size, failed, cancelled.get(), gaveUp)
    }

    /** How many tiles of a style are on disk, and their size. Walks the folder: not on the main thread. */
    fun stored(style: MapStyle): StoredTiles {
        var count = 0
        var bytes = 0L
        File(root, style.key).walkTopDown().filter { it.isFile && !it.name.endsWith(".part") }.forEach {
            count++
            bytes += it.length()
        }
        return StoredTiles(count, bytes)
    }

    fun clear(style: MapStyle) {
        File(root, style.key).deleteRecursively()
        synchronized(memory) { memory.keys.removeAll { it.startsWith("${style.key}/") } }
        missing.clear()
    }

    companion object {
        const val MEMORY_TILES = 64
        /** A tile that was not on disk is looked for again after this. */
        const val RETRY_MS = 60_000L
        const val GIVE_UP_AFTER = 8
        const val POLITE_MS = 120L
        /** Largest area download, per style. */
        const val MAX_DOWNLOAD = 3000
        /** A tile is a few tens of kB; anything this big is no tile. */
        const val MAX_TILE_BYTES = 2_000_000

        /** PNG or JPEG by their first bytes (the formats tile servers serve), up to MAX_TILE_BYTES. */
        fun isPicture(bytes: ByteArray): Boolean {
            fun at(i: Int) = bytes[i].toInt() and 0xFF
            return bytes.size in 4..MAX_TILE_BYTES &&
                ((at(0) == 0x89 && at(1) == 0x50 && at(2) == 0x4E && at(3) == 0x47) ||
                    (at(0) == 0xFF && at(1) == 0xD8 && at(2) == 0xFF))
        }

        private fun decodeTile(file: File): ImageBitmap? = BitmapFactory.decodeFile(file.path)?.asImageBitmap()

        /** A plain GET naming the app (tile servers ask for that); null on any failure. */
        fun httpGet(url: String, userAgent: String): ByteArray? {
            val connection = (URL(url).openConnection() as? HttpURLConnection) ?: return null
            return try {
                connection.connectTimeout = 4000
                connection.readTimeout = 8000
                connection.setRequestProperty("User-Agent", userAgent)
                if (connection.responseCode != 200 || connection.contentLengthLong > MAX_TILE_BYTES) return null
                // Read no more than a tile can be: the URL is a setting, the answer anything.
                connection.inputStream.use { input ->
                    val out = ByteArrayOutputStream()
                    val buffer = ByteArray(16 * 1024)
                    while (true) {
                        val n = input.read(buffer)
                        if (n < 0) break
                        out.write(buffer, 0, n)
                        if (out.size() > MAX_TILE_BYTES) return null
                    }
                    out.toByteArray()
                }
            } catch (_: IOException) {
                null
            } finally {
                connection.disconnect()
            }
        }
    }
}
