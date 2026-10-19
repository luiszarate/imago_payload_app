package aero.imago.payloadcontrol.app

import aero.imago.payloadcontrol.core.AppJson
import aero.imago.payloadcontrol.core.TargetPosition
import kotlinx.serialization.Serializable
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.TimeZone
import java.util.UUID

/** A target position the operator kept, with when it was marked (ISO 8601, UTC). */
@Serializable
data class MarkedPoint(
    val id: String = UUID.randomUUID().toString(),
    val name: String,
    val time: String,
    val position: TargetPosition,
)

/**
 * The marked points: kept in the app's files as marked-points.json, exported as CSV for
 * spreadsheets and KML for Google Earth and most mapping apps.
 */
object MarkedPoints {
    fun load(file: File): List<MarkedPoint> =
        runCatching { AppJson.decodeFromString<List<MarkedPoint>>(file.readText()) }.getOrDefault(emptyList())

    fun save(file: File, points: List<MarkedPoint>) {
        // Written beside the old file, then renamed over it: a crash never leaves half a list.
        runCatching {
            val partial = File(file.parentFile, file.name + ".part")
            partial.writeText(AppJson.encodeToString(points))
            if (!partial.renameTo(file)) partial.delete()
        }
    }

    /** java.time needs Android 8; SimpleDateFormat works from Android 7. */
    fun timestamp(date: Date = Date()): String =
        SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss'Z'", Locale.US).apply { timeZone = TimeZone.getTimeZone("UTC") }
            .format(date)

    /** "Point N", one past the highest number used so far. */
    fun nextName(points: List<MarkedPoint>): String =
        "Point ${(points.maxOfOrNull { it.name.removePrefix("Point ").toIntOrNull() ?: 0 } ?: 0) + 1}"

    fun csv(points: List<MarkedPoint>): String = buildString {
        append("name,time_utc,latitude,longitude,altitude_m\n")
        for (point in points) {
            val p = point.position
            append(String.format(Locale.US, "%s,%s,%.5f,%.5f,%.0f\n", point.name.replace(",", " "), point.time,
                                 p.latitude, p.longitude, p.altitude))
        }
    }

    fun kml(points: List<MarkedPoint>): String {
        val placemarks = points.joinToString("\n") { point ->
            val p = point.position
            val name = point.name.replace("&", "&amp;").replace("<", "&lt;")
            String.format(Locale.US, "  <Placemark><name>%s</name><TimeStamp><when>%s</when></TimeStamp>" +
                          "<Point><coordinates>%.5f,%.5f,%.0f</coordinates></Point></Placemark>",
                          name, point.time, p.longitude, p.latitude, p.altitude)
        }
        return """<?xml version="1.0" encoding="UTF-8"?>
<kml xmlns="http://www.opengis.net/kml/2.2"><Document><name>Marked points</name>
$placemarks
</Document></kml>
"""
    }
}
