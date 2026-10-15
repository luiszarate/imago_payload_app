package aero.imago.payloadcontrol.core

import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.roundToInt

// Kotlin views of the core's state snapshot, split by topic so each composable only
// recomposes when its own part changes.

enum class GimbalMode(val raw: Int, val title: String) {
    OFF(0, "Off"), LOCK(1, "Lock"), FOLLOW(2, "Follow"), MAPPING(3, "Mapping"), RESET(4, "Reset");

    companion object {
        fun of(raw: Int) = entries.firstOrNull { it.raw == raw }
    }
}

enum class ViewSource(val raw: Int, val title: String) {
    EO_IR(0, "EO + IR"), EO(1, "EO"), IR(2, "IR"), IR_EO(3, "IR + EO"), SYNC(4, "Sync"),
    SIDE_BY_SIDE(6, "Side by side");

    companion object {
        fun of(raw: Int) = entries.firstOrNull { it.raw == raw }
    }
}

enum class CameraMode(val raw: Int) {
    PHOTO(0), VIDEO(1);

    companion object {
        fun of(raw: Int) = entries.firstOrNull { it.raw == raw }
    }
}

enum class TrackingState(val raw: Int) {
    IDLE(0), TRACKED(1), LOST(2);

    companion object {
        fun of(raw: Int) = entries.firstOrNull { it.raw == raw } ?: IDLE
    }
}

/** Normalized image rectangle: (0, 0) top-left, (1, 1) bottom-right. */
data class NormRect(val x: Float, val y: Float, val w: Float, val h: Float) {
    val midX get() = x + w / 2
    val midY get() = y + h / 2

    fun contains(px: Float, py: Float, slack: Float = 0f) =
        px >= x - slack && px <= x + w + slack && py >= y - slack && py <= y + h + slack
}

data class LinkStatus(
    val up: Boolean = false,
    val peer: String = "",
    val roundTripMs: Int = 0,
    val cameraComponent: Int = 0,
    val gimbalComponent: Int = 0,
)

/** Who holds the autopilot's gimbal manager (only with that gimbal protocol). */
sealed interface GimbalControl {
    data object Unknown : GimbalControl // no gimbal manager heard
    data object ThisApp : GimbalControl
    data object Nobody : GimbalControl
    data class Other(val system: Int, val component: Int) : GimbalControl

    /** "Autopilot", "Ground station"... for the rail. */
    val holder: String
        get() = when (this) {
            Unknown -> "No gimbal manager"
            ThisApp -> "This app"
            Nobody -> "Nobody"
            is Other -> when {
                component == 1 -> "Autopilot"
                system == 255 || component == 190 -> "Ground station"
                else -> "$system/$component"
            }
        }
}

data class GimbalStatus(
    val valid: Boolean = false,
    /** Degrees, rounded to 0.1. Relative to the horizon, positive up. */
    val pitch: Float = 0f,
    /** Relative to the aircraft's nose, positive right. null when unknown. */
    val yaw: Float? = null,
    /** Relative to North, 0..<360. */
    val yawNorth: Float? = null,
    val mode: GimbalMode? = null,
    val control: GimbalControl = GimbalControl.Unknown,
)

data class CameraStatus(
    val view: ViewSource? = null,
    val mode: CameraMode? = null,
    val zoom: Float = 0f,
    /** Top of the EO zoom range in the camera's zoom mode; null until reported. */
    val zoomMax: Float? = null,
    val recording: Boolean = false,
    val recordingSeconds: Int = 0,
    val storageAvailableGB: Float? = null,
    val laserRangeMeters: Float? = null,
    val horizontalFOV: Float? = null,
) {
    companion object {
        const val OPTICAL_ZOOM_MAX = Imago.EO_ZOOM_OPTICAL_MAX
    }
}

data class Detection(
    val id: Int,
    val rect: NormRect,
    val classId: Int,
    val trackId: Int,
    val confidence: Float,
) {
    val label: String get() = DetectionLabels.name(classId)
}

/**
 * The detection a tap at a normalized point means: the smallest box around it, with a little
 * slack for a fingertip and for boxes that trail a moving object. null when none is near.
 */
fun List<Detection>.hit(x: Float, y: Float, slack: Float = 0.02f): Detection? =
    filter { it.rect.contains(x, y, slack) }.minByOrNull { it.rect.w * it.rect.h }

data class TrackingStatus(
    val state: TrackingState = TrackingState.IDLE,
    val box: NormRect? = null,
    val detections: List<Detection> = emptyList(),
)

data class VideoMetrics(
    val active: Boolean = false,
    val codec: String = "—",
    val transport: String = "—",
    val fps: Float = 0f,
    val kbps: Float = 0f,
    val packetsLost: Int = 0,
    val packetsLate: Int = 0,
    val jitterMs: Float = 0f,
    val transferMs: Float = 0f,
    val assemblyMs: Float = 0f,
    val decodeMs: Float = 0f,
    val mavlinkGaps: Int = 0,
    val mavlinkReceived: Int = 0,
    val roundTripLastMs: Float = 0f,
    val lagMs: Float = 0f,
    val resyncs: Int = 0,
    /** Frames that arrived with packets missing (core). */
    val framesCorrupt: Int = 0,
    /** Frames the decoder rejected, and pictures skipped to show the newest. */
    val decodeFailures: Int = 0,
    val framesSkipped: Int = 0,
    /** Time between frames arriving, last 2 s: p95 and max (one period on a smooth link). */
    val frameGapP95Ms: Float = 0f,
    val frameGapMaxMs: Float = 0f,
    /** Frame received -> shown, p95, including the smoothing wait (0 without smoothing). */
    val presentMs: Float = 0f,
)

data class PayloadInfo(
    val model: String = "",
    val softwareVersion: String = "",
    val serialNumber: String = "",
)

/**
 * One state snapshot as the native side serializes it (src/main/cpp/state_writer.cpp). The
 * read order is the contract with writeState: change both together.
 */
class PayloadSnapshot private constructor(b: ByteBuffer) {
    // Link
    val linkUp = b.bool()
    val mavlinkPeer = b.string()
    val cameraComponentId = b.int
    val gimbalComponentId = b.int
    val mavlinkRxMessages = b.int
    val mavlinkTxMessages = b.int
    val mavlinkRxDropped = b.int
    val commandRttMsLast = b.float
    val commandRttMsAvg = b.float
    val commandsPending = b.int

    // Identity
    val modelName = b.string()
    val softwareVersion = b.string()
    val serialNumber = b.string()

    // Gimbal
    val gimbalValid = b.bool()
    val gimbalRollDeg = b.float
    val gimbalPitchDeg = b.float
    val gimbalMode = b.int
    val gimbalControl = b.int
    val gimbalControlSystem = b.int
    val gimbalControlComponent = b.int
    val gimbalYawValid = b.bool()
    val gimbalYawDeg = b.float
    val gimbalYawNorthValid = b.bool()
    val gimbalYawNorthDeg = b.float
    val vehicleHeadingValid = b.bool()
    val vehicleHeadingDeg = b.float

    // Camera
    val viewSource = b.int
    val cameraMode = b.int
    val eoZoomLevel = b.float
    val eoZoomMode = b.int
    val eoZoomMax = b.float
    val irZoomLevel = b.float
    val eoHfovDeg = b.float
    val recording = b.bool()
    val recordingTimeMs = b.int
    val imageCount = b.int
    val storageValid = b.bool()
    val storageTotalMib = b.float
    val storageAvailableMib = b.float
    val lrfValid = b.bool()
    val lrfRangeM = b.float

    // Tracking and detections
    val trackingMode = b.int
    val trackingState = b.int
    val trackingBoxValid = b.bool()
    val trackingBox = NormRect(b.float, b.float, b.float, b.float)
    val detections: List<Detection> = List(b.int) { index ->
        val rect = NormRect(b.float, b.float, b.float, b.float)
        val trackId = b.int
        val classId = b.int
        Detection(index, rect, classId, trackId, b.float)
    }

    // IR temperatures
    val irTempValid = b.bool()
    val irTempMaxC = b.float
    val irTempMinC = b.float
    val irTempMeanC = b.float

    // Video
    val videoActive = b.bool()
    val videoCodec = b.int
    val videoTransport = b.int
    val videoFps = b.float
    val videoBitrateKbps = b.float
    val videoPacketsReceived = b.int
    val videoPacketsLost = b.int
    val videoPacketsLate = b.int
    val videoFramesDelivered = b.int
    val videoFramesCorrupt = b.int
    val videoJitterMs = b.float
    val videoFrameTransferMs = b.float
    val videoFrameGapP95Ms = b.float
    val videoFrameGapMaxMs = b.float
    val latencyAssemblyMs = b.float
    val latencyDecodeMs = b.float
    val latencyPresentMs = b.float
    val videoLagMs = b.float
    val videoResyncs = b.int

    init {
        // The native writer ends with a marker: anything else means the two sides disagree on
        // the layout, and every field read above is wrong.
        check(b.int == END_MARKER && b.remaining() == 0) { "State snapshot layout mismatch with state_writer.cpp" }
    }

    val linkStatus
        get() = LinkStatus(linkUp, mavlinkPeer, commandRttMsAvg.roundToInt(), cameraComponentId,
                           gimbalComponentId)

    val gimbalStatus
        get() = GimbalStatus(
            valid = gimbalValid,
            pitch = tenths(gimbalPitchDeg),
            yaw = if (gimbalValid && gimbalYawValid) tenths(gimbalYawDeg) else null,
            yawNorth = if (gimbalValid && gimbalYawNorthValid) tenths(gimbalYawNorthDeg) else null,
            mode = GimbalMode.of(gimbalMode),
            control = when (gimbalControl) {
                Imago.GIMBAL_CONTROL_THIS_APP -> GimbalControl.ThisApp
                Imago.GIMBAL_CONTROL_NOBODY -> GimbalControl.Nobody
                Imago.GIMBAL_CONTROL_OTHER -> GimbalControl.Other(gimbalControlSystem, gimbalControlComponent)
                else -> GimbalControl.Unknown
            },
        )

    /** Aircraft heading from North, whole degrees (it changes constantly in flight). */
    val vehicleHeading: Float?
        get() = if (vehicleHeadingValid) Math.round(vehicleHeadingDeg).toFloat() else null

    /** Standing video lag in 50 ms steps, so the warning only recomposes on real changes. */
    val videoLagSteps: Int
        get() = if (videoActive) (videoLagMs / 50).roundToInt() * 50 else 0

    val cameraStatus
        get() = CameraStatus(
            view = ViewSource.of(viewSource),
            mode = CameraMode.of(cameraMode),
            zoom = Math.round(eoZoomLevel * 10) / 10f,
            zoomMax = if (eoZoomMax > 0) eoZoomMax else null,
            recording = recording,
            recordingSeconds = recordingTimeMs / 1000,
            storageAvailableGB = if (storageValid) storageAvailableMib / 1024 else null,
            laserRangeMeters = if (lrfValid) lrfRangeM else null,
            horizontalFOV = if (eoHfovDeg > 0) Math.round(eoHfovDeg).toFloat() else null,
        )

    val trackingStatus
        get() = TrackingStatus(
            state = TrackingState.of(trackingState),
            box = if (trackingBoxValid) trackingBox else null,
            detections = detections,
        )

    val videoMetrics
        get() = VideoMetrics(
            active = videoActive,
            codec = when (videoCodec) {
                Imago.CODEC_H265 -> "H.265"
                Imago.CODEC_H264 -> "H.264"
                else -> "—"
            },
            transport = if (videoTransport == Imago.RTSP_TCP) "TCP" else if (videoActive) "UDP" else "—",
            fps = videoFps,
            kbps = videoBitrateKbps,
            packetsLost = videoPacketsLost,
            packetsLate = videoPacketsLate,
            jitterMs = videoJitterMs,
            transferMs = videoFrameTransferMs,
            assemblyMs = latencyAssemblyMs,
            decodeMs = latencyDecodeMs,
            mavlinkGaps = mavlinkRxDropped,
            mavlinkReceived = mavlinkRxMessages,
            roundTripLastMs = commandRttMsLast,
            lagMs = videoLagMs,
            resyncs = videoResyncs,
            framesCorrupt = videoFramesCorrupt,
            frameGapP95Ms = videoFrameGapP95Ms,
            frameGapMaxMs = videoFrameGapMaxMs,
            presentMs = latencyPresentMs,
        )

    val payloadInfo get() = PayloadInfo(modelName, softwareVersion, serialNumber)

    companion object {
        /** Same as kStateEndMarker in state_writer.h. */
        private const val END_MARKER = 0x494D4147

        /** Reads a snapshot of `size` bytes from the start of `buffer`. */
        fun read(buffer: ByteBuffer, size: Int): PayloadSnapshot {
            val view = buffer.duplicate().order(ByteOrder.LITTLE_ENDIAN)
            view.position(0)
            view.limit(size)
            return PayloadSnapshot(view)
        }

        private fun ByteBuffer.bool() = int != 0

        private fun ByteBuffer.string(): String {
            val length = int
            val bytes = ByteArray(length)
            get(bytes)
            return String(bytes, Charsets.UTF_8)
        }

        private fun tenths(value: Float) = Math.round(value * 10) / 10f
    }
}
