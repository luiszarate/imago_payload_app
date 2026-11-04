package aero.imago.payloadcontrol.quest

import android.app.Activity
import android.graphics.SurfaceTexture
import android.os.Bundle
import android.os.Handler
import android.os.HandlerThread
import android.util.Log
import android.view.Surface
import android.view.WindowManager

/**
 * The only activity. The OpenXR session (native) draws to the headset; this activity exists
 * for the lifecycle, the profiles file and the HUD overlay, which is painted here with a
 * Canvas onto a Surface the native side samples as a texture. Nothing on the video path
 * touches Kotlin.
 */
class QuestActivity : Activity() {
    private lateinit var videoTexture: SurfaceTexture
    private lateinit var videoSurface: Surface
    private lateinit var overlayTexture: SurfaceTexture
    private lateinit var overlaySurface: Surface
    private lateinit var hudThread: HandlerThread
    private lateinit var hudHandler: Handler
    private lateinit var painter: HudPainter
    private var profiles: List<ConnectionProfile> = emptyList()
    private var running = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        val dir = getExternalFilesDir(null) ?: filesDir
        profiles = Profiles.load(dir)
        Log.i(TAG, "${profiles.size} profiles from ${dir.absolutePath}/${Profiles.FILE_NAME}")

        // Detached surface textures: the native GL thread attaches them to its own context.
        videoTexture = SurfaceTexture(false)
        videoTexture.setDefaultBufferSize(1920, 1080)
        videoSurface = Surface(videoTexture)
        overlayTexture = SurfaceTexture(false)
        overlayTexture.setDefaultBufferSize(HudPainter.WIDTH, HudPainter.HEIGHT)
        overlaySurface = Surface(overlayTexture)

        painter = HudPainter(overlaySurface, profiles.map { it.name })
        hudThread = HandlerThread("imago-hud").apply { start() }
        hudHandler = Handler(hudThread.looper)

        running = NativeBridge.start(this, profiles.map { ProfileArgs(it) }.toTypedArray(),
            videoSurface, videoTexture, overlayTexture)
        if (!running) {
            Log.e(TAG, "native start failed")
            finish()
            return
        }
        hudHandler.post(paintTick)
    }

    private val paintTick = object : Runnable {
        override fun run() {
            if (!running) return
            painter.paint()
            hudHandler.postDelayed(this, HudPainter.PERIOD_MS)
        }
    }

    override fun onDestroy() {
        if (running) {
            running = false
            hudHandler.removeCallbacksAndMessages(null)
            NativeBridge.stop()
        }
        hudThread.quitSafely()
        overlaySurface.release()
        overlayTexture.release()
        videoSurface.release()
        videoTexture.release()
        super.onDestroy()
    }

    /** From the native decoder (any thread): the picture's size. */
    @Suppress("unused")
    fun onVideoSize(width: Int, height: Int) {
        if (width <= 0 || height <= 0) return
        videoTexture.setDefaultBufferSize(width, height)
        painter.videoAspect = width.toFloat() / height
        NativeBridge.setVideoSize(width, height)
    }

    /** From the native side (any thread): a line for the HUD's notice area. */
    @Suppress("unused")
    fun onNotice(text: ByteArray) {
        painter.notice(String(text, Charsets.UTF_8))
    }

    private companion object {
        const val TAG = "imago-quest"
    }
}
