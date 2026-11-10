package aero.imago.payloadcontrol.quest

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.graphics.SurfaceTexture
import android.net.wifi.WifiManager
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
    private var wifiLock: WifiManager.WifiLock? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        // Low-latency Wi-Fi while the app runs: the radio stays out of power save between
        // video packets, as the Android app's NetworkBinder does.
        wifiLock = (applicationContext.getSystemService(Context.WIFI_SERVICE) as? WifiManager)
            ?.createWifiLock(WifiManager.WIFI_MODE_FULL_LOW_LATENCY, "imago-quest")
            ?.also { it.setReferenceCounted(false); it.acquire() }

        val dir = getExternalFilesDir(null) ?: filesDir
        profiles = Profiles.load(dir)
        Log.i(TAG, "${profiles.size} profiles from ${dir.absolutePath}/${Profiles.FILE_NAME}")

        // Detached surface textures: the native GL thread attaches them to its own context.
        videoTexture = SurfaceTexture(false)
        videoTexture.setDefaultBufferSize(1920, 1080)
        videoSurface = Surface(videoTexture)
        overlayTexture = SurfaceTexture(false)
        sizeOverlay(16f / 9f)
        overlaySurface = Surface(overlayTexture)

        painter = HudPainter(this, overlaySurface, profiles.map { it.name }, onExit = { runOnUiThread { exitToPanel() } })
        hudThread = HandlerThread("imago-hud").apply { start() }
        hudHandler = Handler(hudThread.looper)

        // The profile chosen on the panel; launched directly (adb), the remembered one.
        val initial = intent.getIntExtra(EXTRA_PROFILE, Selection.load(this)).coerceIn(0, profiles.size - 1)
        running = NativeBridge.start(this, profiles.map { ProfileArgs(it) }.toTypedArray(), initial,
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
        wifiLock?.let { if (it.isHeld) it.release() }
        overlaySurface.release()
        overlayTexture.release()
        videoSurface.release()
        videoTexture.release()
        super.onDestroy()
    }

    private fun sizeOverlay(aspect: Float) {
        val scale = HudPainter.BUFFER_SCALE
        overlayTexture.setDefaultBufferSize((HudPainter.WIDTH * scale).toInt(), (HudPainter.canvasHeight(aspect) * scale).toInt())
    }

    /** EXIT in the top bar: back to the 2D panel; this activity and its session end. */
    private fun exitToPanel() {
        startActivity(Intent(this, PanelActivity::class.java)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_REORDER_TO_FRONT))
        finishAndRemoveTask()
    }

    /** From the native decoder (any thread): the picture's size. */
    @Suppress("unused")
    fun onVideoSize(width: Int, height: Int) {
        if (width <= 0 || height <= 0) return
        videoTexture.setDefaultBufferSize(width, height)
        val aspect = width.toFloat() / height
        painter.videoAspect = aspect
        // The interface keeps the picture's shape plus its bars.
        sizeOverlay(aspect)
        NativeBridge.setVideoSize(width, height)
    }

    /** From the native side (any thread): a line for the HUD's notice area. */
    @Suppress("unused")
    fun onNotice(text: ByteArray) {
        painter.notice(String(text, Charsets.UTF_8))
    }

    companion object {
        private const val TAG = "imago-quest"
        /** Index into the profiles of profiles.json to connect to. */
        const val EXTRA_PROFILE = "profile"
    }
}
