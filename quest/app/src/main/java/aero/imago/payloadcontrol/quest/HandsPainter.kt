package aero.imago.payloadcontrol.quest

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.PorterDuff
import android.graphics.RectF
import android.graphics.Shader
import android.graphics.Typeface
import android.view.Surface

/**
 * What each button does, drawn once: two cards side by side, the left controller's and the
 * right one's. The native side shows each half on a quad floating over its controller while
 * the operator looks at it (with the room dark the controllers cannot be seen at all).
 *
 * Each card is a sketch of a Touch controller from above, as it sits in the hand, with a
 * label on every control. The texts are the same as the table in quest/README.md.
 */
internal class HandsPainter(context: Context) {
    private val bold: Typeface = context.resources.getFont(R.font.space_grotesk_700)
    private val regular: Typeface = context.resources.getFont(R.font.manrope_600)
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeWidth = 3f; strokeCap = Paint.Cap.ROUND }
    private val key = Paint(Paint.ANTI_ALIAS_FLAG).apply { typeface = bold; textSize = 27f; color = HudPainter.TEXT }
    private val what = Paint(Paint.ANTI_ALIAS_FLAG).apply { typeface = regular; textSize = 23f; color = HudPainter.TEXT_DIM }

    fun paint(surface: Surface) {
        val canvas = try {
            surface.lockCanvas(null)
        } catch (e: Exception) {
            return
        }
        try {
            canvas.drawColor(Color.TRANSPARENT, PorterDuff.Mode.CLEAR)
            paintOn(canvas)
        } finally {
            surface.unlockCanvasAndPost(canvas)
        }
    }

    /** Both cards onto a canvas WIDTH x HEIGHT (a test renders them to a bitmap). */
    fun paintOn(canvas: Canvas) {
        canvas.save()
        canvas.scale(canvas.width / WIDTH.toFloat(), canvas.height / HEIGHT.toFloat())
        card(canvas, 0f, left = true)
        card(canvas, WIDTH / 2f, left = false)
        canvas.restore()
    }

    /** One control's label: its name, what it does, and a line to where it is on the sketch. */
    private fun label(c: Canvas, textX: Float, textY: Float, right: Boolean, name: String, does: String, toX: Float, toY: Float) {
        key.textAlign = if (right) Paint.Align.RIGHT else Paint.Align.LEFT
        what.textAlign = key.textAlign
        c.drawText(name, textX, textY, key)
        c.drawText(does, textX, textY + 27f, what)
        val width = maxOf(key.measureText(name), what.measureText(does))
        val fromX = if (right) textX - width - 10f else textX + width + 10f
        stroke.color = Color.argb(150, 0xEE, 0x49, 0xA7)
        stroke.strokeWidth = 2.5f
        c.drawLine(fromX, textY - 8f, toX, toY, stroke)
        fill.color = HudPainter.MAGENTA
        c.drawCircle(toX, toY, 5f, fill)
    }

    private fun card(c: Canvas, x0: Float, left: Boolean) {
        val r = RectF(x0 + 12f, 12f, x0 + WIDTH / 2f - 12f, HEIGHT - 12f)
        fill.shader = LinearGradient(0f, r.top, 0f, r.bottom, Color.argb(225, 0x1A, 0x1C, 0x26), Color.argb(225, 0x0A, 0x0B, 0x10), Shader.TileMode.CLAMP)
        c.drawRoundRect(r, 36f, 36f, fill)
        fill.shader = null
        stroke.color = Color.argb(60, 255, 255, 255)
        stroke.strokeWidth = 2f
        c.drawRoundRect(r, 36f, 36f, stroke)

        key.textAlign = Paint.Align.CENTER
        key.color = HudPainter.MAGENTA
        key.textSize = 24f
        c.drawText(if (left) "LEFT CONTROLLER" else "RIGHT CONTROLLER", r.centerX(), r.top + 40f, key)
        key.color = HudPainter.TEXT
        key.textSize = 27f

        // The sketch: the face with its stick and buttons, the trigger in front (up on the
        // card) and the grip on the outer side. `m` mirrors the left controller.
        val cx = r.centerX()
        val cy = 330f
        val m = if (left) -1f else 1f
        fun at(dx: Float) = cx + m * dx
        stroke.color = HudPainter.TEXT_DIM
        stroke.strokeWidth = 3f
        fill.color = Color.argb(255, 0x2A, 0x2C, 0x3A)
        // Trigger, front.
        val trigger = RectF(cx - 58f, cy - 132f, cx + 58f, cy - 92f)
        c.drawRoundRect(trigger, 18f, 18f, fill)
        c.drawRoundRect(trigger, 18f, 18f, stroke)
        // Grip, on the side away from the other hand.
        val grip = RectF(minOf(at(96f), at(126f)), cy - 30f, maxOf(at(96f), at(126f)), cy + 70f)
        c.drawRoundRect(grip, 14f, 14f, fill)
        c.drawRoundRect(grip, 14f, 14f, stroke)
        // Face.
        fill.color = Color.argb(255, 0x14, 0x15, 0x1C)
        c.drawCircle(cx, cy, 104f, fill)
        c.drawCircle(cx, cy, 104f, stroke)
        // Thumbstick, towards the other hand; two buttons outside it; the small one below.
        val stickX = at(-34f)
        val stickY = cy - 30f
        fill.color = Color.argb(255, 0x3A, 0x3C, 0x48)
        c.drawCircle(stickX, stickY, 34f, fill)
        c.drawCircle(stickX, stickY, 34f, stroke)
        c.drawCircle(stickX, stickY, 15f, stroke)
        val upperX = at(46f)
        val upperY = cy - 4f
        val lowerX = at(20f)
        val lowerY = cy + 46f
        for ((bx, by, name) in listOf(Triple(upperX, upperY, if (left) "Y" else "B"), Triple(lowerX, lowerY, if (left) "X" else "A"))) {
            c.drawCircle(bx, by, 21f, fill)
            c.drawCircle(bx, by, 21f, stroke)
            key.textAlign = Paint.Align.CENTER
            key.textSize = 22f
            c.drawText(name, bx, by + 8f, key)
            key.textSize = 27f
        }
        val smallX = at(-40f)
        val smallY = cy + 52f
        c.drawCircle(smallX, smallY, 10f, fill)
        c.drawCircle(smallX, smallY, 10f, stroke)

        // Labels: on the inner side of the card for the stick and the small button, on the
        // outer side for the buttons and the grip, the trigger's on top.
        val inner = if (left) r.right - 22f else r.left + 22f   // towards the other card
        val outer = if (left) r.left + 22f else r.right - 22f
        val innerRight = left   // text aligned to the right edge when it sits at the card's right
        val outerRight = !left
        if (left) {
            label(c, outer, 96f, outerRight, "TRIGGER", "next view", trigger.left + 20f, trigger.centerY())
            label(c, inner, 96f, innerRight, "STICK ↑ ↓", "zoom in / out", stickX, stickY - 34f)
            label(c, inner, 196f, innerRight, "STICK CLICK", "head: recentre", stickX + 10f, stickY)
            label(c, outer, 250f, outerRight, "Y", "record", upperX + m * 19f, upperY)
            label(c, outer, 500f, outerRight, "X", "photo", lowerX + m * 15f, lowerY + 15f)
            label(c, outer, 376f, outerRight, "GRIP", "center · clutch", grip.centerX(), grip.centerY())
            label(c, inner, 500f, innerRight, "MENU", "clear view", smallX, smallY)
        } else {
            label(c, outer, 96f, outerRight, "TRIGGER", "press · track", trigger.right - 20f, trigger.centerY())
            label(c, inner, 96f, innerRight, "STICK", "move the camera", stickX, stickY - 34f)
            label(c, inner, 196f, innerRight, "STICK CLICK", "screen in front", stickX - 10f, stickY)
            label(c, outer, 250f, outerRight, "B", "stop tracking", upperX + m * 19f, upperY)
            label(c, outer, 500f, outerRight, "A", "head tracking", lowerX + m * 15f, lowerY + 15f)
            label(c, outer, 376f, outerRight, "GRIP", "point there", grip.centerX(), grip.centerY())
        }
        what.textAlign = Paint.Align.CENTER
        what.textSize = 19f
        c.drawText(if (left) "Grip with Clutch on: hold it to move the camera with your head"
                   else "Point at the screen: the trigger presses a button, or tracks", r.centerX(), r.bottom - 26f, what)
        what.textSize = 23f
    }

    companion object {
        const val WIDTH = 1280
        const val HEIGHT = 640
    }
}
