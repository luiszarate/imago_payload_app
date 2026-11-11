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
    /** A tiny datagram ~30 times a second keeps the headset's Wi-Fi out of power save, which
        otherwise delivers video and telemetry in bursts (the Android app has the same switch,
        off there since the first version stalled the Herelink; this one pokes the discard
        port, which does not). On by default here: the Quest has no other network to serve. */
    val keepWifiAwake: Boolean = true,
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
    /** "angle" (attitude setpoints: what the Orus L follows, the default) or "rate" (a closed
        loop of rates, for gimbals that take no angles). */
    val mode: String = "angle",
    /** Start with head tracking on as soon as the link is up. */
    val onAtStart: Boolean = false,
    val gainDpsPerDeg: Float = 0f,
    val maxRateDps: Float = 0f,
    val deadbandDeg: Float = 0f,
    val smoothingMs: Int = 0,
    val yawRangeDeg: Float = 0f,
    /** Start each axis with its rate reversed; the core then checks the direction against the
        gimbal's attitude and keeps whatever works (see imago_head_track_config). */
    val invertPitch: Boolean = false,
    val invertYaw: Boolean = false,
    /** How much less the gimbal turns per degree of head as the camera zooms in: the head's
        motion times zoom^-zoomCompensation. 0: the same at any zoom; 1 (the default here):
        the picture moves across the screen the same for a head turn at any zoom. */
    val zoomCompensation: Float = 1f,
    /** A clutch: the camera follows the head only while the left grip is held. */
    val clutch: Boolean = false,
) {
    /** imago_head_track_mode when switched on. */
    val modeRaw: Int get() = if (mode.equals("rate", ignoreCase = true)) 1 else 2
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
    /** Slow stick moves become angle setpoints (the Orus L ignores rates under ~2 deg/s). */
    val fineAim: Boolean = true,
)

/** The virtual screen, fixed in the room in front of where the headset started. */
@Serializable
data class PanelSettings(
    val distanceM: Float = 2.2f,
    val widthM: Float = 2.6f,
    /** Degrees below eye level, so the operator looks slightly down, as at a monitor. */
    val pitchDownDeg: Float = 8f,
    /** Show the room (the headset's cameras) around the screen, when the runtime offers it. */
    val passthrough: Boolean = true,
    /** How much the room is darkened around the screen with passthrough on: 0 none, 1 black. */
    val roomDim: Float = 0.35f,
    /** With head tracking on, the screen goes with the head instead of staying in the room. */
    val followHead: Boolean = true,
    /** The picture's edge darkens while the head turns fast. */
    val vignette: Boolean = true,
)

/**
 * version 1 files (no field) were written when head tracking's default was the rate loop,
 * which ran the Orus L to its stops (2026-11-09, 2026-11-10): reading one moves every profile
 * to angles and the core's present gain.
 */
@Serializable
data class ProfilesFile(val profiles: List<ConnectionProfile> = emptyList(), val version: Int = 1) {
    companion object {
        const val CURRENT = 2
    }
}

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
            save(dir, defaults)
            return defaults
        }
        return parsed.ifEmpty { defaults }
    }

    private fun migrate(file: ProfilesFile): List<ConnectionProfile> =
        if (file.version >= ProfilesFile.CURRENT) file.profiles
        else file.profiles.map { it.copy(headTracking = it.headTracking.copy(mode = "angle", gainDpsPerDeg = 0f)) }

    /** Writes the list; false if the file could not be written. */
    fun save(dir: File, profiles: List<ConnectionProfile>): Boolean =
        runCatching { File(dir, FILE_NAME).writeText(json.encodeToString(ProfilesFile(profiles, ProfilesFile.CURRENT))) }.isSuccess

    fun parse(text: String): List<ConnectionProfile> =
        migrate(json.decodeFromString<ProfilesFile>(text)).filter { it.validationError == null }
}
