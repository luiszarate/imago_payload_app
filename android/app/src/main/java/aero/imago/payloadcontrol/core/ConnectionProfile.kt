package aero.imago.payloadcontrol.core

import android.content.SharedPreferences
import androidx.core.content.edit
import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import java.util.UUID

/**
 * How the app reaches one payload. Like Payload Plus, control and video are configured
 * separately: MAVLink goes to the Orus L, video usually comes from the datalink (Herelink).
 */
@Serializable
data class ConnectionProfile(
    val id: String = UUID.randomUUID().toString(),
    val name: String = "Payload",
    val mavlinkMode: MavlinkMode = MavlinkMode.DIRECT,
    val payloadIP: String = "",
    val mavlinkPort: Int = 14566,
    /** 0 = default (same as mavlinkPort in direct mode, 14550 in listen mode). */
    val localPort: Int = 0,
    /** Empty = use the URL the payload announces over MAVLink. */
    val videoURL: String = "",
    val videoTransport: VideoTransport = VideoTransport.AUTO,
    val gimbalProtocol: GimbalProtocol = GimbalProtocol.ATTITUDE,
    /** Sends RC_MODE = standard when the link comes up. Off until confirmed on the firmware. */
    val setStandardRCMode: Boolean = false,
    /** Restart the video when it stays this far behind live (a queue upstream). 0 = never. */
    val maxVideoLagMs: Int = 500,
    /** Keeps the device's Wi-Fi from dozing between video packets (which delivers them, and
        telemetry, in bursts) with a 1-byte datagram ~30 times a second to the video host's
        discard port. Off by default: the first version (RTCP to the stream) stalled the
        Herelink's video, and its stored "on" lives under the old key, which is ignored. */
    val keepWifiAwake: Boolean = false,
    /** Playout smoothing in ms: shows frames that arrive in bursts at the stream's own pace,
        adding up to this much delay. 0 = every frame at once (lowest latency). */
    val videoSmoothingMs: Int = 0,
) {
    @Serializable
    enum class MavlinkMode(val title: String) {
        /** Send to the payload's IP (Herelink Ethernet bridge, direct cable). */
        @SerialName("direct") DIRECT("Payload IP"),
        /** Listen on a local port and answer whoever sends MAVLink (Herelink forwarding). */
        @SerialName("listen") LISTEN("Listen (forwarded)"),
    }

    @Serializable
    enum class VideoTransport(val raw: Int, val title: String) {
        @SerialName("auto") AUTO(Imago.RTSP_AUTO, "Auto"),
        @SerialName("udp") UDP(Imago.RTSP_UDP, "UDP"),
        @SerialName("tcp") TCP(Imago.RTSP_TCP, "TCP"),
    }

    @Serializable
    enum class GimbalProtocol(val raw: Int, val title: String) {
        /** GIMBAL_DEVICE_SET_ATTITUDE, as the Gremsy SDK sends it. */
        @SerialName("attitude") ATTITUDE(Imago.GIMBAL_PROTOCOL_V2, "Gimbal v2 (direct)"),
        /** MAV_CMD_DO_MOUNT_CONTROL, the older MAVLink gimbal protocol. */
        @SerialName("mountControl") MOUNT_CONTROL(Imago.GIMBAL_PROTOCOL_V1, "Mount control"),
        /** Through the autopilot's gimbal manager (PX4, MAVLink gimbal protocol v2). */
        @SerialName("manager") MANAGER(Imago.GIMBAL_PROTOCOL_MANAGER, "Autopilot gimbal manager"),
    }

    /** Problem with the profile, or null if it can be used. */
    val validationError: String?
        get() {
            if (name.isBlank()) return "Enter a name."
            if (mavlinkMode == MavlinkMode.DIRECT && !isIPv4(payloadIP)) {
                return "Enter the payload IP address, e.g. 192.168.144.240."
            }
            val url = videoURL.trim()
            if (url.isNotEmpty() && !url.lowercase().startsWith("rtsp://")) {
                return "The video URL must start with rtsp://"
            }
            if (mavlinkMode == MavlinkMode.LISTEN && url.isEmpty()) {
                return "Listen mode needs a video URL."
            }
            if (mavlinkPort !in 1..65535 || localPort !in 0..65535) return "Ports go from 1 to 65535."
            return null
        }

    companion object {
        val videoLagChoices = listOf(0, 300, 500, 1000)
        val smoothingChoices = listOf(0, 50, 100, 200)

        /** Field setup confirmed with the user: Orus L behind Herelink, video from Herelink. */
        val herelink = ConnectionProfile(
            name = "Orus L · Herelink",
            payloadIP = "192.168.144.240",
            videoURL = "rtsp://192.168.43.1:8554/fpv_stream",
        )

        /** Orus L on a direct Ethernet link, video announced by the payload. */
        val direct = ConnectionProfile(name = "Orus L · Direct", payloadIP = "192.168.55.1")

        /** tools/orus-sim on the development computer, seen from the Android emulator (10.0.2.2
            is the host). The local port differs from the simulator's 14566. */
        val simulator = ConnectionProfile(
            name = "orus-sim (emulator host)",
            payloadIP = "10.0.2.2",
            localPort = 14567,
        )

        fun isIPv4(text: String): Boolean {
            val parts = text.trim().split(".")
            return parts.size == 4 && parts.all { part ->
                part.isNotEmpty() && part.length <= 3 && part.all(Char::isDigit) && part.toInt() in 0..255
            }
        }
    }
}

/** Shared JSON settings: fields added in later versions get their defaults. */
val AppJson = Json {
    ignoreUnknownKeys = true
    coerceInputValues = true
    encodeDefaults = true
}

/** Saved profiles, persisted in SharedPreferences as JSON. */
class ProfileStore(private val prefs: SharedPreferences) {
    fun load(): List<ConnectionProfile> {
        val json = prefs.getString(PROFILES_KEY, null) ?: return defaults()
        return runCatching { AppJson.decodeFromString<List<ConnectionProfile>>(json) }
            .getOrNull()?.takeIf { it.isNotEmpty() } ?: defaults()
    }

    fun save(profiles: List<ConnectionProfile>) {
        prefs.edit { putString(PROFILES_KEY, AppJson.encodeToString(profiles)) }
    }

    var selectedId: String?
        get() = prefs.getString(SELECTED_KEY, null)
        set(value) = prefs.edit { putString(SELECTED_KEY, value) }

    private fun defaults() = listOf(ConnectionProfile.herelink, ConnectionProfile.direct,
                                    ConnectionProfile.simulator)

    private companion object {
        const val PROFILES_KEY = "connectionProfiles.v1"
        const val SELECTED_KEY = "connectionProfiles.selected"
    }
}
