package aero.imago.payloadcontrol.core

import android.view.Surface
import java.nio.ByteBuffer

/**
 * The core's C API (core/include/imago/imago_core.h) through JNI (src/main/cpp/jni_core.cpp).
 * Handles are native pointers. Command functions return the core's request ID (0 = not sent).
 */
internal object NativeCore {
    init {
        System.loadLibrary("imago_jni")
    }

    // Video decoder and renderer (lives as long as the app)
    @JvmStatic external fun decoderCreate(listener: VideoRenderer): Long
    @JvmStatic external fun decoderDestroy(handle: Long)
    @JvmStatic external fun decoderSetSurface(handle: Long, surface: Surface?)
    @JvmStatic external fun decoderReset(handle: Long)
    /** decode failures, skipped, rendered */
    @JvmStatic external fun decoderCounters(handle: Long, out: IntArray)

    // Session
    @JvmStatic external fun sessionCreate(
        payloadIp: String?, payloadPort: Int, localPort: Int, rtspUrl: String?, transport: Int,
        gimbalProtocol: Int, keepWifiAwake: Boolean, maxLagMs: Int, smoothingMs: Int, decoder: Long,
        listener: PayloadSession.Listener,
    ): Long
    @JvmStatic external fun sessionStart(handle: Long): Int
    @JvmStatic external fun sessionStop(handle: Long)
    @JvmStatic external fun sessionDestroy(handle: Long)
    @JvmStatic external fun sessionState(handle: Long, buffer: ByteBuffer): Int
    @JvmStatic external fun stateBufferSize(): Int
    @JvmStatic external fun startRecording(handle: Long, path: String): Int
    @JvmStatic external fun stopRecording(handle: Long): Int
    @JvmStatic external fun nowNs(): Long

    // Gimbal
    @JvmStatic external fun gimbalSetRate(handle: Long, pitch: Float, yaw: Float)
    @JvmStatic external fun gimbalSetAngle(handle: Long, pitch: Float, yaw: Float): Int
    @JvmStatic external fun gimbalSetMode(handle: Long, mode: Int): Int
    @JvmStatic external fun gimbalTakeControl(handle: Long, take: Boolean): Int

    // Camera
    @JvmStatic external fun cameraSetMode(handle: Long, mode: Int): Int
    @JvmStatic external fun cameraTakePhoto(handle: Long): Int
    @JvmStatic external fun cameraSetRecording(handle: Long, on: Boolean): Int
    @JvmStatic external fun cameraZoom(handle: Long, direction: Int): Int
    @JvmStatic external fun cameraZoomTarget(handle: Long, level: Float)
    @JvmStatic external fun cameraSetView(handle: Long, source: Int): Int
    @JvmStatic external fun cameraSetParam(handle: Long, id: String, value: Int): Int
    @JvmStatic external fun cameraReadParam(handle: Long, id: String): Int
    @JvmStatic external fun zoomLevels(mode: Int): FloatArray
    @JvmStatic external fun streamSetBitrate(handle: Long, camera: Int, bitsPerSecond: Int): Int

    // Tracking
    @JvmStatic external fun trackerSetFunction(handle: Long, function: Int): Int
    @JvmStatic external fun trackingStartAt(handle: Long, x: Float, y: Float, w: Float, h: Float): Int
    @JvmStatic external fun trackingPointAt(handle: Long, x: Float, y: Float): Int
    @JvmStatic external fun trackingStop(handle: Long): Int
}

/** Constants of imago_core.h. */
internal object Imago {
    const val RTSP_AUTO = 0
    const val RTSP_UDP = 1
    const val RTSP_TCP = 2

    const val GIMBAL_PROTOCOL_V2 = 0
    const val GIMBAL_PROTOCOL_V1 = 1
    const val GIMBAL_PROTOCOL_MANAGER = 2

    const val GIMBAL_CONTROL_THIS_APP = 1
    const val GIMBAL_CONTROL_OTHER = 2
    const val GIMBAL_CONTROL_NOBODY = 3

    const val CODEC_H264 = 1
    const val CODEC_H265 = 2

    const val ZOOM_MODE_COMBINED = 0
    const val ZOOM_MODE_SUPER_RESOLUTION = 2
    const val EO_ZOOM_OPTICAL_MAX = 25f

    const val TRACKER_OBJECT_TRACKING = 0
    const val TRACKER_OBJECT_DETECTION = 1

    const val EVENT_LINK_UP = 1
    const val EVENT_LINK_DOWN = 2
    const val EVENT_VIDEO_STARTED = 3
    const val EVENT_VIDEO_STOPPED = 4
    const val EVENT_VIDEO_ERROR = 5
    const val EVENT_COMMAND_RESULT = 6
    const val EVENT_PARAM_RESULT = 7
    const val EVENT_STREAM_URI = 8
    const val EVENT_STATUS_TEXT = 9
    const val EVENT_VIDEO_RESYNC = 10
    const val EVENT_CAMERA_PARAM = 11
    const val EVENT_FOREIGN_COMMAND = 12
    const val EVENT_ZOOM_CHANGED_EXTERNALLY = 13
}
