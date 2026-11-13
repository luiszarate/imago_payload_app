package aero.imago.payloadcontrol.quest

import android.graphics.Bitmap
import android.graphics.Color
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode
import java.io.ByteArrayOutputStream
import java.io.File
import java.nio.file.Files
import java.util.concurrent.AbstractExecutorService
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

@RunWith(RobolectricTestRunner::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = [34])
class MapTilesTest {
    /** Runs what it is given at once, so a test sees the tile on the next call. */
    private class Inline : AbstractExecutorService() {
        override fun execute(command: Runnable) = command.run()
        override fun shutdown() {}
        override fun shutdownNow(): MutableList<Runnable> = mutableListOf()
        override fun isShutdown() = false
        override fun isTerminated() = false
        override fun awaitTermination(timeout: Long, unit: TimeUnit) = true
    }

    private fun png(color: Int): ByteArray {
        val bitmap = Bitmap.createBitmap(8, 8, Bitmap.Config.ARGB_8888).apply { eraseColor(color) }
        return ByteArrayOutputStream().also { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }.toByteArray()
    }

    @Test
    fun tileArithmetic() {
        // The whole world is one tile at zoom 0, and Greenwich on the equator is its centre.
        assertEquals(0.5, TileMath.x(0.0, 0), 1e-9)
        assertEquals(0.5, TileMath.y(0.0, 0), 1e-9)
        // Mexico City at zoom 15 (worked out separately with the standard slippy-map formula).
        assertEquals(7360, TileMath.x(-99.1332, 15).toInt())
        assertEquals(14580, TileMath.y(19.4326, 15).toInt())
        // There and back.
        assertEquals(19.4326, TileMath.lat(TileMath.y(19.4326, 16), 16), 1e-6)
        assertEquals(-99.1332, TileMath.lon(TileMath.x(-99.1332, 16), 16), 1e-6)
        // A pixel at zoom 16 there is a little over two metres.
        assertEquals(2.25, TileMath.metresPerPixel(19.4326, 16), 0.05)
        // 3 km around it: a handful of tiles far out, a hundred or so close in, the centre's among them.
        val far = TileMath.around(19.4326, -99.1332, 3.0, 11..11)
        val near = TileMath.around(19.4326, -99.1332, 3.0, 16..16)
        assertTrue(far.size in 1..4)
        assertTrue(near.size.toString(), near.size in 100..200)
        assertTrue(TileId(16, TileMath.x(-99.1332, 16).toInt(), TileMath.y(19.4326, 16).toInt()) in near)
    }

    @Test
    fun aTileIsFetchedOnceKeptOnDiskAndThereWithNoInternet() {
        val root = Files.createTempDirectory("tiles").toFile()
        val asked = ArrayList<String>()
        var online = true
        val settings = MapSettings(satelliteUrl = "https://tiles.example/{z}/{y}/{x}")
        val store = TileStore(root, { settings }, fetch = { url -> asked += url; if (online) png(Color.GREEN) else null }, io = Inline())
        val id = TileId(15, 7360, 14580)

        assertNull(store.bitmap("satellite", id)) // asked for; loaded behind
        assertEquals(listOf("https://tiles.example/15/14580/7360"), asked)
        val first = store.bitmap("satellite", id)
        assertNotNull(first)
        assertEquals(Color.GREEN, first!!.getPixel(2, 2))
        assertTrue(store.has("satellite", id))

        // Another session, no internet: the tile comes from the disk and nothing is asked.
        online = false
        val later = TileStore(root, { settings }, fetch = { url -> asked += url; null }, io = Inline())
        later.bitmap("satellite", id)
        assertNotNull(later.bitmap("satellite", id))
        assertEquals(1, asked.size)

        // A tile that is not there, offline: asked once, then left alone for a while.
        val other = TileId(15, 7361, 14580)
        assertNull(later.bitmap("satellite", other))
        assertNull(later.bitmap("satellite", other))
        assertNull(later.bitmap("satellite", TileId(15, 7362, 14580)))
        assertEquals(2, asked.size)
        assertFalse(later.has("satellite", other))
        File(root, "satellite").deleteRecursively()
    }

    @Test
    fun anAreaIsDownloadedOnceAndStopsWhenThereIsNoInternet() {
        val root = Files.createTempDirectory("tiles").toFile()
        var fetched = 0
        var online = true
        val store = TileStore(root, { MapSettings() }, fetch = { fetched++; if (online) png(Color.BLUE) else null }, io = Inline())
        val tiles = TileMath.around(19.4326, -99.1332, 0.4, 14..15)
        var last = Triple(0, 0, 0)
        val done = store.downloadAll("street", tiles, AtomicBoolean(false)) { d, total, failed -> last = Triple(d, total, failed) }
        assertEquals(tiles.size, done)
        assertEquals(Triple(tiles.size, tiles.size, 0), last)
        assertEquals(tiles.size, store.stored("street").first)
        // Again: everything is there, nothing is fetched.
        val before = fetched
        assertEquals(tiles.size, store.downloadAll("street", tiles, AtomicBoolean(false)) { _, _, _ -> })
        assertEquals(before, fetched)
        // A larger area with no internet gives up after a few failures instead of trying them all.
        online = false
        val more = TileMath.around(19.4326, -99.1332, 3.0, 16..16)
        store.downloadAll("street", more, AtomicBoolean(false)) { _, _, _ -> }
        assertTrue(fetched - before <= TileStore.GIVE_UP_AFTER)
        store.clear("street")
        assertEquals(0, store.stored("street").first)
    }

    @Test
    fun theSettingsSurviveAndBadZoomsAreClamped() {
        val dir = Files.createTempDirectory("map").toFile()
        assertEquals(MapSettings(), MapSettings.load(dir)) // no file yet
        assertTrue(MapSettings.save(dir, MapSettings(style = "street", zoom = 14, visible = false, lastLat = 19.4, lastLon = -99.1)))
        val back = MapSettings.load(dir)
        assertEquals("street", back.style)
        assertFalse(back.satellite)
        assertEquals(14, back.zoom)
        assertFalse(back.visible)
        assertEquals(19.4, back.lastLat, 1e-9)
        File(dir, MapSettings.FILE_NAME).writeText("""{"zoom": 40}""")
        assertEquals(TileMath.MAX_ZOOM, MapSettings.load(dir).zoom)
    }
}
