package aero.imago.payloadcontrol.quest

import android.content.Context
import android.graphics.Canvas
import android.graphics.LinearGradient
import android.graphics.Shader
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RadialGradient
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
internal class HudPainter(
    context: Context,
    private val surface: Surface?,
    private val profileNames: List<String>,
    private val backend: Backend = NativeBackend,
    /** False in tests: hover, press and fades jump to their end state. */
    private val animated: Boolean = true,
    private val onExit: () -> Unit,
) {
    /** Where the snapshot comes from and the commands go: the native app, or a test. */
    interface Backend {
        fun snapshot(out: FloatArray): Boolean
        fun command(command: Int, a: Float, b: Float)
        fun paletteName(cameraType: Int, index: Int): String
        /** A sheet covers the picture: the native side brings its layer forward. */
        fun sheetOpen(open: Boolean)
    }

    object NativeBackend : Backend {
        override fun snapshot(out: FloatArray) = NativeBridge.hudSnapshot(out)
        override fun command(command: Int, a: Float, b: Float) = NativeBridge.command(command, a, b)
        override fun paletteName(cameraType: Int, index: Int) = NativeBridge.irPaletteName(cameraType, index)
        override fun sheetOpen(open: Boolean) = NativeBridge.setSheetOpen(open)
    }

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
    private var darkWarned = false // the notice about head tracking in the dark was given
    private var dimLevel = 0.35f // what "dimmed" means, kept while the room is clear or dark

    // Animation: seconds since the last frame, and eased values that follow their targets.
    private var lastFrameMs = 0L
    private var dt = 0f
    private val hoverAmount = HashMap<String, Float>() // per control, 0..1
    private var railsAlpha = 1f
    private var railsSeenMs = 0L   // when the pointer was last on a rail
    private var sheetAmount = 0f   // Connections fading in
    private var sheetReported = false
    private val clip = Path()

    /** Moves `value` towards `target`, reaching it in about `seconds`. */
    private fun ease(value: Float, target: Float, seconds: Float): Float {
        if (!animated || seconds <= 0f) return target
        val step = dt / seconds
        return if (value < target) minOf(target, value + step) else maxOf(target, value - step)
    }

    // The tablet app's fonts: Space Grotesk for labels and figures, Manrope for body text,
    // and Material Icons (the desktop app's icon font: an icon is its name, as a ligature).
    private val grotesk600: Typeface = context.resources.getFont(R.font.space_grotesk_600)
    private val grotesk700: Typeface = context.resources.getFont(R.font.space_grotesk_700)
    private val grotesk500: Typeface = context.resources.getFont(R.font.space_grotesk_500)
    private val manrope500: Typeface = context.resources.getFont(R.font.manrope_500)
    private val manrope600: Typeface = context.resources.getFont(R.font.manrope_600)
    private val icons: Typeface = context.resources.getFont(R.font.material_icons)

    private val fill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeWidth = 2f }
    private val label = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = TEXT
        textSize = 24f
        typeface = grotesk600
        textAlign = Paint.Align.CENTER
    }
    private val icon = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = TEXT
        typeface = icons
        textAlign = Paint.Align.CENTER
        fontFeatureSettings = "'liga' 1"
    }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = TEXT
        textSize = 24f
        typeface = grotesk500
        fontFeatureSettings = "'tnum' 1" // tabular figures: telemetry does not jitter
    }
    private val caption = Paint(Paint.ANTI_ALIAS_FLAG).apply { textSize = 18f; color = TEXT_DIM; typeface = grotesk600; letterSpacing = 0.08f }
    private val body = Paint(Paint.ANTI_ALIAS_FLAG).apply { textSize = 20f; color = TEXT_DIM; typeface = manrope500 }
    private val shadowed = Paint(Paint.ANTI_ALIAS_FLAG).apply { typeface = manrope600; setShadowLayer(4f, 0f, 0f, Color.BLACK) }

    fun notice(line: String) {
        synchronized(this) {
            noticeText = line
            noticeUntilMs = SystemClock.elapsedRealtime() + NOTICE_MS
        }
    }


    fun paint() {
        val target = surface ?: return
        if (!backend.snapshot(s)) return
        // Hardware canvas: the overlay is larger than a full HD screen and redrawn 30 times a
        // second. Software as a fallback.
        val canvas = try {
            target.lockHardwareCanvas()
        } catch (e: Exception) {
            try {
                target.lockCanvas(null)
            } catch (e2: Exception) {
                return
            }
        }
        try {
            canvas.drawColor(Color.TRANSPARENT, PorterDuff.Mode.CLEAR)
            frame(canvas)
        } finally {
            target.unlockCanvasAndPost(canvas)
        }
    }

    /** One frame onto any canvas (a test renders it to a bitmap). */
    fun paintOn(canvas: Canvas) {
        if (backend.snapshot(s)) frame(canvas)
    }

    private fun frame(canvas: Canvas) {
        val now = SystemClock.elapsedRealtime()
        dt = if (lastFrameMs == 0L) 0f else ((now - lastFrameMs) / 1000f).coerceIn(0f, 0.25f)
        lastFrameMs = now
        // Everything is laid out on a canvas WIDTH wide; the buffer may be denser (sharper
        // text at two metres).
        val scale = canvas.width / WIDTH.toFloat()
        canvas.save()
        canvas.scale(scale, scale)
        layout(canvas.width / scale, canvas.height / scale)
        readInput()
        draw(canvas)
        canvas.restore()
    }

    private fun on(i: Int) = s[i] >= 0.5f

    private fun layout(canvasWidth: Float, canvasHeight: Float) {
        width = canvasWidth
        height = canvasHeight
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
        val dim = s[Hud.ROOM_DIM]
        if (dim >= 0.03f && dim <= 0.97f) dimLevel = dim
        val count = s[Hud.CLICK_COUNT].toInt()
        click = if (lastClickCount >= 0 && count != lastClickCount) {
            floatArrayOf(toCanvasX(s[Hud.CLICK_X]), toCanvasY(s[Hud.CLICK_Y]))
        } else {
            null
        }
        lastClickCount = count
    }

    // MARK: Widgets (the tablet app's components, drawn by hand)

    private fun hover(r: RectF) = r.contains(pointerX, pointerY)

    /**
     * Glass: a translucent raised surface (the room and the picture show through), or the
     * brand gradient when active; `glow` (0..1) is how much the pointer lights it.
     */
    private fun surface(c: Canvas, r: RectF, radius: Float, active: Boolean, glow: Float, tint: Int? = null) {
        if (active && tint == null) {
            fill.shader = LinearGradient(r.left, r.top, r.right, r.bottom, intArrayOf(MAGENTA, FUCHSIA, PURPLE), null, Shader.TileMode.CLAMP)
        } else if (active) {
            fill.color = tint!!
        } else {
            // Lighter at the top, like light falling on glass.
            fill.shader = LinearGradient(0f, r.top, 0f, r.bottom, GLASS_TOP, GLASS_BOTTOM, Shader.TileMode.CLAMP)
        }
        c.drawRoundRect(r, radius, radius, fill)
        fill.shader = null
        if (glow > 0.01f && pointerX >= 0f) {
            // A soft light that follows the pointer inside the control.
            clip.rewind()
            clip.addRoundRect(r, radius, radius, Path.Direction.CW)
            c.save()
            c.clipPath(clip)
            fill.shader = RadialGradient(pointerX, pointerY, 120f, Color.argb((70 * glow).toInt(), 255, 255, 255), Color.TRANSPARENT, Shader.TileMode.CLAMP)
            c.drawRect(r, fill)
            fill.shader = null
            c.restore()
        }
        stroke.color = Color.argb((26 + 150 * glow).toInt(), 255, 255, 255)
        stroke.strokeWidth = 1.5f + glow
        c.drawRoundRect(r, radius, radius, stroke)
        stroke.strokeWidth = 2f
    }

    /**
     * One interactive control: eases its hover light, shrinks a little while the trigger is
     * held on it (as the tablet's buttons do under a finger), draws its surface and then its
     * content. True when it was pressed this frame.
     */
    private inline fun control(
        c: Canvas, r: RectF, radius: Float, key: String, active: Boolean, enabled: Boolean, tint: Int?,
        content: () -> Unit,
    ): Boolean {
        val hovered = enabled && hover(r)
        val glow = ease(hoverAmount[key] ?: 0f, if (hovered) 1f else 0f, 0.12f)
        hoverAmount[key] = glow
        val held = hovered && triggerDown
        c.save()
        if (held) c.scale(0.94f, 0.94f, r.centerX(), r.centerY())
        surface(c, r, radius, active || (held && tint == null), glow, tint)
        content()
        c.restore()
        return pressed(r, enabled)
    }

    private fun pressed(r: RectF, enabled: Boolean): Boolean {
        val p = click
        if (enabled && p != null && r.contains(p[0], p[1])) {
            click = null
            return true
        }
        return false
    }

    private fun fitLabel(title: String, maxWidth: Float, start: Float) {
        var size = start
        label.textSize = size
        while (size > 12f && label.measureText(title) > maxWidth) {
            size -= 1f
            label.textSize = size
        }
    }

    /** RailButton: an icon over an uppercase label. True when pressed this frame. */
    private fun railButton(
        c: Canvas, r: RectF, iconName: String, title: String?, active: Boolean = false,
        enabled: Boolean = true, tint: Int? = null, iconColor: Int = TEXT,
    ): Boolean = control(c, r, RADIUS, "rail:$iconName:${r.left.toInt()}:${r.top.toInt()}", active, enabled, tint) {
        icon.color = if (!enabled) TEXT_OFF else if (active) TEXT else iconColor
        icon.textSize = if (title != null) 38f else 44f
        val iconBaseline = if (title != null) r.centerY() + 6f else r.centerY() + 22f
        c.drawText(iconName, r.centerX(), iconBaseline, icon)
        if (title != null) {
            label.typeface = grotesk600
            label.color = if (enabled) TEXT else TEXT_OFF
            fitLabel(title.uppercase(Locale.US), r.width() - 10f, 18f)
            c.drawText(title.uppercase(Locale.US), r.centerX(), r.bottom - 12f, label)
        }
    }

    /** A rail button that acts while the trigger is held on it (HoldButton). */
    private fun holdButton(c: Canvas, r: RectF, iconName: String, enabled: Boolean): Boolean {
        val down = enabled && triggerDown && hover(r)
        railButton(c, r, iconName, null, active = down, enabled = enabled)
        return down
    }

    /** A text button (sheets). */
    private fun button(c: Canvas, r: RectF, title: String, active: Boolean = false, enabled: Boolean = true, tint: Int? = null, color: Int = TEXT): Boolean =
        control(c, r, RADIUS, "button:$title:${r.left.toInt()}:${r.top.toInt()}", active, enabled, tint) {
            label.typeface = grotesk600
            label.color = if (enabled) color else TEXT_OFF
            fitLabel(title, r.width() - 16f, 22f)
            c.drawText(title, r.centerX(), r.centerY() + label.textSize * 0.35f, label)
        }

    /** IconPill of the top bar. */
    private fun iconPill(c: Canvas, r: RectF, iconName: String, active: Boolean = false): Boolean =
        control(c, r, r.height() / 2, "pill:$iconName", active, true, null) {
            icon.color = TEXT
            icon.textSize = 34f
            c.drawText(iconName, r.centerX(), r.centerY() + 17f, icon)
        }

    /** The glass panel behind every control group: translucent, lit from above, with a soft shadow. */
    private fun panel(c: Canvas, r: RectF, radius: Float = 26f) {
        fill.setShadowLayer(18f, 0f, 6f, Color.argb(110, 0, 0, 0))
        fill.shader = LinearGradient(0f, r.top, 0f, r.bottom, PANEL_TOP, PANEL_BOTTOM, Shader.TileMode.CLAMP)
        c.drawRoundRect(r, radius, radius, fill)
        fill.shader = null
        fill.clearShadowLayer()
        stroke.color = STROKE
        stroke.strokeWidth = 1.5f
        c.drawRoundRect(r, radius, radius, stroke)
        // The highlight along the top edge.
        stroke.color = Color.argb(46, 255, 255, 255)
        c.drawLine(r.left + radius, r.top + 1f, r.right - radius, r.top + 1f, stroke)
        stroke.strokeWidth = 2f
    }

    /** StatusChip: an icon and a figure in a pill. Returns the x after it. */
    private fun chip(c: Canvas, x: Float, mid: Float, value: String, iconName: String? = null, color: Int = TEXT_DIM, dot: Int? = null): Float {
        text.textSize = 22f
        val textWidth = text.measureText(value)
        val lead = if (iconName != null) 34f else if (dot != null) 24f else 0f
        val r = RectF(x, mid - 22f, x + 18f + lead + textWidth + 18f, mid + 22f)
        fill.shader = LinearGradient(0f, r.top, 0f, r.bottom, PANEL_TOP, PANEL_BOTTOM, Shader.TileMode.CLAMP)
        c.drawRoundRect(r, 22f, 22f, fill)
        fill.shader = null
        stroke.color = STROKE
        stroke.strokeWidth = 1.5f
        c.drawRoundRect(r, 22f, 22f, stroke)
        stroke.strokeWidth = 2f
        if (iconName != null) {
            icon.color = color
            icon.textSize = 26f
            c.drawText(iconName, r.left + 18f + 13f, mid + 13f, icon)
        } else if (dot != null) {
            fill.color = dot
            c.drawCircle(r.left + 18f + 7f, mid, 7f, fill)
        }
        text.color = color
        c.drawText(value, r.left + 18f + lead, mid + 8f, text)
        return r.right + 10f
    }

    private fun send(command: Int, a: Float = 0f, b: Float = 0f) = backend.command(command, a, b)

    // MARK: Frame

    private fun draw(c: Canvas) {
        val connected = on(Hud.CONNECTED)
        val link = on(Hud.LINK_UP)
        val now = SystemClock.elapsedRealtime()

        drawPicture(c, connected)
        drawTopBar(c, connected, link)

        // The rails step back while nobody points at them (the picture is what matters in
        // flight) and come back at once under the pointer.
        val onRail = pointerX >= 0f && pointerY >= video.top && pointerY <= video.bottom &&
            (pointerX < video.left || pointerX > video.right)
        if (onRail || railsSeenMs == 0L) railsSeenMs = now
        val railsTarget = if (now - railsSeenMs < RAILS_STAY_MS) 1f else RAILS_RESTING_ALPHA
        railsAlpha = ease(railsAlpha, railsTarget, if (railsTarget > railsAlpha) 0.15f else 0.8f)
        val layer = if (railsAlpha < 0.995f) c.saveLayerAlpha(0f, video.top - 4f, width, video.bottom + 4f, (railsAlpha * 255).toInt()) else -1
        drawCameraRail(c, link)
        drawGimbalRail(c, link)
        if (layer >= 0) c.restoreToCount(layer)

        drawBottomBar(c, link)

        // Connections fades in and its layer comes forward (the native side moves it).
        sheetAmount = ease(sheetAmount, if (connectionsOpen) 1f else 0f, 0.15f)
        val sheetVisible = connectionsOpen || sheetAmount > 0.01f
        if (sheetVisible != sheetReported) {
            sheetReported = sheetVisible
            backend.sheetOpen(sheetVisible)
        }
        if (sheetVisible) {
            val amount = if (connectionsOpen) maxOf(sheetAmount, 0.02f) else sheetAmount
            val sheet = c.saveLayerAlpha(video.left, video.top, video.right, video.bottom, (amount * 255).toInt())
            // The picture steps back behind the sheet.
            fill.color = Color.argb(120, 0, 0, 0)
            val corner = PICTURE_CORNER * video.width()
            c.drawRoundRect(video, corner, corner, fill)
            drawConnections(c, connected)
            c.restoreToCount(sheet)
        }

        // A press nothing took: on the picture it tracks the object there, like a tap.
        val pressed = click
        if (pressed != null && !connectionsOpen && link && video.contains(pressed[0], pressed[1])) {
            send(Cmd.TRACK_AT, (pressed[0] - video.left) / video.width(), (pressed[1] - video.top) / video.height())
        }
        click = null

        // Where the ray lands: a soft halo in the accent colour around a bright ring.
        if (pointerX >= 0f) {
            fill.shader = RadialGradient(pointerX, pointerY, 34f, Color.argb(120, 0xEE, 0x49, 0xA7), Color.TRANSPARENT, Shader.TileMode.CLAMP)
            c.drawCircle(pointerX, pointerY, 34f, fill)
            fill.shader = null
            stroke.color = TEXT
            stroke.strokeWidth = 3f
            c.drawCircle(pointerX, pointerY, if (triggerDown) 7f else 10f, stroke)
            stroke.strokeWidth = 2f
        }
    }

    private fun drawPicture(c: Canvas, connected: Boolean) {
        val w = video.width()
        val h = video.height()
        // A thin frame on the picture's rounded edge (the native side rounds the video itself).
        val corner = PICTURE_CORNER * w
        stroke.color = Color.argb(60, 255, 255, 255)
        stroke.strokeWidth = 2f
        c.drawRoundRect(RectF(video.left + 1f, video.top + 1f, video.right - 1f, video.bottom - 1f), corner, corner, stroke)
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
            val line = when (fault) {
                1 -> "HEAD TRACKING STOPPED · the gimbal did not move"
                3 -> "HEAD TRACKING · angles need Gimbal v2 or the gimbal manager"
                else -> "HEAD TRACKING STOPPED · the gimbal moved away"
            }
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
        val mid = video.top / 2
        // Wordmark, then the link pill: the profile's name opens Connections.
        label.typeface = grotesk700
        label.textSize = 30f
        label.color = MAGENTA
        label.textAlign = Paint.Align.LEFT
        c.drawText("IMAGO", 16f, mid + 11f, label)
        val wordmark = label.measureText("IMAGO")
        label.textAlign = Paint.Align.CENTER

        val name = profileNames.getOrNull(s[Hud.PROFILE_INDEX].toInt()) ?: "—"
        val shown = if (name.length > 24) name.take(23) + "…" else name
        val state = if (link) "" else if (connected) "waiting for payload" else "off"
        text.textSize = 22f
        body.textSize = 19f
        val pillWidth = 18f + 24f + text.measureText(shown) + (if (state.isEmpty()) 0f else 12f + body.measureText(state)) + 18f
        val pill = RectF(16f + wordmark + 18f, mid - 24f, 16f + wordmark + 18f + pillWidth, mid + 24f)
        val pillGlow = ease(hoverAmount["profile"] ?: 0f, if (hover(pill)) 1f else 0f, 0.12f)
        hoverAmount["profile"] = pillGlow
        surface(c, pill, 24f, connectionsOpen, pillGlow)
        fill.color = if (link) OK else WARNING
        c.drawCircle(pill.left + 25f, mid, 7f, fill)
        text.color = TEXT
        c.drawText(shown, pill.left + 42f, mid + 8f, text)
        if (state.isNotEmpty()) {
            body.color = TEXT_DIM
            c.drawText(state, pill.left + 42f + text.measureText(shown) + 12f, mid + 7f, body)
        }
        if (pressed(pill, true)) connectionsOpen = !connectionsOpen

        var x = pill.right + 14f
        if (on(Hud.VIDEO_ACTIVE)) {
            x = chip(c, x, mid, String.format(Locale.US, "%.0f fps  %.1f Mb/s", s[Hud.FPS], s[Hud.BITRATE_KBPS] / 1000f), "speed")
        }
        if (on(Hud.GIMBAL_VALID)) {
            x = chip(c, x, mid, String.format(Locale.US, "P %+.1f°  Y %+.1f°", s[Hud.PITCH], s[Hud.YAW]), "control_camera",
                if (on(Hud.PAN_NEAR_LIMIT)) WARNING else TEXT_DIM)
        }
        if (on(Hud.LRF_VALID)) x = chip(c, x, mid, String.format(Locale.US, "%.0f m", s[Hud.LRF_RANGE_M]), "gps_fixed")
        if (on(Hud.STORAGE_VALID)) x = chip(c, x, mid, String.format(Locale.US, "%.0f GB", s[Hud.STORAGE_FREE_MIB] / 1024f), "sd_card")
        if (on(Hud.RECORDING)) {
            val secs = (s[Hud.RECORDING_TIME_MS] / 1000f).toInt()
            x = chip(c, x, mid, String.format(Locale.US, "REC %02d:%02d", secs / 60, secs % 60), color = TEXT, dot = DANGER)
        }
        if (s[Hud.GIMBAL_CONTROL].toInt() == 2) chip(c, x, mid, "OTHER IN CONTROL", "control_camera", WARNING)

        // Icon pills at the right, as on the tablets: screen, connections, exit.
        var right = width - 16f
        val exit = RectF(right - 72f, mid - 26f, right, mid + 26f)
        if (iconPill(c, exit, "close")) onExit()
        right = exit.left - 10f
        val connections = RectF(right - 72f, mid - 26f, right, mid + 26f)
        if (iconPill(c, connections, "tune", connectionsOpen)) connectionsOpen = !connectionsOpen
        right = connections.left - 10f
        val place = RectF(right - 72f, mid - 26f, right, mid + 26f)
        if (iconPill(c, place, "center_focus_weak")) send(Cmd.PLACE_PANEL)
        // The room around the screen, as video apps on the headset have it: as it is, dimmed,
        // or dark. One press steps to the next; the dimmed level is set in Connections.
        if (on(Hud.PASSTHROUGH_ACTIVE)) {
            right = place.left - 10f
            val room = RectF(right - 72f, mid - 26f, right, mid + 26f)
            val state = roomState()
            if (iconPill(c, room, ROOM_ICONS[state])) setRoom((state + 1) % 3)
        }
    }

    /** 0 the room as it is, 1 dimmed, 2 dark. */
    private fun roomState(): Int {
        val dim = s[Hud.ROOM_DIM]
        return if (dim < 0.03f) 0 else if (dim > 0.97f) 2 else 1
    }

    private fun setRoom(state: Int) {
        send(Cmd.ROOM_DIM, when (state) {
            0 -> 0f
            2 -> 1f
            else -> dimLevel
        })
    }


    /** Rail geometry: a translucent panel with two columns of rail buttons and captions. */
    private inner class Rail(val left: Float, val right: Float) {
        var y = video.top + 10f
        private val pad = 12f
        fun begin(c: Canvas, bottom: Float) = panel(c, RectF(left + 8f, video.top, right - 8f, bottom))
        fun caption(c: Canvas, title: String) {
            caption.textSize = 15f
            caption.color = TEXT_OFF
            c.drawText(title, left + 8f + pad + 2f, y + 18f, caption)
            y += 26f
        }
        fun row(heightPx: Float = BUTTON): RectF {
            val r = RectF(left + 8f + pad, y, right - 8f - pad, y + heightPx)
            y += heightPx + 8f
            return r
        }
        fun pair(heightPx: Float = BUTTON): Pair<RectF, RectF> {
            val mid = (left + right) / 2
            val a = RectF(left + 8f + pad, y, mid - 4f, y + heightPx)
            val b = RectF(mid + 4f, y, right - 8f - pad, y + heightPx)
            y += heightPx + 8f
            return a to b
        }
        fun gap() { y += 6f }
    }

    private fun drawCameraRail(c: Canvas, link: Boolean) {
        val rail = Rail(0f, video.left)
        rail.begin(c, video.top + 10f + 26f * 3 + (BUTTON + 8f) * 5 + 44f + 6f * 2 + 12f)
        rail.caption(c, "CAMERA")
        val (photo, record) = rail.pair()
        if (railButton(c, photo, "photo_camera", "Photo", enabled = link)) send(Cmd.PHOTO)
        val recording = on(Hud.RECORDING)
        if (railButton(c, record, if (recording) "stop" else "videocam", if (recording) "Stop" else "Record",
                active = recording, enabled = link, tint = DANGER)) {
            send(Cmd.RECORD_TOGGLE)
        }
        rail.gap()
        rail.caption(c, "VIEW")
        val view = s[Hud.VIEW_SOURCE].toInt()
        val (eo, ir) = rail.pair()
        if (railButton(c, eo, "visibility", "EO", active = view == 1, enabled = link)) send(Cmd.SET_VIEW, 1f)
        if (railButton(c, ir, "device_thermostat", "IR", active = view == 2, enabled = link)) send(Cmd.SET_VIEW, 2f)
        val (eoIr, irEo) = rail.pair()
        if (railButton(c, eoIr, "layers", "EO + IR", active = view == 0, enabled = link)) send(Cmd.SET_VIEW, 0f)
        if (railButton(c, irEo, "layers", "IR + EO", active = view == 3, enabled = link)) send(Cmd.SET_VIEW, 3f)
        rail.gap()
        rail.caption(c, "ZOOM")
        val (minus, plus) = rail.pair()
        val zoomOut = holdButton(c, minus, "zoom_out", link)
        val zoomIn = holdButton(c, plus, "zoom_in", link)
        val zoom = if (zoomIn) 1 else if (zoomOut) -1 else 0
        if (zoom != heldZoom) {
            heldZoom = zoom
            send(Cmd.ZOOM, zoom.toFloat())
        }
        val level = rail.row(36f)
        text.textSize = 28f
        text.color = if (s[Hud.ZOOM] > 25.05f) WARNING else TEXT // digital zoom in amber, as the slider marks it
        text.textAlign = Paint.Align.CENTER
        c.drawText(if (s[Hud.ZOOM] > 0f) String.format(Locale.US, "%.1f×", s[Hud.ZOOM]) else "—", level.centerX(), level.centerY() + 10f, text)
        text.textAlign = Paint.Align.LEFT
        if (railButton(c, rail.row(), "center_focus_strong", "Autofocus", enabled = link)) send(Cmd.AUTOFOCUS)
    }

    private fun drawGimbalRail(c: Canvas, link: Boolean) {
        val rail = Rail(video.right, width)
        val manager = s[Hud.GIMBAL_PROTOCOL].toInt() == 2
        rail.begin(c, video.top + 10f + 26f * 3 + (BUTTON + 8f) * 4 + 6f * 2 + 12f + (52f + 8f) * 2 + (if (manager) 64f + 8f else 0f))
        rail.caption(c, "HEAD TRACKING")
        val headOn = s[Hud.HEAD_MODE] >= 0.5f
        val (head, recentre) = rail.pair()
        if (railButton(c, head, "view_in_ar", if (headOn) "Head on" else "Head off", active = headOn, enabled = link)) send(Cmd.HEAD_TOGGLE)
        if (railButton(c, recentre, "my_location", "Recentre", enabled = link && headOn)) send(Cmd.HEAD_RECENTRE)
        // With head tracking on the screen goes with the head, or stays in the room.
        val follows = on(Hud.FOLLOW_HEAD)
        if (button(c, rail.row(52f), if (follows) "Screen follows" else "Screen stays", active = follows)) {
            send(Cmd.FOLLOW_HEAD, if (follows) 0f else 1f)
        }
        // Comfort: the clutch (the camera moves with the head only while the left grip is
        // held) and the vignette (the picture's edge darkens in fast head turns).
        val (clutch, vignette) = rail.pair(52f)
        if (button(c, clutch, "Clutch", active = on(Hud.CLUTCH))) send(Cmd.CLUTCH, if (on(Hud.CLUTCH)) 0f else 1f)
        if (button(c, vignette, "Vignette", active = on(Hud.VIGNETTE))) send(Cmd.VIGNETTE, if (on(Hud.VIGNETTE)) 0f else 1f)
        rail.gap()
        rail.caption(c, "GIMBAL")
        val mode = s[Hud.GIMBAL_MODE].toInt()
        val (lock, follow) = rail.pair()
        if (railButton(c, lock, "lock", "Lock", active = mode == 1, enabled = link)) send(Cmd.GIMBAL_MODE, 1f)
        if (railButton(c, follow, "navigation", "Follow", active = mode == 2, enabled = link)) send(Cmd.GIMBAL_MODE, 2f)
        val (mapping, center) = rail.pair()
        if (railButton(c, mapping, "map", "Mapping", active = mode == 3, enabled = link)) send(Cmd.GIMBAL_MODE, 3f)
        if (railButton(c, center, "filter_center_focus", "Center", enabled = link)) send(Cmd.GIMBAL_MODE, 4f)
        if (manager) {
            // Who drives the gimbal through the autopilot, as on the tablets: press to take
            // control, or to give it back.
            val ours = s[Hud.GIMBAL_CONTROL].toInt() == 1
            val holder = when (s[Hud.GIMBAL_CONTROL].toInt()) {
                1 -> "press to release"
                2 -> "held by ${s[Hud.CONTROL_SYSTEM].toInt()}/${s[Hud.CONTROL_COMPONENT].toInt()}"
                3 -> "nobody in control"
                else -> "no gimbal manager heard"
            }
            val r = rail.row(64f)
            val took = control(c, r, RADIUS, "control", ours, link, null) {
                label.typeface = grotesk700
                label.color = if (link) TEXT else TEXT_OFF
                fitLabel(if (ours) "IN CONTROL" else "TAKE CONTROL", r.width() - 16f, 19f)
                c.drawText(if (ours) "IN CONTROL" else "TAKE CONTROL", r.centerX(), r.centerY() - 2f, label)
                body.textSize = 15f
                body.color = if (ours) TEXT else TEXT_DIM
                body.textAlign = Paint.Align.CENTER
                c.drawText(holder, r.centerX(), r.centerY() + 20f, body)
                body.textAlign = Paint.Align.LEFT
            }
            if (took) send(Cmd.TAKE_CONTROL, if (ours) 0f else 1f)
        }
        rail.gap()
        rail.caption(c, "TRACKING")
        val tracking = s[Hud.TRACKING_STATE].toInt() != 0 || s[Hud.TRACKING_MODE].toInt() == 1
        val (stop, ai) = rail.pair()
        if (railButton(c, stop, "cancel", if (s[Hud.TRACKING_STATE].toInt() == 2) "Lost · stop" else "Stop",
                enabled = link && tracking, active = s[Hud.TRACKING_STATE].toInt() == 1, tint = DANGER)) {
            send(Cmd.TRACK_STOP)
        }
        if (railButton(c, ai, "auto_awesome", "AI", active = aiDetection, enabled = link)) {
            aiDetection = !aiDetection
            send(Cmd.TRACKER_FUNCTION, if (aiDetection) 1f else 0f)
        }
        if (!link) aiDetection = false
    }

    private fun drawBottomBar(c: Canvas, link: Boolean) {
        val mid1 = video.bottom + 34f
        val inverted = s[Hud.HEAD_INVERTED].toInt()
        val signs = if (inverted != 0) " [${if (inverted and 1 != 0) "P" else ""}${if (inverted and 2 != 0) "Y" else ""} reversed]" else ""
        val fault = s[Hud.HEAD_FAULT].toInt()
        val head = when {
            s[Hud.HEAD_MODE] < 0.5f -> "Head tracking off"
            fault == 1 -> "Head tracking stopped · the gimbal did not move · recentre to retry"
            fault == 2 -> "Head tracking stopped · the gimbal moved away · recentre to retry"
            fault == 3 -> "Head tracking by angles needs Gimbal v2 or the gimbal manager"
            on(Hud.HEAD_HELD) -> "Head tracking · held by the tracker"
            on(Hud.HEAD_CLUTCHED) -> "Head tracking · hold the left grip to move the camera"
            !on(Hud.HEAD_POSE_FRESH) -> "Head tracking · no pose"
            on(Hud.HEAD_FOLLOWING) -> String.format(Locale.US, "Head tracking · following (%.1f°)%s%s", s[Hud.HEAD_ERROR], signs,
                if (s[Hud.HEAD_SCALE] < 0.995f) String.format(Locale.US, " · ×%.2f", s[Hud.HEAD_SCALE]) else "")
            else -> "Head tracking · waiting for the gimbal"
        }
        var x = chip(c, video.left, mid1, head, "view_in_ar", if (fault != 0) DANGER else if (s[Hud.HEAD_MODE] >= 0.5f) MAGENTA else TEXT_DIM)
        val tracking = when (s[Hud.TRACKING_STATE].toInt()) {
            1 -> "Tracking"
            2 -> "Track lost"
            else -> if (s[Hud.TRACKING_MODE].toInt() == 1) "Tracker armed" else "Tracker idle"
        }
        x = chip(c, x, mid1, tracking, "touch_app", if (s[Hud.TRACKING_STATE].toInt() == 1) MAGENTA else TEXT_DIM)
        val detections = s[Hud.DETECTION_COUNT].toInt()
        if (detections > 0) x = chip(c, x, mid1, "$detections", "auto_awesome", OK)
        // Following the head with nothing of the room in view is the hardest on the stomach.
        val darkWhileTracking = s[Hud.HEAD_MODE] >= 0.5f && on(Hud.PASSTHROUGH_ACTIVE) && s[Hud.ROOM_DIM] > 0.97f
        if (darkWhileTracking) {
            x = chip(c, x, mid1, "Room dark: more motion sickness", "brightness_2", WARNING)
            if (!darkWarned) notice("Head tracking in the dark is harder on the stomach: keep the room visible (sun button)")
        }
        darkWarned = darkWhileTracking
        if (s[Hud.LATENCY_PRESENT_MS] > 0f) chip(c, x, mid1, String.format(Locale.US, "app %.0f ms", s[Hud.LATENCY_PRESENT_MS]))

        // Thermal bar while the IR picture is in view; the controls legend otherwise.
        val view = s[Hud.VIEW_SOURCE].toInt()
        val rowTop = video.bottom + 62f
        val rowBottom = height - 6f
        if (link && (view == 2 || view == 3 || view == 0) && rowBottom - rowTop > 30f) {
            val palette = s[Hud.IR_PALETTE].toInt()
            val type = s[Hud.IR_CAMERA_TYPE].toInt()
            val bar = RectF(video.left, rowTop, video.left + 560f, rowBottom)
            panel(c, bar, 18f)
            val inner = RectF(bar.left + 6f, bar.top + 5f, bar.right - 6f, bar.bottom - 5f)
            icon.color = TEXT_DIM
            icon.textSize = 28f
            c.drawText("palette", inner.left + 24f, inner.centerY() + 14f, icon)
            val prev = RectF(inner.left + 50f, inner.top, inner.left + 104f, inner.bottom)
            val next = RectF(inner.left + 330f, inner.top, inner.left + 384f, inner.bottom)
            if (button(c, prev, "‹", enabled = palette >= 0)) send(Cmd.PALETTE, ((palette - 1 + PALETTES) % PALETTES).toFloat())
            text.textAlign = Paint.Align.CENTER
            text.color = TEXT
            text.textSize = 22f
            c.drawText(if (palette >= 0) backend.paletteName(type, palette) else "—", (prev.right + next.left) / 2, inner.centerY() + 8f, text)
            text.textAlign = Paint.Align.LEFT
            if (button(c, next, "›", enabled = palette >= 0)) send(Cmd.PALETTE, ((palette + 1) % PALETTES).toFloat())
            if (button(c, RectF(inner.left + 400f, inner.top, inner.right, inner.bottom), "FFC")) send(Cmd.FFC)
        } else {
            body.textSize = 18f
            body.color = TEXT_OFF
            c.drawText(LEGEND, video.left, height - 16f, body)
        }
    }

    private fun drawConnections(c: Canvas, connected: Boolean) {
        val rows = profileNames.size
        val w = 820f
        val h = 96f + rows * 66f + 8f + (28f + 60f) * 3 + 8f + 60f + 22f
        val r = RectF(video.centerX() - w / 2, video.centerY() - h / 2, video.centerX() + w / 2, video.centerY() + h / 2)
        fill.color = Color.argb(245, 10, 11, 16)
        c.drawRoundRect(r, 26f, 26f, fill)
        stroke.color = STROKE
        c.drawRoundRect(r, 26f, 26f, stroke)
        label.typeface = grotesk700
        label.textSize = 28f
        label.color = TEXT
        c.drawText("Connections", r.centerX(), r.top + 46f, label)
        body.textSize = 17f
        body.color = TEXT_DIM
        body.textAlign = Paint.Align.CENTER
        c.drawText("Edit profiles on the 2D panel: close (✕) in the top bar", r.centerX(), r.top + 74f, body)
        body.textAlign = Paint.Align.LEFT
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

        // How gimbal commands travel, changed while connected (for this session; the panel
        // keeps the profile's own).
        fun segments(top: Float, left: Float, right: Float, titles: List<String>, selected: Int, onPick: (Int) -> Unit) {
            val gap = 8f
            val each = (right - left - gap * (titles.size - 1)) / titles.size
            titles.forEachIndexed { k, title ->
                val x = left + k * (each + gap)
                if (button(c, RectF(x, top, x + each, top + 52f), title, active = k == selected)) onPick(k)
            }
        }
        caption.textSize = 15f
        caption.color = TEXT_OFF
        c.drawText("GIMBAL COMMANDS", r.left + 24f, y + 18f, caption)
        y += 28f
        segments(y, r.left + 24f, r.right - 24f, listOf("Gimbal v2 (direct)", "Mount control", "Gimbal manager"),
            s[Hud.GIMBAL_PROTOCOL].toInt()) { send(Cmd.GIMBAL_PROTOCOL, it.toFloat()) }
        y += 60f

        // Head tracking: by angles (what the Orus L follows) or by rates, and the rate loop's gain.
        c.drawText("HEAD TRACKING", r.left + 24f, y + 18f, caption)
        val angle = s[Hud.HEAD_MODE_SETTING].toInt() != 1
        // Beside the mode: by angles, how much the head's motion shrinks with the zoom; by
        // rates, the loop's gain.
        c.drawText(if (angle) "SCALING WITH ZOOM" else "RATE GAIN  ·  °/s PER DEGREE", r.left + 24f + 380f, y + 18f, caption)
        y += 28f
        segments(y, r.left + 24f, r.left + 24f + 340f, listOf("Angle", "Rate"), if (angle) 0 else 1) {
            send(Cmd.HEAD_MODE, if (it == 0) 2f else 1f)
        }
        val gx = r.left + 24f + 380f
        val gain = s[Hud.HEAD_GAIN]
        val comp = s[Hud.HEAD_ZOOM_COMP]
        if (button(c, RectF(gx, y, gx + 80f, y + 52f), "−")) {
            if (angle) send(Cmd.HEAD_ZOOM_COMP, (comp - 0.1f).coerceAtLeast(0f)) else send(Cmd.HEAD_GAIN, (gain - 0.1f).coerceAtLeast(0.1f))
        }
        text.textAlign = Paint.Align.CENTER
        text.color = TEXT
        text.textSize = 26f
        c.drawText(if (angle) "${Math.round(comp * 100)} %" else String.format(Locale.US, "%.1f", gain), gx + 135f, y + 36f, text)
        text.textAlign = Paint.Align.LEFT
        if (button(c, RectF(gx + 190f, y, gx + 270f, y + 52f), "+")) {
            if (angle) send(Cmd.HEAD_ZOOM_COMP, (comp + 0.1f).coerceAtMost(1f)) else send(Cmd.HEAD_GAIN, (gain + 0.1f).coerceAtMost(20f))
        }
        if (angle && s[Hud.HEAD_SCALE] < 0.995f) {
            body.textSize = 16f
            body.color = TEXT_DIM
            c.drawText(String.format(Locale.US, "now ×%.2f", s[Hud.HEAD_SCALE]), gx + 284f, y + 34f, body)
        }
        y += 60f

        // The room around the screen: as it is, dimmed (how much: − / +), or dark.
        val roomOn = on(Hud.PASSTHROUGH_ACTIVE)
        c.drawText(if (roomOn) "ROOM AROUND THE SCREEN" else "ROOM AROUND THE SCREEN  ·  PASSTHROUGH IS OFF FOR THIS PROFILE",
            r.left + 24f, y + 18f, caption)
        if (roomOn) c.drawText("DIMMING", r.left + 24f + 500f, y + 18f, caption)
        y += 28f
        if (roomOn) {
            segments(y, r.left + 24f, r.left + 24f + 460f, listOf("Clear", "Dimmed", "Dark"), roomState()) { setRoom(it) }
            val dx = r.left + 24f + 500f
            if (button(c, RectF(dx, y, dx + 80f, y + 52f), "−")) {
                dimLevel = (dimLevel - 0.05f).coerceAtLeast(0.05f)
                send(Cmd.ROOM_DIM, dimLevel)
            }
            text.textAlign = Paint.Align.CENTER
            text.color = TEXT
            text.textSize = 26f
            c.drawText("${(dimLevel * 100).toInt()} %", dx + 135f, y + 36f, text)
            text.textAlign = Paint.Align.LEFT
            if (button(c, RectF(dx + 190f, y, dx + 270f, y + 52f), "+")) {
                dimLevel = (dimLevel + 0.05f).coerceAtMost(0.95f)
                send(Cmd.ROOM_DIM, dimLevel)
            }
        }
        y += 60f + 8f

        if (button(c, RectF(r.right - 404f, y, r.right - 214f, y + 52f), "Disconnect", enabled = connected, color = DANGER)) {
            send(Cmd.DISCONNECT)
            connectionsOpen = false
        }
        if (button(c, RectF(r.right - 204f, y, r.right - 24f, y + 52f), "Close")) connectionsOpen = false
        // Presses inside the sheet never reach the picture.
        val p = click
        if (p != null && r.contains(p[0], p[1])) click = null
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
        const val GIMBAL_PROTOCOL = 18
        const val TAKE_CONTROL = 19
        const val HEAD_MODE = 20
        const val ROOM_DIM = 21
        const val FOLLOW_HEAD = 22
        const val HEAD_ZOOM_COMP = 23
        const val CLUTCH = 24
        const val VIGNETTE = 25
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
        const val RADIUS = 24f  // 13 dp on the tablet
        const val BUTTON = 92f  // rail button height
        const val LEGEND = "Trigger: press a button, or track on the picture  ·  sticks: gimbal / zoom  ·  A head  ·  B stop track  ·  grip point  ·  X photo  ·  Y record"

        // The apps' palette (Theme.kt on Android).
        val SURFACE = Color.rgb(0x0A, 0x0B, 0x10)
        val RAISED = Color.rgb(0x14, 0x15, 0x1C)
        val RAISED_HOVER = Color.rgb(0x2A, 0x2C, 0x38)
        val OUTLINE = Color.rgb(0x3A, 0x3C, 0x48)
        val TEXT = Color.rgb(0xF5, 0xF6, 0xF8)
        val TEXT_DIM = Color.argb(158, 0xF5, 0xF6, 0xF8) // text at 62 %
        val TEXT_OFF = Color.argb(97, 0xF5, 0xF6, 0xF8)  // text at 38 %
        val MAGENTA = Color.rgb(0xEE, 0x49, 0xA7)
        val FUCHSIA = Color.rgb(0xC2, 0x18, 0x8F)
        val PURPLE = Color.rgb(0x7E, 0x2D, 0x91)
        // Glass: the surfaces at about half strength, a little lighter at the top.
        val PANEL_TOP = Color.argb(140, 0x1A, 0x1C, 0x26)
        val PANEL_BOTTOM = Color.argb(120, 0x0A, 0x0B, 0x10)
        val GLASS_TOP = Color.argb(150, 0x2A, 0x2C, 0x3A)
        val GLASS_BOTTOM = Color.argb(130, 0x14, 0x15, 0x1C)
        /** The picture's corner radius as a fraction of its width (kPictureCorner in gl_renderer.cpp). */
        const val PICTURE_CORNER = 0.018f
        /** The buffer is this many times denser than the layout. */
        const val BUFFER_SCALE = 1.5f
        const val RAILS_STAY_MS = 4000L
        const val RAILS_RESTING_ALPHA = 0.4f
        val ROOM_ICONS = arrayOf("brightness_high", "brightness_medium", "brightness_2")
        val STROKE = Color.argb(26, 255, 255, 255)      // white at 10 %
        val DANGER = Color.rgb(0xEF, 0x44, 0x44)
        val OK = Color.rgb(0x34, 0xD3, 0x99)
        val WARNING = Color.rgb(0xFB, 0xBF, 0x24)
    }
}
