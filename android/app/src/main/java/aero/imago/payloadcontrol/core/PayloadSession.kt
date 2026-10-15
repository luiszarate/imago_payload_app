package aero.imago.payloadcontrol.core

import android.os.Handler
import android.os.Looper
import androidx.annotation.Keep
import java.nio.ByteBuffer

/** Something the payload or the video link reported. Delivered on the main thread. */
sealed interface PayloadEvent {
    data object LinkUp : PayloadEvent
    data object LinkDown : PayloadEvent
    data class VideoStarted(val url: String, val tcp: Boolean) : PayloadEvent
    data object VideoStopped : PayloadEvent
    data class VideoError(val message: String) : PayloadEvent
    /** The video fell behind live by this much and the core restarted the stream. */
    data class VideoResync(val lagMs: Int) : PayloadEvent
    data class CommandResult(val requestId: Int, val command: Int, val result: Int) : PayloadEvent
    data class ParamResult(val requestId: Int, val paramId: String, val result: Int) : PayloadEvent
    data class StreamUri(val url: String) : PayloadEvent
    data class StatusText(val text: String) : PayloadEvent
    /** A camera setting's value: read on request, or just accepted. */
    data class CameraParam(val id: String, val value: Int) : PayloadEvent
    /** The payload acknowledged a command someone else sent. from: "system/component". */
    data class ForeignCommand(val command: Int, val result: Int, val from: String) : PayloadEvent
    /** The zoom moved without this app asking, e.g. "12.0x -> 1.0x". */
    data class ZoomChangedExternally(val change: String) : PayloadEvent

    companion object {
        fun of(type: Int, requestId: Int, code: Int, result: Int, text: String): PayloadEvent =
            when (type) {
                Imago.EVENT_LINK_UP -> LinkUp
                Imago.EVENT_LINK_DOWN -> LinkDown
                Imago.EVENT_VIDEO_STARTED -> VideoStarted(text, code == Imago.RTSP_TCP)
                Imago.EVENT_VIDEO_STOPPED -> VideoStopped
                Imago.EVENT_VIDEO_ERROR -> VideoError(text)
                Imago.EVENT_VIDEO_RESYNC -> VideoResync(code)
                Imago.EVENT_COMMAND_RESULT -> CommandResult(requestId, code, result)
                Imago.EVENT_PARAM_RESULT -> ParamResult(requestId, text, result)
                Imago.EVENT_STREAM_URI -> StreamUri(text)
                Imago.EVENT_CAMERA_PARAM -> CameraParam(text, code)
                Imago.EVENT_FOREIGN_COMMAND -> ForeignCommand(code, result, text)
                Imago.EVENT_ZOOM_CHANGED_EXTERNALLY -> ZoomChangedExternally(text)
                else -> StatusText(text)
            }
    }
}

/**
 * One connection to a payload, wrapping an imago_session.
 *
 * Every command may be called from any thread. The core encodes and sends it on the calling
 * thread immediately, so the joystick and the gamepad never wait for anything.
 */
class PayloadSession private constructor(
    val profile: ConnectionProfile,
    private val handle: Long,
) {
    /** Called by the core's threads (JNI); forwards events to the main thread. */
    class Listener(private val onEvent: (PayloadEvent) -> Unit) {
        private val main = Handler(Looper.getMainLooper())

        @Keep
        @Suppress("unused")
        fun onEvent(type: Int, requestId: Int, code: Int, result: Int, text: ByteArray?) {
            val event = PayloadEvent.of(type, requestId, code, result,
                                        text?.toString(Charsets.UTF_8) ?: "")
            main.post { onEvent(event) }
        }
    }

    private val stateBuffer = ByteBuffer.allocateDirect(NativeCore.stateBufferSize())
    @Volatile private var closed = false

    /** Opens the sockets and starts the core. false if the local port is taken. */
    fun start(): Boolean = NativeCore.sessionStart(handle) == 0

    /** Stops the core threads and frees the session. No callback runs after this. */
    fun close() {
        if (closed) return
        closed = true
        NativeCore.sessionDestroy(handle)
    }

    fun state(): PayloadSnapshot? {
        if (closed) return null
        val size = NativeCore.sessionState(handle, stateBuffer)
        return if (size > 0) PayloadSnapshot.read(stateBuffer, size) else null
    }

    // Commands. After close() they do nothing and return 0.
    private inline fun cmd(block: (Long) -> Int): Int = if (closed) 0 else block(handle)

    // MARK: Gimbal

    fun setGimbalRate(pitch: Float, yaw: Float) {
        if (!closed) NativeCore.gimbalSetRate(handle, pitch, yaw)
    }

    fun setGimbalMode(mode: GimbalMode) = cmd { NativeCore.gimbalSetMode(it, mode.raw) }
    fun setGimbalAngle(pitch: Float, yaw: Float) = cmd { NativeCore.gimbalSetAngle(it, pitch, yaw) }
    /** Takes or gives back primary control of the autopilot's gimbal manager. */
    fun takeGimbalControl(take: Boolean) = cmd { NativeCore.gimbalTakeControl(it, take) }

    // MARK: Camera

    fun setCameraMode(mode: CameraMode) = cmd { NativeCore.cameraSetMode(it, mode.raw) }
    fun takePhoto() = cmd { NativeCore.cameraTakePhoto(it) }
    fun setRecording(on: Boolean) = cmd { NativeCore.cameraSetRecording(it, on) }
    /** -1 zoom out, 0 stop, 1 zoom in. */
    fun zoom(direction: Int) = cmd { NativeCore.cameraZoom(it, direction) }

    /** Exact level from a dragged control: paced and not tracked (imago_camera_zoom_target). */
    fun zoomTarget(level: Float) {
        if (!closed) NativeCore.cameraZoomTarget(handle, level)
    }

    fun setView(source: ViewSource) = cmd { NativeCore.cameraSetView(it, source.raw) }
    fun setCameraParam(id: String, value: Int) = cmd { NativeCore.cameraSetParam(it, id, value) }

    /** Asks for a setting's current value; it arrives as CameraParam. Paced by the core. */
    fun readCameraParam(id: String) {
        if (!closed) NativeCore.cameraReadParam(handle, id)
    }

    /** Bitrate of a camera's stream, bit/s. (Unverified on the Orus L: which camera is EO.) */
    fun setStreamBitrate(cameraId: Int, bitsPerSecond: Int) =
        cmd { NativeCore.streamSetBitrate(it, cameraId, bitsPerSecond) }

    /** Records all MAVLink in and out to a .tlog until stopRecording() or close(). */
    fun startRecording(path: String): Boolean = !closed && NativeCore.startRecording(handle, path) == 0

    /** false if part of the log could not be written (full storage). */
    fun stopRecording(): Boolean = closed || NativeCore.stopRecording(handle) == 0

    // MARK: Tracking

    /** Normalized point, (0, 0) top-left. */
    fun track(x: Float, y: Float) = cmd { NativeCore.trackingStartAt(it, x, y, 0f, 0f) }

    /** Locks onto an object with its own box (normalized), e.g. an AI detection. */
    fun track(box: NormRect) = cmd { NativeCore.trackingStartAt(it, box.midX, box.midY, box.w, box.h) }

    fun pointGimbal(x: Float, y: Float) = cmd { NativeCore.trackingPointAt(it, x, y) }
    fun stopTracking() = cmd { NativeCore.trackingStop(it) }
    fun setDetection(enabled: Boolean) = cmd {
        NativeCore.trackerSetFunction(it, if (enabled) Imago.TRACKER_OBJECT_DETECTION
                                          else Imago.TRACKER_OBJECT_TRACKING)
    }

    companion object {
        /** null if the profile cannot make a session (invalid address). */
        fun create(profile: ConnectionProfile, video: VideoRenderer,
                   onEvent: (PayloadEvent) -> Unit): PayloadSession? {
            val listener = Listener(onEvent)
            val ip = if (profile.mavlinkMode == ConnectionProfile.MavlinkMode.DIRECT) profile.payloadIP.trim() else ""
            val url = profile.videoURL.trim()
            val handle = NativeCore.sessionCreate(
                payloadIp = ip.ifEmpty { null },
                payloadPort = profile.mavlinkPort,
                localPort = profile.localPort,
                rtspUrl = url.ifEmpty { null },
                transport = profile.videoTransport.raw,
                gimbalProtocol = profile.gimbalProtocol.raw,
                keepWifiAwake = profile.keepWifiAwake,
                maxLagMs = profile.maxVideoLagMs,
                smoothingMs = profile.videoSmoothingMs.coerceIn(0, 500),
                decoder = video.handle,
                listener = listener,
            )
            return if (handle != 0L) PayloadSession(profile, handle) else null
        }

        /** The levels the camera steps through in a zoom mode (C_V_ZM_CB_LV / C_V_ZM_SR_LV). */
        fun zoomLevels(superResolution: Boolean): List<Float> = NativeCore.zoomLevels(
            if (superResolution) Imago.ZOOM_MODE_SUPER_RESOLUTION else Imago.ZOOM_MODE_COMBINED
        ).toList()

        fun nowNs(): Long = NativeCore.nowNs()
    }
}
