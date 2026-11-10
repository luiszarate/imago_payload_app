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
 * The operator interface around and over the virtual screen, laid out like the other apps:
 * a top bar (connection, status), the camera rail on the left, the gimbal rail on the right,
 * a bottom bar (head tracking, tracker, thermal controls, notices), and over the picture the
 * tracking box, the AI detections, the pointer and the video badge.
 *
 * Painted with a Canvas onto a Surface the native side samples as a texture; transparent
 * over the picture. It is an immediate-mode interface: every frame the buttons are laid
 * out, hit-tested against the right controller's pointer and drawn. A press of the right
 * trigger arrives as a click counted by the native side; held buttons (zoom) follow the
 * trigger's level. Clicks on the picture itself start tracking there, as a tap does on the
 * tablets. Commands go back through NativeBridge.command and run on the XR thread.
 */
internal class HudPainter(private val surface: Surface, private val profileNames: List<String>) {
    /** The canvas follows the picture's aspect; QuestActivity resizes the buffer with it. */
    @Volatile var videoAspect: Float = 16f / 9f

    private val s = FloatArray(Hud.SIZE)

    // Layout of this frame, canvas pixels.
    private var video = RectF()
    private var width = 0f
    private var height = 0f

    // Input of this frame.
    private var pointerX = -1f
    private var pointerY = -1f
    private var triggerDown = false
    private var click: FloatArray? = null // canvas x, y of a press not handled yet
    private var lastClickCount = -1

    // Interface state.
    private var connectionsOpen = false
    private var aiDetection = false
    private var heldZoom = 0
    private var noticeText = ""
    private var noticeUntilMs = 0L

    private val fill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeWidth = 2f }
    private val label = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = TEXT
        textSize = 24f
        typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
        textAlign = Paint.Align.CENTER
    }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = TEXT
        textSize = 24f
        typeface = Typeface.MONOSPACE
    }
    private val caption = Paint(text).apply { textSize = 18f; color = TEXT_DIM; typeface = Typeface.SANS_SERIF }
    private val shadowed = Paint(text).apply { setShadowLayer(4f, 0f, 0f, Color.BLACK) }

    fun notice(line: String) {
        synchronized(this) {
            noticeText = line
            noticeUntilMs = SystemClock.elapsedRealtime() + NOTICE_MS
        }
    }


    fun paint() {
        if (!NativeBridge.hudSnapshot(s)) return
        val canvas = try {
            surface.lockCanvas(null)
        } catch (e: Exception) {
            return
        }
        try {
            canvas.drawColor(Color.TRANSPARENT, PorterDuff.Mode.CLEAR)
            layout(canvas)
            readInput()
            draw(canvas)
        } finally {
            surface.unlockCanvasAndPost(canvas)
        }
    }

    private fun on(i: Int) = s[i] >= 0.5f

    private fun layout(c: Canvas) {
        width = c.width.toFloat()
        height = c.height.toFloat()
        val videoWidth = width / (1 + 2 * Hud.MARGIN_SIDE)
        val videoHeight = height / (1 + Hud.MARGIN_TOP + Hud.MARGIN_BOTTOM)
        val left = videoWidth * Hud.MARGIN_SIDE
        val top = videoHeight * Hud.MARGIN_TOP
        video = RectF(left, top, left + videoWidth, top + videoHeight)
    }

    private fun toCanvasX(x: Float) = video.left + x * video.width()
    private fun toCanvasY(y: Float) = video.top + y * video.height()

    private fun readInput() {
        if (on(Hud.POINTER_VALID)) {
            pointerX = toCanvasX(s[Hud.POINTER_X])
            pointerY = toCanvasY(s[Hud.POINTER_Y])
        } else {
            pointerX = -1f
            pointerY = -1f
        }
        triggerDown = on(Hud.TRIGGER_DOWN)
        val count = s[Hud.CLICK_COUNT].toInt()
        click = if (lastClickCount >= 0 && count != lastClickCount) {
            floatArrayOf(toCanvasX(s[Hud.CLICK_X]), toCanvasY(s[Hud.CLICK_Y]))
        } else {
            null
        }
        lastClickCount = count
    }

    // MARK: Widgets

    private fun hover(r: RectF) = r.contains(pointerX, pointerY)

    /** Draws a button; true when it was pressed this frame. */
    private fun button(
        c: Canvas, r: RectF, title: String, active: Boolean = false, enabled: Boolean = true,
        tint: Int = MAGENTA, sub: String? = null,
    ): Boolean {
        val hovered = enabled && hover(r)
        fill.color = when {
            active -> tint
            hovered -> RAISED_HOVER
            else -> RAISED
        }
        c.drawRoundRect(r, RADIUS, RADIUS, fill)
        stroke.color = if (hovered) TEXT else if (active) tint else OUTLINE
        c.drawRoundRect(r, RADIUS, RADIUS, stroke)
        label.color = if (!enabled) TEXT_OFF else if (active) Color.BLACK else TEXT
        var size = 24f
        label.textSize = size
        while (size > 13f && label.measureText(title) > r.width() - 14f) {
            size -= 1f
            label.textSize = size
        }
        val baseline = r.centerY() + (if (sub != null) -2f else 8f)
        c.drawText(title, r.centerX(), baseline, label)
        if (sub != null) {
            caption.textAlign = Paint.Align.CENTER
            caption.color = if (active) Color.BLACK else TEXT_DIM
            c.drawText(sub, r.centerX(), baseline + 22f, caption)
            caption.textAlign = Paint.Align.LEFT
            caption.color = TEXT_DIM
        }
        val pressed = click
        if (enabled && pressed != null && r.contains(pressed[0], pressed[1])) {
            click = null
            return true
        }
        return false
    }

    /** A button that acts while the trigger is held on it. */
    private fun held(c: Canvas, r: RectF, title: String, enabled: Boolean): Boolean {
        val down = enabled && triggerDown && hover(r)
        button(c, r, title, active = down, enabled = enabled)
        return down
    }

    private fun panel(c: Canvas, r: RectF) {
        fill.color = SURFACE
        c.drawRect(r, fill)
    }

    private fun chip(c: Canvas, x: Float, y: Float, textValue: String, color: Int = TEXT): Float {
        text.color = color
        c.drawText(textValue, x, y, text)
        return x + text.measureText(textValue) + 26f
    }

    private fun send(command: Int, a: Float = 0f, b: Float = 0f) = NativeBridge.command(command, a, b)

    // MARK: Frame

    private fun draw(c: Canvas) {
        val connected = on(Hud.CONNECTED)
        val link = on(Hud.LINK_UP)

        drawPicture(c, connected)
        drawTopBar(c, connected, link)
        drawCameraRail(c, link)
        drawGimbalRail(c, link)
        drawBottomBar(c, link)
        if (connectionsOpen) drawConnections(c, connected)

        // A press nothing took: on the picture it tracks the object there, like a tap.
        val pressed = click
        if (pressed != null && !connectionsOpen && link && video.contains(pressed[0], pressed[1])) {
            send(Cmd.TRACK_AT, (pressed[0] - video.left) / video.width(), (pressed[1] - video.top) / video.height())
        }
        click = null

        if (pointerX >= 0f) {
            stroke.color = WARNING
            stroke.strokeWidth = 3f
            c.drawCircle(pointerX, pointerY, 12f, stroke)
            c.drawLine(pointerX - 22f, pointerY, pointerX - 14f, pointerY, stroke)
            c.drawLine(pointerX + 14f, pointerY, pointerX + 22f, pointerY, stroke)
            c.drawLine(pointerX, pointerY - 22f, pointerX, pointerY - 14f, stroke)
            c.drawLine(pointerX, pointerY + 14f, pointerX, pointerY + 22f, stroke)
            stroke.strokeWidth = 2f
        }
    }

    private fun drawPicture(c: Canvas, connected: Boolean) {
        val w = video.width()
        val h = video.height()
        if (on(Hud.BOX_VALID)) {
            stroke.color = MAGENTA
            stroke.strokeWidth = 4f
            c.drawRect(RectF(video.left + s[Hud.BOX_X] * w, video.top + s[Hud.BOX_Y] * h,
                video.left + (s[Hud.BOX_X] + s[Hud.BOX_W]) * w, video.top + (s[Hud.BOX_Y] + s[Hud.BOX_H]) * h), stroke)
        }
        val detections = s[Hud.DETECTION_COUNT].toInt().coerceIn(0, Hud.MAX_DETECTIONS)
        stroke.color = OK
        stroke.strokeWidth = 3f
        for (k in 0 until detections) {
            val base = Hud.DETECTIONS + k * Hud.DETECTION_STRIDE
            val left = video.left + s[base] * w
            val top = video.top + s[base + 1] * h
            c.drawRect(RectF(left, top, left + s[base + 2] * w, top + s[base + 3] * h), stroke)
            shadowed.textSize = 20f
            shadowed.color = OK
            c.drawText("${DetectionLabels.name(s[base + 4].toInt())} ${(s[base + 5] * 100).toInt()}%", left + 4f, top - 6f, shadowed)
        }
        stroke.strokeWidth = 2f

        // Video badge, top centre, one at a time (most urgent first), as on the tablets.
        val badge = when {
            !connected -> null
            !on(Hud.LINK_UP) -> "NO LINK"
            !on(Hud.VIDEO_ACTIVE) -> "VIDEO RECONNECTING…"
            on(Hud.LAG_STALE) -> String.format(Locale.US, "VIDEO %.1f s BEHIND", s[Hud.LAG_MS] / 1000f)
            else -> null
        }
        if (badge != null) {
            label.textSize = 26f
            val half = label.measureText(badge) / 2 + 20f
            val r = RectF(video.centerX() - half, video.top + 14f, video.centerX() + half, video.top + 58f)
            fill.color = WARNING
            c.drawRoundRect(r, RADIUS, RADIUS, fill)
            label.color = Color.BLACK
            c.drawText(badge, r.centerX(), r.centerY() + 9f, label)
        }
        // Head tracking stopped by its own guard: say so where the operator is looking.
        val fault = s[Hud.HEAD_FAULT].toInt()
        if (fault != 0 && s[Hud.HEAD_MODE] >= 0.5f) {
            val line = if (fault == 1) "HEAD TRACKING STOPPED · the gimbal did not move" else "HEAD TRACKING STOPPED · the gimbal moved away"
            label.textSize = 24f
            val half = label.measureText(line) / 2 + 20f
            val r = RectF(video.centerX() - half, video.bottom - 64f, video.centerX() + half, video.bottom - 20f)
            fill.color = DANGER
            c.drawRoundRect(r, RADIUS, RADIUS, fill)
            label.color = TEXT
            c.drawText(line, r.centerX(), r.centerY() + 8f, label)
        }
        // Notices: top right of the picture, beside the gimbal rail.
        val notice = synchronized(this) { if (SystemClock.elapsedRealtime() < noticeUntilMs) noticeText else "" }
        if (notice.isNotEmpty()) {
            shadowed.textSize = 22f
            val shown = if (notice.length > 64) notice.take(63) + "…" else notice
            val tw = shadowed.measureText(shown)
            val r = RectF(video.right - tw - 36f, video.top + 14f, video.right - 12f, video.top + 54f)
            fill.color = Color.argb(200, 20, 21, 28)
            c.drawRoundRect(r, RADIUS, RADIUS, fill)
            shadowed.color = MAGENTA
            c.drawText(shown, r.left + 12f, r.centerY() + 8f, shadowed)
        }
    }

    private fun drawTopBar(c: Canvas, connected: Boolean, link: Boolean) {
        val bar = RectF(0f, 0f, width, video.top)
        panel(c, bar)
        val mid = bar.centerY()
        label.textSize = 26f
        label.color = MAGENTA
        label.textAlign = Paint.Align.LEFT
        c.drawText("IMAGO", 18f, mid + 9f, label)
        label.textAlign = Paint.Align.CENTER

        // Connection: the profile's name opens the list, as "Connections" does on the tablets.
        val name = profileNames.getOrNull(s[Hud.PROFILE_INDEX].toInt()) ?: "—"
        val profile = RectF(120f, 10f, 120f + 320f, bar.bottom - 10f)
        if (button(c, profile, if (name.length > 22) name.take(21) + "…" else name, active = connectionsOpen,
                sub = if (link) "connected" else if (connected) "connecting…" else "off")) {
            connectionsOpen = !connectionsOpen
        }

        var x = profile.right + 24f
        val y = mid + 9f
        text.textSize = 24f
        x = chip(c, x, y, if (link) "LINK" else "NO LINK", if (link) OK else DANGER)
        if (on(Hud.VIDEO_ACTIVE)) {
            x = chip(c, x, y, String.format(Locale.US, "%.0f fps %.1f Mb/s", s[Hud.FPS], s[Hud.BITRATE_KBPS] / 1000f))
        }
        if (on(Hud.GIMBAL_VALID)) {
            x = chip(c, x, y, String.format(Locale.US, "P %+.1f°  Y %+.1f°", s[Hud.PITCH], s[Hud.YAW]),
                if (on(Hud.PAN_NEAR_LIMIT)) WARNING else TEXT)
        }
        if (on(Hud.LRF_VALID)) x = chip(c, x, y, String.format(Locale.US, "LRF %.0f m", s[Hud.LRF_RANGE_M]))
        if (on(Hud.STORAGE_VALID)) x = chip(c, x, y, String.format(Locale.US, "SD %.1f GB", s[Hud.STORAGE_FREE_MIB] / 1024f))
        if (on(Hud.RECORDING)) {
            val secs = (s[Hud.RECORDING_TIME_MS] / 1000f).toInt()
            x = chip(c, x, y, String.format(Locale.US, "REC %02d:%02d", secs / 60, secs % 60), DANGER)
        }
        if (s[Hud.GIMBAL_CONTROL].toInt() == 2) chip(c, x, y, "GIMBAL: OTHER IN CONTROL", WARNING)

        val place = RectF(width - 190f, 10f, width - 14f, bar.bottom - 10f)
        if (button(c, place, "SCREEN", sub = "bring here")) send(Cmd.PLACE_PANEL)
    }

    /** Rail geometry: buttons stacked from the top of the picture, with section captions. */
    private inner class Rail(val left: Float, val right: Float) {
        var y = video.top + 12f
        fun caption(c: Canvas, title: String) {
            caption.textSize = 17f
            c.drawText(title, left + 14f, y + 16f, caption)
            y += 24f
        }
        fun row(heightPx: Float = 58f): RectF {
            val r = RectF(left + 12f, y, right - 12f, y + heightPx)
            y += heightPx + 8f
            return r
        }
        fun pair(heightPx: Float = 54f): Pair<RectF, RectF> {
            val mid = (left + right) / 2
            val a = RectF(left + 12f, y, mid - 4f, y + heightPx)
            val b = RectF(mid + 4f, y, right - 12f, y + heightPx)
            y += heightPx + 8f
            return a to b
        }
        fun gap() { y += 10f }
    }

    private fun drawCameraRail(c: Canvas, link: Boolean) {
        panel(c, RectF(0f, video.top, video.left, video.bottom))
        val rail = Rail(0f, video.left)
        rail.caption(c, "CAMERA")
        if (button(c, rail.row(), "PHOTO", enabled = link)) send(Cmd.PHOTO)
        val recording = on(Hud.RECORDING)
        if (button(c, rail.row(), if (recording) "STOP" else "RECORD", active = recording, enabled = link, tint = DANGER)) {
            send(Cmd.RECORD_TOGGLE)
        }
        rail.gap()
        rail.caption(c, "VIEW")
        val view = s[Hud.VIEW_SOURCE].toInt()
        val (eo, ir) = rail.pair()
        if (button(c, eo, "EO", active = view == 1, enabled = link)) send(Cmd.SET_VIEW, 1f)
        if (button(c, ir, "IR", active = view == 2, enabled = link)) send(Cmd.SET_VIEW, 2f)
        val (eoIr, irEo) = rail.pair()
        if (button(c, eoIr, "EO+IR", active = view == 0, enabled = link)) send(Cmd.SET_VIEW, 0f)
        if (button(c, irEo, "IR+EO", active = view == 3, enabled = link)) send(Cmd.SET_VIEW, 3f)
        rail.gap()
        rail.caption(c, "ZOOM")
        val zoomIn = held(c, rail.row(), "+", link)
        val level = rail.row(40f)
        text.textSize = 26f
        text.color = if (s[Hud.ZOOM] > 25.05f) WARNING else TEXT
        text.textAlign = Paint.Align.CENTER
        c.drawText(if (s[Hud.ZOOM] > 0f) String.format(Locale.US, "%.1fx", s[Hud.ZOOM]) else "—", level.centerX(), level.centerY() + 9f, text)
        text.textAlign = Paint.Align.LEFT
        val zoomOut = held(c, rail.row(), "−", link)
        val zoom = if (zoomIn) 1 else if (zoomOut) -1 else 0
        if (zoom != heldZoom) {
            heldZoom = zoom
            send(Cmd.ZOOM, zoom.toFloat())
        }
        if (button(c, rail.row(50f), "AUTOFOCUS", enabled = link)) send(Cmd.AUTOFOCUS)
    }

    private fun drawGimbalRail(c: Canvas, link: Boolean) {
        panel(c, RectF(video.right, video.top, width, video.bottom))
        val rail = Rail(video.right, width)
        rail.caption(c, "HEAD TRACKING")
        val headOn = s[Hud.HEAD_MODE] >= 0.5f
        if (button(c, rail.row(), if (headOn) "HEAD ON" else "HEAD OFF", active = headOn, enabled = link)) send(Cmd.HEAD_TOGGLE)
        if (button(c, rail.row(50f), "RECENTRE", enabled = link && headOn, sub = null)) send(Cmd.HEAD_RECENTRE)
        rail.gap()
        rail.caption(c, "GIMBAL")
        val mode = s[Hud.GIMBAL_MODE].toInt()
        val (lock, follow) = rail.pair()
        if (button(c, lock, "LOCK", active = mode == 1, enabled = link)) send(Cmd.GIMBAL_MODE, 1f)
        if (button(c, follow, "FOLLOW", active = mode == 2, enabled = link)) send(Cmd.GIMBAL_MODE, 2f)
        val (mapping, center) = rail.pair()
        if (button(c, mapping, "MAPPING", active = mode == 3, enabled = link)) send(Cmd.GIMBAL_MODE, 3f)
        if (button(c, center, "CENTER", enabled = link)) send(Cmd.GIMBAL_MODE, 4f)
        rail.gap()
        rail.caption(c, "TRACKING")
        val tracking = s[Hud.TRACKING_STATE].toInt() != 0 || s[Hud.TRACKING_MODE].toInt() == 1
        if (button(c, rail.row(), "STOP TRACK", enabled = link && tracking, tint = WARNING, active = s[Hud.TRACKING_STATE].toInt() == 1)) {
            send(Cmd.TRACK_STOP)
        }
        if (button(c, rail.row(), "AI DETECT", active = aiDetection, enabled = link)) {
            aiDetection = !aiDetection
            send(Cmd.TRACKER_FUNCTION, if (aiDetection) 1f else 0f)
        }
        if (!link) aiDetection = false
    }

    private fun drawBottomBar(c: Canvas, link: Boolean) {
        val bar = RectF(0f, video.bottom, width, height)
        panel(c, bar)
        val row1 = bar.top + 40f
        text.textSize = 24f
        val inverted = s[Hud.HEAD_INVERTED].toInt()
        val signs = if (inverted != 0) " [${if (inverted and 1 != 0) "P" else ""}${if (inverted and 2 != 0) "Y" else ""} reversed]" else ""
        val fault = s[Hud.HEAD_FAULT].toInt()
        val head = when {
            s[Hud.HEAD_MODE] < 0.5f -> "HEAD OFF"
            fault == 1 -> "HEAD STOPPED · gimbal did not move · recentre to retry"
            fault == 2 -> "HEAD STOPPED · gimbal moved away · recentre to retry"
            on(Hud.HEAD_HELD) -> "HEAD ON · held by tracker"
            !on(Hud.HEAD_POSE_FRESH) -> "HEAD ON · no pose"
            on(Hud.HEAD_FOLLOWING) -> String.format(Locale.US, "HEAD ON · following (%.1f°)%s", s[Hud.HEAD_ERROR], signs)
            else -> "HEAD ON · waiting for the gimbal"
        }
        var x = chip(c, 18f, row1, head, if (fault != 0) DANGER else if (s[Hud.HEAD_MODE] >= 0.5f) MAGENTA else TEXT_DIM)
        val tracking = when (s[Hud.TRACKING_STATE].toInt()) {
            1 -> "TRACKING"
            2 -> "TRACK LOST"
            else -> if (s[Hud.TRACKING_MODE].toInt() == 1) "TRACKER ARMED" else "TRACKER IDLE"
        }
        x = chip(c, x, row1, tracking, if (s[Hud.TRACKING_STATE].toInt() == 1) MAGENTA else TEXT_DIM)
        val detections = s[Hud.DETECTION_COUNT].toInt()
        if (detections > 0) x = chip(c, x, row1, "AI $detections", OK)
        if (s[Hud.LATENCY_PRESENT_MS] > 0f) chip(c, x, row1, String.format(Locale.US, "app %.0f ms", s[Hud.LATENCY_PRESENT_MS]), TEXT_DIM)

        // Thermal bar while the IR picture is in view; the controls legend otherwise.
        val view = s[Hud.VIEW_SOURCE].toInt()
        val rowTop = bar.top + 56f
        val rowBottom = bar.bottom - 10f
        if (link && (view == 2 || view == 3 || view == 0) && rowBottom - rowTop > 30f) {
            val palette = s[Hud.IR_PALETTE].toInt()
            val type = s[Hud.IR_CAMERA_TYPE].toInt()
            caption.textSize = 17f
            c.drawText("IR PALETTE", 18f, (rowTop + rowBottom) / 2 + 6f, caption)
            val prev = RectF(140f, rowTop, 200f, rowBottom)
            val nameBox = RectF(208f, rowTop, 468f, rowBottom)
            val next = RectF(476f, rowTop, 536f, rowBottom)
            if (button(c, prev, "‹", enabled = palette >= 0)) send(Cmd.PALETTE, ((palette - 1 + PALETTES) % PALETTES).toFloat())
            text.textAlign = Paint.Align.CENTER
            text.color = TEXT
            c.drawText(if (palette >= 0) NativeBridge.irPaletteName(type, palette) else "—", nameBox.centerX(), nameBox.centerY() + 8f, text)
            text.textAlign = Paint.Align.LEFT
            if (button(c, next, "›", enabled = palette >= 0)) send(Cmd.PALETTE, ((palette + 1) % PALETTES).toFloat())
            if (button(c, RectF(560f, rowTop, 680f, rowBottom), "FFC")) send(Cmd.FFC)
        } else {
            caption.textSize = 17f
            c.drawText(LEGEND, 18f, bar.bottom - 16f, caption)
        }
    }

    private fun drawConnections(c: Canvas, connected: Boolean) {
        val rows = profileNames.size
        val w = 760f
        val h = 150f + rows * 66f + 150f
        val r = RectF(video.centerX() - w / 2, video.centerY() - h / 2, video.centerX() + w / 2, video.centerY() + h / 2)
        fill.color = Color.argb(245, 10, 11, 16)
        c.drawRoundRect(r, 16f, 16f, fill)
        stroke.color = OUTLINE
        c.drawRoundRect(r, 16f, 16f, stroke)
        label.textSize = 28f
        label.color = TEXT
        c.drawText("Connections", r.centerX(), r.top + 46f, label)
        caption.textSize = 17f
        caption.textAlign = Paint.Align.CENTER
        c.drawText("Profiles come from profiles.json on the headset (see the manual)", r.centerX(), r.top + 74f, caption)
        caption.textAlign = Paint.Align.LEFT
        var y = r.top + 96f
        val active = s[Hud.PROFILE_INDEX].toInt()
        for (k in 0 until rows) {
            val row = RectF(r.left + 24f, y, r.right - 24f, y + 58f)
            if (button(c, row, profileNames[k], active = connected && k == active)) {
                send(Cmd.CONNECT, k.toFloat())
                connectionsOpen = false
            }
            y += 66f
        }
        y += 8f
        // Head tracking gain: the one figure worth changing in the field.
        caption.textSize = 18f
        c.drawText("Head tracking gain (°/s per degree of error)", r.left + 24f, y + 20f, caption)
        y += 30f
        val gain = s[Hud.HEAD_GAIN]
        if (button(c, RectF(r.left + 24f, y, r.left + 124f, y + 54f), "−")) send(Cmd.HEAD_GAIN, (gain - 0.5f).coerceAtLeast(0.5f))
        text.textAlign = Paint.Align.CENTER
        text.color = TEXT
        text.textSize = 26f
        c.drawText(String.format(Locale.US, "%.1f", gain), r.left + 184f, y + 36f, text)
        text.textAlign = Paint.Align.LEFT
        if (button(c, RectF(r.left + 244f, y, r.left + 344f, y + 54f), "+")) send(Cmd.HEAD_GAIN, (gain + 0.5f).coerceAtMost(20f))
        if (button(c, RectF(r.right - 404f, y, r.right - 214f, y + 54f), "DISCONNECT", enabled = connected, tint = DANGER)) {
            send(Cmd.DISCONNECT)
            connectionsOpen = false
        }
        if (button(c, RectF(r.right - 204f, y, r.right - 24f, y + 54f), "CLOSE")) connectionsOpen = false
        // Presses inside the sheet never reach the picture.
        val pressed = click
        if (pressed != null && r.contains(pressed[0], pressed[1])) click = null
    }

    /** Command ids: `Command` in hud_layout.h. */
    private object Cmd {
        const val TRACK_AT = 1
        const val SET_VIEW = 2
        const val PHOTO = 3
        const val RECORD_TOGGLE = 4
        const val ZOOM = 5
        const val AUTOFOCUS = 6
        const val HEAD_TOGGLE = 7
        const val HEAD_RECENTRE = 8
        const val GIMBAL_MODE = 9
        const val TRACK_STOP = 10
        const val TRACKER_FUNCTION = 11
        const val PALETTE = 12
        const val FFC = 13
        const val CONNECT = 14
        const val DISCONNECT = 15
        const val HEAD_GAIN = 16
        const val PLACE_PANEL = 17
    }

    companion object {
        /** Canvas height for a picture of this aspect (the width is fixed). */
        fun canvasHeight(aspect: Float): Int {
            val videoWidth = WIDTH / (1 + 2 * Hud.MARGIN_SIDE)
            return (videoWidth / aspect * (1 + Hud.MARGIN_TOP + Hud.MARGIN_BOTTOM)).toInt()
        }

        const val WIDTH = 1920
        const val PERIOD_MS = 33L
        const val NOTICE_MS = 4000L
        const val PALETTES = 10
        const val RADIUS = 10f
        const val LEGEND = "Point and pull the trigger: buttons, or track on the picture · sticks: gimbal / zoom · A head · B stop track · grip point · X photo · Y rec"

        // The apps' palette (Theme.kt on Android).
        val SURFACE = Color.rgb(0x0A, 0x0B, 0x10)
        val RAISED = Color.rgb(0x14, 0x15, 0x1C)
        val RAISED_HOVER = Color.rgb(0x2A, 0x2C, 0x38)
        val OUTLINE = Color.rgb(0x3A, 0x3C, 0x48)
        val TEXT = Color.rgb(0xF5, 0xF6, 0xF8)
        val TEXT_DIM = Color.rgb(0xA0, 0xA4, 0xB0)
        val TEXT_OFF = Color.rgb(0x5A, 0x5D, 0x68)
        val MAGENTA = Color.rgb(0xEE, 0x49, 0xA7)
        val DANGER = Color.rgb(0xEF, 0x44, 0x44)
        val OK = Color.rgb(0x34, 0xD3, 0x99)
        val WARNING = Color.rgb(0xFB, 0xBF, 0x24)
    }
}
