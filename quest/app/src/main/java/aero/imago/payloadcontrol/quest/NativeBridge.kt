package aero.imago.payloadcontrol.quest

import android.app.Activity
import android.graphics.SurfaceTexture
import android.view.Surface

/**
 * The native app (src/main/cpp): the OpenXR loop, the core session and the video decoder all
 * live there. Kotlin only hands over the profiles and the surfaces, and paints the HUD
 * overlay from the snapshot the native side fills.
 */
internal object NativeBridge {
    init {
        System.loadLibrary("imago_quest")
    }

    /**
     * Starts the OpenXR thread. The activity receives onVideoSize(width, height) and
     * onNotice(bytes). The video surface takes the decoder's pictures; both surface textures
     * are attached to the native GL context (they must be created detached) and sampled on
     * the virtual screen.
     */
    @JvmStatic external fun start(
        activity: Activity,
        profiles: Array<ProfileArgs>,
        videoSurface: Surface,
        videoTexture: SurfaceTexture,
        overlayTexture: SurfaceTexture,
    ): Boolean

    /** Stops the thread and the session; returns once nothing native runs any more. */
    @JvmStatic external fun stop()

    /** Fills `out` (Hud.SIZE floats) with the snapshot the HUD paints. Returns false if not running. */
    @JvmStatic external fun hudSnapshot(out: FloatArray): Boolean

    /** The picture's size, from the decoder, so the overlay and the panel keep its aspect. */
    @JvmStatic external fun setVideoSize(width: Int, height: Int)
}

/** A profile flattened for JNI (one object, plain fields). */
internal class ProfileArgs(profile: ConnectionProfile) {
    @JvmField val name: String = profile.name
    @JvmField val listen: Boolean = profile.isListen
    @JvmField val payloadIp: String = profile.payloadIP
    @JvmField val mavlinkPort: Int = profile.mavlinkPort
    @JvmField val localPort: Int = profile.localPort
    @JvmField val videoUrl: String = profile.videoURL
    @JvmField val transport: Int = profile.transportRaw
    @JvmField val gimbalProtocol: Int = profile.gimbalProtocolRaw
    @JvmField val maxVideoLagMs: Int = profile.maxVideoLagMs
    @JvmField val videoSmoothingMs: Int = profile.videoSmoothingMs
    @JvmField val headMode: Int = profile.headTracking.modeRaw
    @JvmField val headOnAtStart: Boolean = profile.headTracking.onAtStart
    @JvmField val headGain: Float = profile.headTracking.gainDpsPerDeg
    @JvmField val headMaxRate: Float = profile.headTracking.maxRateDps
    @JvmField val headDeadband: Float = profile.headTracking.deadbandDeg
    @JvmField val headSmoothingMs: Int = profile.headTracking.smoothingMs
    @JvmField val headYawRange: Float = profile.headTracking.yawRangeDeg
    @JvmField val maxYawDps: Float = profile.controls.maxYawDps
    @JvmField val maxPitchDps: Float = profile.controls.maxPitchDps
    @JvmField val deadzone: Float = profile.controls.deadzone
    @JvmField val expo: Float = profile.controls.expo
    @JvmField val zoomCompensation: Float = profile.controls.zoomCompensation
    @JvmField val trimFactor: Float = profile.controls.trimFactor
    @JvmField val panelDistance: Float = profile.panel.distanceM
    @JvmField val panelWidth: Float = profile.panel.widthM
    @JvmField val panelPitchDown: Float = profile.panel.pitchDownDeg
}

/**
 * Layout of the HUD snapshot (floats), the contract with hud_layout.h on the native side:
 * change both together. Booleans are 0/1.
 */
internal object Hud {
    const val LINK_UP = 0
    const val CONNECTED = 1
    const val PROFILE_INDEX = 2
    const val VIDEO_ACTIVE = 3
    const val FPS = 4
    const val BITRATE_KBPS = 5
    const val LAG_MS = 6
    const val LAG_STALE = 7
    const val GIMBAL_VALID = 8
    const val PITCH = 9
    const val YAW = 10
    const val PAN_NEAR_LIMIT = 11
    const val GIMBAL_MODE = 12
    const val ZOOM = 13
    const val VIEW_SOURCE = 14
    const val RECORDING = 15
    const val RECORDING_TIME_MS = 16
    const val TRACKING_MODE = 17
    const val TRACKING_STATE = 18
    const val BOX_VALID = 19
    const val BOX_X = 20
    const val BOX_Y = 21
    const val BOX_W = 22
    const val BOX_H = 23
    const val HEAD_MODE = 24
    const val HEAD_FOLLOWING = 25
    const val HEAD_HELD = 26
    const val HEAD_POSE_FRESH = 27
    const val HEAD_ERROR = 28
    const val POINTER_VALID = 29
    const val POINTER_X = 30
    const val POINTER_Y = 31
    const val LATENCY_PRESENT_MS = 32
    const val LRF_VALID = 33
    const val LRF_RANGE_M = 34
    const val GIMBAL_CONTROL = 35
    const val DETECTION_COUNT = 36
    const val DETECTIONS = 37
    const val DETECTION_STRIDE = 6 // x, y, w, h, class, confidence
    const val MAX_DETECTIONS = 20
    const val SIZE = DETECTIONS + MAX_DETECTIONS * DETECTION_STRIDE
}
