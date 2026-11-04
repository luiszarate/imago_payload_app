package aero.imago.payloadcontrol.quest

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import java.io.File

/**
 * How the headset reaches one payload, plus the head-tracking, control and panel settings
 * that go with it. There is no settings screen in VR: the operator edits `profiles.json` in
 * the app's external files folder (adb or MTP) and the left menu button cycles the profiles.
 * The fields mirror the Android app's ConnectionProfile so a profile can be copied over.
 */
@Serializable
data class ConnectionProfile(
    val name: String = "Payload",
    /** "direct": send to payloadIP; "listen": answer whoever sends MAVLink (Herelink). */
    val mavlinkMode: String = "direct",
    val payloadIP: String = "",
    val mavlinkPort: Int = 14566,
    /** 0 = default (mavlinkPort in direct mode, 14550 in listen mode). */
    val localPort: Int = 0,
    /** Empty = the URL the payload announces over MAVLink. */
    val videoURL: String = "",
    /** "auto", "udp" or "tcp". */
    val videoTransport: String = "auto",
    /** "attitude" (GIMBAL_DEVICE_SET_ATTITUDE), "mountControl" or "manager" (PX4). */
    val gimbalProtocol: String = "attitude",
    /** Restart the video when it stays this far behind live. 0 = never. */
    val maxVideoLagMs: Int = 500,
    /** Playout smoothing, ms. 0 = every frame at once. */
    val videoSmoothingMs: Int = 0,
    val headTracking: HeadTrackingSettings = HeadTrackingSettings(),
    val controls: ControlSettings = ControlSettings(),
    val panel: PanelSettings = PanelSettings(),
) {
    val isListen: Boolean get() = mavlinkMode.equals("listen", ignoreCase = true)

    val transportRaw: Int
        get() = when (videoTransport.lowercase()) {
            "udp" -> 1
            "tcp" -> 2
            else -> 0
        }

    /** imago_gimbal_protocol */
    val gimbalProtocolRaw: Int
        get() = when (gimbalProtocol.lowercase()) {
            "mountcontrol" -> 1
            "manager" -> 2
            else -> 0
        }

    /** Why the profile cannot be used, or null. */
    val validationError: String?
        get() {
            if (!isListen && !isIPv4(payloadIP)) return "payloadIP must be an IPv4 address"
            if (videoURL.isNotEmpty() && !videoURL.lowercase().startsWith("rtsp://")) return "videoURL must start with rtsp://"
            if (isListen && videoURL.isEmpty()) return "listen mode needs a videoURL"
            if (mavlinkPort !in 1..65535 || localPort !in 0..65535) return "ports go from 1 to 65535"
            return null
        }

    companion object {
        fun isIPv4(text: String): Boolean {
            val parts = text.trim().split('.')
            return parts.size == 4 && parts.all { it.toIntOrNull()?.let { n -> n in 0..255 } == true }
        }
    }
}

/** Values of the core's imago_head_track_config; 0 keeps the core's default. */
@Serializable
data class HeadTrackingSettings(
    /** "rate" (closed loop on the gimbal's attitude, the default) or "angle" (setpoints). */
    val mode: String = "rate",
    /** Start with head tracking on as soon as the link is up. */
    val onAtStart: Boolean = false,
    val gainDpsPerDeg: Float = 0f,
    val maxRateDps: Float = 0f,
    val deadbandDeg: Float = 0f,
    val smoothingMs: Int = 0,
    val yawRangeDeg: Float = 0f,
) {
    /** imago_head_track_mode when switched on. */
    val modeRaw: Int get() = if (mode.equals("angle", ignoreCase = true)) 2 else 1
}

/** The right thumbstick as a gimbal joystick (same shaping as the other apps). */
@Serializable
data class ControlSettings(
    val maxYawDps: Float = 60f,
    val maxPitchDps: Float = 60f,
    val deadzone: Float = 0.08f,
    val expo: Float = 0.55f,
    /** Rate x zoom^-compensation: 0 same speed at any zoom, 1 same picture speed on screen. */
    val zoomCompensation: Float = 0.5f,
    /** While head tracking is on the stick trims the aim at this fraction of the speed. */
    val trimFactor: Float = 0.25f,
)

/** The virtual screen, fixed in the room in front of where the headset started. */
@Serializable
data class PanelSettings(
    val distanceM: Float = 2.2f,
    val widthM: Float = 2.6f,
    /** Degrees below eye level, so the operator looks slightly down, as at a monitor. */
    val pitchDownDeg: Float = 8f,
)

@Serializable
data class ProfilesFile(val profiles: List<ConnectionProfile> = emptyList())

object Profiles {
    const val FILE_NAME = "profiles.json"

    val json = Json {
        ignoreUnknownKeys = true
        prettyPrint = true
        encodeDefaults = true
    }

    /** What a fresh install starts with: the Herelink feed, and a direct cable to the payload. */
    val defaults: List<ConnectionProfile> = listOf(
        ConnectionProfile(
            name = "Herelink (forwarded)",
            mavlinkMode = "listen",
            videoURL = "rtsp://192.168.43.1:8554/fpv_stream",
        ),
        ConnectionProfile(
            name = "Orus L direct",
            mavlinkMode = "direct",
            payloadIP = "192.168.144.240",
        ),
        ConnectionProfile(
            name = "orus-sim on this network",
            mavlinkMode = "direct",
            payloadIP = "192.168.1.10",
            headTracking = HeadTrackingSettings(onAtStart = true),
        ),
    )

    /** Reads the file, or writes the defaults and returns them. Invalid profiles are dropped. */
    fun load(dir: File): List<ConnectionProfile> {
        val file = File(dir, FILE_NAME)
        val parsed = runCatching { parse(file.readText()) }.getOrNull()
        if (parsed == null) {
            runCatching { file.writeText(json.encodeToString(ProfilesFile(defaults))) }
            return defaults
        }
        return parsed.ifEmpty { defaults }
    }

    fun parse(text: String): List<ConnectionProfile> =
        json.decodeFromString<ProfilesFile>(text).profiles.filter { it.validationError == null }
}
