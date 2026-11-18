package aero.imago.payloadcontrol.map

import kotlin.math.PI
import kotlin.math.atan
import kotlin.math.cos
import kotlin.math.floor
import kotlin.math.ln
import kotlin.math.sinh
import kotlin.math.tan

/** One Web-Mercator tile (256 px): zoom, column, row. */
data class TileId(val z: Int, val x: Int, val y: Int)

/** What an area download fetches: every zoom from the widest to `topZoom`. */
data class DownloadPlan(val topZoom: Int, val tiles: List<TileId>)

/** The slippy-map arithmetic every tile server shares (as the Quest app's MapTiles.kt). */
object TileMath {
    /** The map on the picture zooms between these. */
    const val MIN_ZOOM = 10
    const val MAX_ZOOM = 18

    /** An offline download stores from this zoom (the whole area)... */
    const val DOWNLOAD_MIN_ZOOM = 11
    /** ...to this one by default... */
    const val DOWNLOAD_TOP_ZOOM = 16
    /**
     * ...or to this one with Closest zoom too: zoom 17 alone is some 70 % of the tiles and of
     * the wait (operator, 2026-11-18), so it is only fetched on request.
     */
    const val DETAIL_TOP_ZOOM = 17
    /** A large area drops its closest zooms to fit the limit, but never below this one. */
    const val PLAN_FLOOR_ZOOM = 13

    const val TILE_PIXELS = 256

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

    /**
     * The tiles of an area download: up to zoom 16, or 17 with `detail`; while that is more
     * than `maxTiles`, the closest zoom is left out, down to zoom 13 at the least.
     */
    fun plan(latDeg: Double, lonDeg: Double, radiusKm: Double, detail: Boolean, maxTiles: Int): DownloadPlan {
        var top = if (detail) DETAIL_TOP_ZOOM else DOWNLOAD_TOP_ZOOM
        var tiles = around(latDeg, lonDeg, radiusKm, DOWNLOAD_MIN_ZOOM..top)
        while (tiles.size > maxTiles && top > PLAN_FLOOR_ZOOM) {
            top--
            tiles = around(latDeg, lonDeg, radiusKm, DOWNLOAD_MIN_ZOOM..top)
        }
        return DownloadPlan(top, tiles)
    }

    /** A tile URL from a template with {z}, {x} and {y}. */
    fun url(template: String, id: TileId): String =
        template.replace("{z}", id.z.toString()).replace("{x}", id.x.toString()).replace("{y}", id.y.toString())
}
