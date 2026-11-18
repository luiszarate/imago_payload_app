package aero.imago.payloadcontrol

import aero.imago.payloadcontrol.app.MapController
import aero.imago.payloadcontrol.core.AppJson
import aero.imago.payloadcontrol.map.DownloadOutcome
import aero.imago.payloadcontrol.map.MapSettings
import aero.imago.payloadcontrol.map.MapStyle
import aero.imago.payloadcontrol.map.StoredTiles
import aero.imago.payloadcontrol.map.TileId
import aero.imago.payloadcontrol.map.TileMath
import aero.imago.payloadcontrol.map.TileStore
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import java.nio.file.Files
import java.util.concurrent.Executor
import java.util.concurrent.atomic.AtomicBoolean

class TileMathTest {
    @Test
    fun tileArithmetic() {
        // The whole world is one tile at zoom 0, and Greenwich on the equator is its centre.
        assertEquals(0.5, TileMath.x(0.0, 0), 1e-9)
        assertEquals(0.5, TileMath.y(0.0, 0), 1e-9)
        // Mexico City at zoom 15 (the standard slippy-map formula, as in the Quest's test).
        assertEquals(7360, TileMath.x(-99.1332, 15).toInt())
        assertEquals(14580, TileMath.y(19.4326, 15).toInt())
        // There and back.
        assertEquals(19.4326, TileMath.lat(TileMath.y(19.4326, 16), 16), 1e-6)
        assertEquals(-99.1332, TileMath.lon(TileMath.x(-99.1332, 16), 16), 1e-6)
        // A pixel at zoom 16 there is a little over two metres.
        assertEquals(2.25, TileMath.metresPerPixel(19.4326, 16), 0.05)
    }

    @Test
    fun anAreaCoversItsCentreAtEveryZoom() {
        val far = TileMath.around(19.4326, -99.1332, 3.0, 11..11)
        val near = TileMath.around(19.4326, -99.1332, 3.0, 16..16)
        assertTrue(far.size in 1..4)
        assertTrue(near.size.toString(), near.size in 100..200)
        assertTrue(TileId(16, TileMath.x(-99.1332, 16).toInt(), TileMath.y(19.4326, 16).toInt()) in near)
    }

    @Test
    fun aDownloadStopsAtZoom16UnlessTheClosestZoomIsAsked() {
        val plain = TileMath.plan(19.4326, -99.1332, 3.0, detail = false, maxTiles = TileStore.MAX_DOWNLOAD)
        assertEquals(16, plain.topZoom)
        assertEquals(TileMath.around(19.4326, -99.1332, 3.0, 11..16), plain.tiles)
        val detail = TileMath.plan(19.4326, -99.1332, 3.0, detail = true, maxTiles = TileStore.MAX_DOWNLOAD)
        assertEquals(17, detail.topZoom)
        assertEquals(TileMath.around(19.4326, -99.1332, 3.0, 11..17), detail.tiles)
        // Zoom 17 alone is most of the tiles: about three times as many with it.
        assertTrue("${plain.tiles.size} -> ${detail.tiles.size}", detail.tiles.size > plain.tiles.size * 3)
    }

    @Test
    fun aLargeAreaLeavesOutItsClosestZoomsButNotBelow13() {
        // 10 km at 17 is far over the limit: it drops zooms until it fits.
        val large = TileMath.plan(19.4326, -99.1332, 10.0, detail = true, maxTiles = TileStore.MAX_DOWNLOAD)
        assertTrue(large.topZoom in 13..16)
        assertTrue(large.tiles.size <= TileStore.MAX_DOWNLOAD)
        assertTrue(TileMath.around(19.4326, -99.1332, 10.0, 11..large.topZoom + 1).size > TileStore.MAX_DOWNLOAD)
        // Whatever the limit, zoom 13 stays.
        val tiny = TileMath.plan(19.4326, -99.1332, 10.0, detail = false, maxTiles = 1)
        assertEquals(13, tiny.topZoom)
        assertEquals(TileMath.around(19.4326, -99.1332, 10.0, 11..13), tiny.tiles)
    }

    @Test
    fun urlTemplates() {
        assertEquals("https://t.example/15/14580/7360", TileMath.url("https://t.example/{z}/{y}/{x}", TileId(15, 7360, 14580)))
    }
}

class MapSettingsTest {
    @Test
    fun defaultsAreTheQuests() {
        val defaults = MapSettings.decode(null)
        assertEquals(MapSettings(), defaults)
        assertTrue(defaults.visible)
        assertEquals(MapStyle.SATELLITE, defaults.style)
        assertEquals(16, defaults.zoom)
        assertEquals("https://tile.openstreetmap.org/{z}/{x}/{y}.png", defaults.url(MapStyle.STREET))
        assertEquals("© OpenStreetMap contributors", defaults.credit(MapStyle.STREET))
        assertEquals("https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}",
                     defaults.url(MapStyle.SATELLITE))
        assertEquals("Esri, Maxar, Earthstar Geographics", defaults.credit(MapStyle.SATELLITE))
        assertNull(defaults.lastPosition)
    }

    @Test
    fun savedSettingsComeBackAndBadValuesAreTamed() {
        val saved = MapSettings(visible = false, style = MapStyle.STREET, zoom = 14, lastLat = 19.4, lastLon = -99.1)
        val back = MapSettings.decode(AppJson.encodeToString(saved))
        assertEquals(saved, back)
        assertEquals(19.4 to -99.1, back.lastPosition)
        assertEquals(TileMath.MAX_ZOOM, MapSettings.decode("""{"zoom": 40}""").zoom)
        assertEquals(TileMath.MIN_ZOOM, MapSettings.decode("""{"zoom": 2}""").zoom)
        // An unknown style falls back to the default; damaged JSON gives the defaults.
        assertEquals(MapStyle.SATELLITE, MapSettings.decode("""{"style": "terrain", "visible": false}""").style)
        assertEquals(MapSettings(), MapSettings.decode("not json"))
        // Saved before the closest-zoom switch: off.
        assertFalse(MapSettings.decode("""{"zoom": 15, "style": "street"}""").downloadDetail)
        assertTrue(MapSettings.decode(AppJson.encodeToString(MapSettings(downloadDetail = true))).downloadDetail)
    }

    @Test
    fun tileServerAddressesAreChecked() {
        assertNull(MapSettings.urlProblem(MapSettings.DEFAULT_STREET_URL))
        assertNull(MapSettings.urlProblem(MapSettings.DEFAULT_SATELLITE_URL))
        assertNotNull(MapSettings.urlProblem("http://tile.example/{z}/{x}/{y}.png"))
        assertNotNull(MapSettings.urlProblem("https://tile.example/{z}/{x}.png"))
        val custom = MapSettings(streetUrl = "https://x/{z}/{x}/{y}", streetCredit = "X").withDefaultServers()
        assertEquals(MapSettings(), custom)
    }

    @Test
    fun downloadAreasAreChecked() {
        assertNull(MapController.problem(19.43, -99.13, 3f))
        assertNotNull(MapController.problem(89.0, -99.13, 3f))
        assertNotNull(MapController.problem(19.43, 181.0, 3f))
        assertNotNull(MapController.problem(19.43, -99.13, 0f))
        assertNotNull(MapController.problem(19.43, -99.13, 50f))
        assertNotNull(MapController.problem(Double.NaN, -99.13, 3f))
        assertNotNull(MapController.problem(19.43, Double.POSITIVE_INFINITY, 3f))
        assertNotNull(MapController.problem(19.43, -99.13, Float.POSITIVE_INFINITY))
        assertNotNull(MapController.problem(19.43, -99.13, Float.NaN))
    }

    @Test
    fun downloadMessages() {
        assertEquals("Satellite: done, all 10 tiles stored (zoom 11 to 16).",
                     MapController.message(MapStyle.SATELLITE, DownloadOutcome(10, 10, 0, false, false), 16))
        assertTrue(MapController.message(MapStyle.STREET, DownloadOutcome(2, 10, 8, false, true), 16).contains("no internet"))
        assertEquals("Street map: stopped. 3 of 10 tiles stored.",
                     MapController.message(MapStyle.STREET, DownloadOutcome(3, 10, 0, true, false), 17))
        assertEquals("None", MapController.storedText(StoredTiles(0, 0)))
        assertEquals("1,234 tiles · 2.0 MB", MapController.storedText(StoredTiles(1234, 2 * 1_048_576L)))
    }
}

class TileStoreTest {
    private val inline = Executor { it.run() }
    /** The first bytes of a PNG: enough for the store to take it for one. */
    private val png = byteArrayOf(0x89.toByte(), 0x50, 0x4E, 0x47, 0x0D, 0x0A)

    @Test
    fun onlyPicturesAreTiles() {
        assertTrue(TileStore.isPicture(png))
        assertTrue(TileStore.isPicture(byteArrayOf(0xFF.toByte(), 0xD8.toByte(), 0xFF.toByte(), 0xE0.toByte())))
        assertFalse(TileStore.isPicture("<html><body>Log in to this Wi-Fi</body></html>".toByteArray()))
        assertFalse(TileStore.isPicture(byteArrayOf()))
        assertFalse(TileStore.isPicture(ByteArray(TileStore.MAX_TILE_BYTES + 1).also { png.copyInto(it) }))
    }

    @Test
    fun aLoginPageIsNotStoredAsATile() {
        val root = Files.createTempDirectory("tiles").toFile()
        val store = TileStore(root, "test", fetch = { "<html>Sign in</html>".toByteArray() }, decode = { null },
                              io = inline, politeMs = 0)
        val tiles = TileMath.around(19.4326, -99.1332, 0.2, 15..15)
        val outcome = store.downloadAll(MapStyle.STREET, "https://t.example/{z}/{x}/{y}.png", tiles, AtomicBoolean(false)) { _, _, _ -> }
        assertEquals(0, outcome.stored)
        assertEquals(tiles.size.coerceAtMost(TileStore.GIVE_UP_AFTER), outcome.failed)
        assertEquals(StoredTiles(0, 0), store.stored(MapStyle.STREET))
        root.deleteRecursively()
    }

    @Test
    fun aCompleteRunWhoseLastTilesFailedDidNotGiveUp() {
        val root = Files.createTempDirectory("tiles").toFile()
        val tiles = TileMath.around(19.4326, -99.1332, 1.0, 15..16)
        assertTrue(tiles.size > TileStore.GIVE_UP_AFTER + 2)
        val failFrom = tiles.size - TileStore.GIVE_UP_AFTER // the last eight fail
        var asked = 0
        val store = TileStore(root, "test", fetch = { if (asked++ < failFrom) png else null }, decode = { null },
                              io = inline, politeMs = 0)
        val outcome = store.downloadAll(MapStyle.STREET, "https://t.example/{z}/{x}/{y}.png", tiles, AtomicBoolean(false)) { _, _, _ -> }
        assertEquals(failFrom, outcome.stored)
        assertEquals(TileStore.GIVE_UP_AFTER, outcome.failed)
        assertFalse(outcome.gaveUp)
        assertFalse(outcome.cancelled)
        root.deleteRecursively()
    }

    @Test
    fun anAreaIsDownloadedOnceAndStopsWhenThereIsNoInternet() {
        val root = Files.createTempDirectory("tiles").toFile()
        val asked = ArrayList<String>()
        var online = true
        val store = TileStore(root, "test", fetch = { url -> asked += url; if (online) png else null },
                              decode = { null }, io = inline, politeMs = 0)
        val tiles = TileMath.around(19.4326, -99.1332, 0.4, 14..15)
        var last = Triple(0, 0, 0)
        val outcome = store.downloadAll(MapStyle.STREET, "https://t.example/{z}/{x}/{y}.png", tiles, AtomicBoolean(false)) { d, total, failed ->
            last = Triple(d, total, failed)
        }
        assertEquals(DownloadOutcome(tiles.size, tiles.size, 0, cancelled = false, gaveUp = false), outcome)
        assertEquals(Triple(tiles.size, tiles.size, 0), last)
        assertEquals(StoredTiles(tiles.size, png.size.toLong() * tiles.size), store.stored(MapStyle.STREET))
        assertTrue(store.has(MapStyle.STREET, tiles.first()))
        assertFalse(store.has(MapStyle.SATELLITE, tiles.first()))
        val first = tiles.first()
        assertTrue("https://t.example/${first.z}/${first.x}/${first.y}.png" in asked)
        assertTrue(File(root, "street/${first.z}/${first.x}/${first.y}").isFile)

        // Again: everything is there, nothing is fetched.
        val before = asked.size
        store.downloadAll(MapStyle.STREET, "https://t.example/{z}/{x}/{y}.png", tiles, AtomicBoolean(false)) { _, _, _ -> }
        assertEquals(before, asked.size)

        // A larger area with no internet gives up after a few failures instead of trying them all.
        online = false
        val more = TileMath.around(19.4326, -99.1332, 3.0, 16..16)
        val offline = store.downloadAll(MapStyle.STREET, "https://t.example/{z}/{x}/{y}.png", more, AtomicBoolean(false)) { _, _, _ -> }
        assertTrue(offline.gaveUp)
        assertTrue(asked.size - before <= TileStore.GIVE_UP_AFTER)

        store.clear(MapStyle.STREET)
        assertEquals(StoredTiles(0, 0), store.stored(MapStyle.STREET))
        root.deleteRecursively()
    }

    @Test
    fun aStoppedDownloadStops() {
        val root = Files.createTempDirectory("tiles").toFile()
        val cancelled = AtomicBoolean(false)
        val store = TileStore(root, "test", fetch = { png }, decode = { null }, io = inline, politeMs = 0)
        val tiles = TileMath.around(19.4326, -99.1332, 1.0, 15..16)
        val outcome = store.downloadAll(MapStyle.SATELLITE, "https://t.example/{z}/{x}/{y}", tiles, cancelled) { done, _, _ ->
            if (done == 3) cancelled.set(true)
        }
        assertTrue(outcome.cancelled)
        assertEquals(3, outcome.stored)
        root.deleteRecursively()
    }

    @Test
    fun theMapNeverFetchesAndAMissingTileIsNotLookedForEachFrame() {
        val root = Files.createTempDirectory("tiles").toFile()
        var fetched = 0
        var decoded = 0
        val store = TileStore(root, "test", fetch = { fetched++; byteArrayOf(1) }, decode = { decoded++; null },
                              io = inline, politeMs = 0)
        val id = TileId(15, 7360, 14580)
        assertNull(store.bitmap(MapStyle.SATELLITE, id))
        assertNull(store.bitmap(MapStyle.SATELLITE, id))
        assertEquals(0, fetched) // flying: only the store
        assertEquals(0, decoded) // not on disk: nothing to decode, and not looked for again at once
        root.deleteRecursively()
    }
}
