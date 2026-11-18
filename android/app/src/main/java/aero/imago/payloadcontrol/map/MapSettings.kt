package aero.imago.payloadcontrol.map

import aero.imago.payloadcontrol.core.AppJson
import android.content.SharedPreferences
import androidx.core.content.edit
import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

/** The two kinds of map; `key` is also the tile folder's name. */
@Serializable
enum class MapStyle(val key: String, val title: String) {
    @SerialName("satellite") SATELLITE("satellite", "Satellite"),
    @SerialName("street") STREET("street", "Street map"),
}

/**
 * The map's settings: this device's, not a profile's, so in SharedPreferences beside the other
 * app settings. The tile URLs take {z}, {x} and {y}; point them at a provider whose terms allow
 * keeping its tiles for offline use. Defaults as the Quest app's.
 */
@Serializable
data class MapSettings(
    val visible: Boolean = true,
    val style: MapStyle = MapStyle.SATELLITE,
    val zoom: Int = 16,
    val streetUrl: String = DEFAULT_STREET_URL,
    val streetCredit: String = DEFAULT_STREET_CREDIT,
    val satelliteUrl: String = DEFAULT_SATELLITE_URL,
    val satelliteCredit: String = DEFAULT_SATELLITE_CREDIT,
    /** Where the aircraft was last seen: what Settings offers to download around. 0, 0 = never. */
    val lastLat: Double = 0.0,
    val lastLon: Double = 0.0,
    val downloadRadiusKm: Float = 3f,
    /** Area downloads also fetch zoom 17 (about three times the tiles); off: up to zoom 16. */
    val downloadDetail: Boolean = false,
) {
    fun url(style: MapStyle): String = if (style == MapStyle.STREET) streetUrl else satelliteUrl
    fun credit(style: MapStyle): String = if (style == MapStyle.STREET) streetCredit else satelliteCredit

    val lastPosition: Pair<Double, Double>?
        get() = if (lastLat == 0.0 && lastLon == 0.0) null else lastLat to lastLon

    fun save(prefs: SharedPreferences) {
        prefs.edit { putString(KEY, AppJson.encodeToString(this@MapSettings)) }
    }

    /** Only the tile servers back to the defaults. */
    fun withDefaultServers() = copy(
        streetUrl = DEFAULT_STREET_URL, streetCredit = DEFAULT_STREET_CREDIT,
        satelliteUrl = DEFAULT_SATELLITE_URL, satelliteCredit = DEFAULT_SATELLITE_CREDIT,
    )

    companion object {
        private const val KEY = "map.settings"
        const val DEFAULT_STREET_URL = "https://tile.openstreetmap.org/{z}/{x}/{y}.png"
        const val DEFAULT_STREET_CREDIT = "© OpenStreetMap contributors"
        const val DEFAULT_SATELLITE_URL =
            "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}"
        const val DEFAULT_SATELLITE_CREDIT = "Esri, Maxar, Earthstar Geographics"
        const val RADIUS_MIN_KM = 0.2f
        const val RADIUS_MAX_KM = 20f

        fun load(prefs: SharedPreferences): MapSettings = decode(prefs.getString(KEY, null))

        /** Saved JSON, with fields added later at their defaults and the zoom kept in range. */
        fun decode(json: String?): MapSettings {
            val settings = json?.let { runCatching { AppJson.decodeFromString<MapSettings>(it) }.getOrNull() }
                ?: return MapSettings()
            return settings.copy(zoom = settings.zoom.coerceIn(TileMath.MIN_ZOOM, TileMath.MAX_ZOOM))
        }

        /**
         * What is wrong with a tile URL template, or null. https only: Android blocks plain
         * http for this app (no cleartext allowed), so an http server would never answer.
         */
        fun urlProblem(template: String): String? {
            val url = template.trim()
            return when {
                !url.lowercase().startsWith("https://") -> "The address must start with https://"
                listOf("{z}", "{x}", "{y}").any { it !in url } -> "The address needs {z}, {x} and {y}"
                else -> null
            }
        }
    }
}
