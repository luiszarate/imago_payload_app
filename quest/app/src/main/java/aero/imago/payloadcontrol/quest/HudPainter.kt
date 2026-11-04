package aero.imago.payloadcontrol.quest

import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.PorterDuff
import android.graphics.RectF
import android.graphics.Typeface
import android.os.SystemClock
import android.view.Surface
import java.util.Locale

/**
 * Paints the overlay of the virtual screen: status lines, tracking box, AI detections, the
 * pointer and the controls legend. Transparent where there is nothing, so the video shows
 * through. Runs on its own thread at a few frames per second; the native side samples the
 * latest picture every headset frame.
 */
internal class HudPainter(private val surface: Surface, private val profileNames: List<String>) {
    @Volatile var videoAspect: Float = 16f / 9f

    private val snapshot = FloatArray(Hud.SIZE)
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textSize = 30f
        typeface = Typeface.MONOSPACE
        setShadowLayer(3f, 0f, 0f, Color.BLACK)
    }
    private val small = Paint(text).apply { textSize = 24f; color = Color.argb(220, 255, 255, 255) }
    private val accent = Paint(text).apply { color = Color.rgb(255, 196, 0) }
    private val warn = Paint(text).apply { color = Color.rgb(255, 90, 60) }
    private val box = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 4f
        color = Color.MAGENTA
    }
    private val detection = Paint(box).apply { strokeWidth = 3f; color = Color.rgb(80, 220, 120) }
    private val pointer = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 3f
        color = Color.rgb(255, 196, 0)
    }
    private val dim = Paint().apply { color = Color.argb(110, 0, 0, 0) }

    private var noticeText = ""
    private var noticeUntilMs = 0L

    fun notice(line: String) {
        synchronized(this) {
            noticeText = line
            noticeUntilMs = SystemClock.elapsedRealtime() + NOTICE_MS
        }
    }

    fun paint() {
        if (!NativeBridge.hudSnapshot(snapshot)) return
        val canvas = try {
            surface.lockCanvas(null)
        } catch (e: Exception) {
            return
        }
        try {
            canvas.drawColor(Color.TRANSPARENT, PorterDuff.Mode.CLEAR)
            draw(canvas, snapshot)
        } finally {
            surface.unlockCanvasAndPost(canvas)
        }
    }

    private fun draw(c: Canvas, s: FloatArray) {
        val w = c.width.toFloat()
        val h = c.height.toFloat()
        val on = { i: Int -> s[i] >= 0.5f }

        // Picture rectangles: the overlay covers the panel, which keeps the video's aspect.
        if (on(Hud.BOX_VALID)) {
            c.drawRect(RectF(s[Hud.BOX_X] * w, s[Hud.BOX_Y] * h, (s[Hud.BOX_X] + s[Hud.BOX_W]) * w,
                (s[Hud.BOX_Y] + s[Hud.BOX_H]) * h), box)
        }
        val detections = s[Hud.DETECTION_COUNT].toInt().coerceIn(0, Hud.MAX_DETECTIONS)
        for (k in 0 until detections) {
            val base = Hud.DETECTIONS + k * Hud.DETECTION_STRIDE
            val left = s[base] * w
            val top = s[base + 1] * h
            c.drawRect(RectF(left, top, left + s[base + 2] * w, top + s[base + 3] * h), detection)
            val label = "${DetectionLabels.name(s[base + 4].toInt())} ${(s[base + 5] * 100).toInt()}%"
            c.drawText(label, left + 4f, top - 6f, small)
        }
        if (on(Hud.POINTER_VALID)) {
            val x = s[Hud.POINTER_X] * w
            val y = s[Hud.POINTER_Y] * h
            c.drawCircle(x, y, 14f, pointer)
            c.drawLine(x - 24f, y, x - 16f, y, pointer)
            c.drawLine(x + 16f, y, x + 24f, y, pointer)
            c.drawLine(x, y - 24f, x, y - 16f, pointer)
            c.drawLine(x, y + 16f, x, y + 24f, pointer)
        }

        // Top band: link, video, gimbal, camera.
        c.drawRect(0f, 0f, w, 50f, dim)
        val connected = on(Hud.CONNECTED)
        val profile = profileNames.getOrNull(s[Hud.PROFILE_INDEX].toInt()) ?: "—"
        var x = 16f
        x = line(c, x, 36f, profile, text)
        x = line(c, x, 36f, if (on(Hud.LINK_UP)) "LINK" else if (connected) "NO LINK" else "OFF",
            if (on(Hud.LINK_UP)) accent else warn)
        if (on(Hud.VIDEO_ACTIVE)) {
            x = line(c, x, 36f, String.format(Locale.US, "%.0f fps %.1f Mb/s", s[Hud.FPS], s[Hud.BITRATE_KBPS] / 1000f), text)
            if (on(Hud.LAG_STALE)) x = line(c, x, 36f, String.format(Locale.US, "STALE +%.0f ms", s[Hud.LAG_MS]), warn)
        } else if (connected) {
            x = line(c, x, 36f, "NO VIDEO", warn)
        }
        if (on(Hud.GIMBAL_VALID)) {
            x = line(c, x, 36f, String.format(Locale.US, "P %+.1f°  Y %+.1f°", s[Hud.PITCH], s[Hud.YAW]),
                if (on(Hud.PAN_NEAR_LIMIT)) warn else text)
        }
        if (s[Hud.ZOOM] > 0f) x = line(c, x, 36f, String.format(Locale.US, "%.1fx", s[Hud.ZOOM]), text)
        x = line(c, x, 36f, viewName(s[Hud.VIEW_SOURCE].toInt()), text)
        if (on(Hud.RECORDING)) {
            val secs = (s[Hud.RECORDING_TIME_MS] / 1000f).toInt()
            x = line(c, x, 36f, String.format(Locale.US, "REC %02d:%02d", secs / 60, secs % 60), warn)
        }
        if (on(Hud.LRF_VALID)) line(c, x, 36f, String.format(Locale.US, "LRF %.0f m", s[Hud.LRF_RANGE_M]), text)

        // Second band: head tracking and the tracker.
        c.drawRect(0f, 50f, w, 96f, dim)
        x = 16f
        val head = when {
            s[Hud.HEAD_MODE] < 0.5f -> "HEAD OFF"
            on(Hud.HEAD_HELD) -> "HEAD ON · held by tracker"
            !on(Hud.HEAD_POSE_FRESH) -> "HEAD ON · no pose"
            on(Hud.HEAD_FOLLOWING) -> String.format(Locale.US, "HEAD ON · following (%.1f°)", s[Hud.HEAD_ERROR])
            else -> "HEAD ON · waiting for the gimbal"
        }
        x = line(c, x, 82f, head, if (s[Hud.HEAD_MODE] >= 0.5f) accent else text)
        val tracking = when (s[Hud.TRACKING_STATE].toInt()) {
            1 -> "TRACKING"
            2 -> "TRACK LOST"
            else -> if (s[Hud.TRACKING_MODE].toInt() == 1) "TRACKER ARMED" else "TRACKER IDLE"
        }
        x = line(c, x, 82f, tracking, if (s[Hud.TRACKING_STATE].toInt() == 1) accent else text)
        if (detections > 0) x = line(c, x, 82f, "AI $detections", text)
        if (s[Hud.GIMBAL_CONTROL].toInt() == 2) x = line(c, x, 82f, "GIMBAL: OTHER IN CONTROL", warn)
        if (s[Hud.LATENCY_PRESENT_MS] > 0f) line(c, x, 82f, String.format(Locale.US, "app %.0f ms", s[Hud.LATENCY_PRESENT_MS]), small)

        // Notice and legend at the bottom.
        val notice = synchronized(this) { if (SystemClock.elapsedRealtime() < noticeUntilMs) noticeText else "" }
        if (notice.isNotEmpty()) {
            c.drawRect(0f, h - 96f, w, h - 50f, dim)
            c.drawText(notice, 16f, h - 62f, accent)
        }
        c.drawRect(0f, h - 44f, w, h, dim)
        c.drawText(LEGEND, 16f, h - 14f, small)
    }

    private fun line(c: Canvas, x: Float, y: Float, s: String, paint: Paint): Float {
        c.drawText(s, x, y, paint)
        return x + paint.measureText(s) + 28f
    }

    private fun viewName(source: Int) = when (source) {
        0 -> "EO+IR"
        1 -> "EO"
        2 -> "IR"
        3 -> "IR+EO"
        4 -> "SYNC"
        6 -> "SIDE BY SIDE"
        else -> "VIEW ?"
    }

    companion object {
        const val WIDTH = 1600
        const val HEIGHT = 900
        const val PERIOD_MS = 66L
        const val NOTICE_MS = 4000L
        const val LEGEND = "A head · B stop track · trigger track · grip point · X photo · Y rec · L-trigger view · L-grip center · L-stick zoom · sticks click: recentre / place screen · menu profile"
    }
}
