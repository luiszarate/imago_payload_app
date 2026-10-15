package aero.imago.payloadcontrol.core

import android.os.Handler
import android.os.Looper
import android.view.Surface
import androidx.annotation.Keep

/**
 * Hardware decoding and display of the payload's video (src/main/cpp/video_decoder.cpp).
 * One for the whole app: the SurfaceView hands it its surface, each session feeds it frames.
 */
class VideoRenderer {
    /** Pictures lost after the network: frames the decoder rejected, pictures skipped. */
    data class Counters(val decodeFailures: Int = 0, val skipped: Int = 0, val rendered: Int = 0)

    /** Main thread: the picture's size in pixels, for overlays and tap mapping. */
    var onVideoSizeChange: ((width: Int, height: Int) -> Unit)? = null

    internal val handle: Long = NativeCore.decoderCreate(this)
    private val main = Handler(Looper.getMainLooper())
    private val countersOut = IntArray(3)

    /** null when the surface goes away; the decoder stops until the next one. */
    fun setSurface(surface: Surface?) = NativeCore.decoderSetSurface(handle, surface)

    /** Drops the decoder. Call only while no session is running. */
    fun reset() = NativeCore.decoderReset(handle)

    fun counters(): Counters {
        NativeCore.decoderCounters(handle, countersOut)
        return Counters(countersOut[0], countersOut[1], countersOut[2])
    }

    /** Decoder output thread. */
    @Keep
    @Suppress("unused")
    private fun onVideoSize(width: Int, height: Int) {
        main.post { onVideoSizeChange?.invoke(width, height) }
    }
}
