package aero.imago.payloadcontrol.quest

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode
import java.io.File
import java.io.FileOutputStream

/**
 * Draws the immersive interface off the headset and saves it as PNG files under
 * build/outputs/hud/ (CI publishes them), over a grey stand-in for the picture. Also checks
 * that the pointer and the trigger reach the buttons.
 */
@RunWith(RobolectricTestRunner::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = [34])
class HudPainterTest {
    private class FakeBackend : HudPainter.Backend {
        val state = FloatArray(Hud.SIZE)
        val commands = mutableListOf<Triple<Int, Float, Float>>()
        override fun snapshot(out: FloatArray): Boolean {
            state.copyInto(out)
            return true
        }
        override fun command(command: Int, a: Float, b: Float) {
            commands += Triple(command, a, b)
        }
        override fun paletteName(cameraType: Int, index: Int) = "Iron red"
        var sheet = false
        override fun sheetOpen(open: Boolean) {
            sheet = open
        }
    }

    private val names = listOf("Herelink (forwarded)", "Orus L direct", "orus-sim on this network")

    private fun connected(b: FakeBackend) {
        val s = b.state
        s[Hud.LINK_UP] = 1f; s[Hud.CONNECTED] = 1f; s[Hud.VIDEO_ACTIVE] = 1f
        s[Hud.FPS] = 30f; s[Hud.BITRATE_KBPS] = 4100f
        s[Hud.GIMBAL_VALID] = 1f; s[Hud.PITCH] = -12.3f; s[Hud.YAW] = 45f; s[Hud.GIMBAL_MODE] = 2f
        s[Hud.ZOOM] = 12.5f; s[Hud.VIEW_SOURCE] = 0f
        s[Hud.LRF_VALID] = 1f; s[Hud.LRF_RANGE_M] = 842f
        s[Hud.STORAGE_VALID] = 1f; s[Hud.STORAGE_FREE_MIB] = 52000f
        s[Hud.RECORDING] = 1f; s[Hud.RECORDING_TIME_MS] = 83000f
        s[Hud.HEAD_MODE] = 1f; s[Hud.HEAD_FOLLOWING] = 1f; s[Hud.HEAD_POSE_FRESH] = 1f; s[Hud.HEAD_ERROR] = 1.4f
        s[Hud.HEAD_GAIN] = 0.6f; s[Hud.HEAD_MODE_SETTING] = 2f
        s[Hud.GIMBAL_PROTOCOL] = 2f; s[Hud.GIMBAL_CONTROL] = 1f
        s[Hud.PASSTHROUGH_ACTIVE] = 1f; s[Hud.ROOM_DIM] = 0.35f
        s[Hud.HEAD_SCALE] = 0.08f; s[Hud.HEAD_ZOOM_COMP] = 1f; s[Hud.FOLLOW_HEAD] = 1f
        s[Hud.TRACKING_STATE] = 1f; s[Hud.TRACKING_MODE] = 1f
        s[Hud.BOX_VALID] = 1f; s[Hud.BOX_X] = 0.42f; s[Hud.BOX_Y] = 0.4f; s[Hud.BOX_W] = 0.1f; s[Hud.BOX_H] = 0.14f
        s[Hud.DETECTION_COUNT] = 1f
        s[Hud.DETECTIONS] = 0.7f; s[Hud.DETECTIONS + 1] = 0.55f; s[Hud.DETECTIONS + 2] = 0.08f
        s[Hud.DETECTIONS + 3] = 0.1f; s[Hud.DETECTIONS + 4] = 2f; s[Hud.DETECTIONS + 5] = 0.87f
        s[Hud.IR_PALETTE] = 3f; s[Hud.LATENCY_PRESENT_MS] = 14f
    }

    private fun render(painter: HudPainter, file: String): Bitmap {
        val w = HudPainter.WIDTH
        val h = HudPainter.canvasHeight(16f / 9f)
        val bitmap = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        // The room (dark) and a grey stand-in for the picture, where the video quad shows.
        // A stand-in for the darkened room behind the glass: bands, so the translucency shows.
        canvas.drawColor(Color.rgb(24, 26, 30))
        val room = android.graphics.Paint()
        for (k in 0 until 12) {
            room.color = if (k % 2 == 0) Color.rgb(58, 64, 60) else Color.rgb(36, 40, 46)
            canvas.drawRect(k * w / 12f, 0f, (k + 1) * w / 12f, h.toFloat(), room)
        }
        val vw = w / (1 + 2 * Hud.MARGIN_SIDE)
        val vh = h / (1 + Hud.MARGIN_TOP + Hud.MARGIN_BOTTOM)
        val paint = android.graphics.Paint().apply { color = Color.rgb(96, 104, 112) }
        canvas.drawRect(vw * Hud.MARGIN_SIDE, vh * Hud.MARGIN_TOP, vw * Hud.MARGIN_SIDE + vw, vh * Hud.MARGIN_TOP + vh, paint)
        painter.paintOn(canvas)
        if (file.isNotEmpty()) {
            val dir = File("build/outputs/hud").apply { mkdirs() }
            FileOutputStream(File(dir, file)).use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
        }
        return bitmap
    }

    @Test
    fun drawsTheOperatorInterface() {
        val backend = FakeBackend()
        connected(backend)
        backend.state[Hud.POINTER_VALID] = 1f; backend.state[Hud.POINTER_X] = 0.6f; backend.state[Hud.POINTER_Y] = 0.3f
        val painter = HudPainter(RuntimeEnvironment.getApplication(), null, names, backend, animated = false) {}
        painter.notice("Photo taken")
        val bitmap = render(painter, "operator.png")
        // Something was drawn on the left rail (not the plain background).
        assertTrue(bitmap.getPixel(60, 200) != Color.rgb(58, 64, 60))
    }

    @Test
    fun aClickOnTheProfilePillOpensConnectionsAndAProfileConnects() {
        val backend = FakeBackend()
        connected(backend)
        var exited = false
        val painter = HudPainter(RuntimeEnvironment.getApplication(), null, names, backend, animated = false) { exited = true }
        render(painter, "") // first frame: the click counter's baseline
        // The pill sits right of the wordmark, in the top bar: y < 0 in picture units.
        backend.state[Hud.CLICK_COUNT] = 1f; backend.state[Hud.CLICK_X] = 0.02f; backend.state[Hud.CLICK_Y] = -0.05f
        render(painter, "")
        render(painter, "connections.png") // the sheet is open now
        assertEquals(true, backend.sheet)
        // Second profile row of the sheet: centre of the picture, a little above the middle.
        backend.state[Hud.CLICK_COUNT] = 2f; backend.state[Hud.CLICK_X] = 0.5f; backend.state[Hud.CLICK_Y] = 0.31f
        render(painter, "")
        assertTrue(backend.commands.toString(), backend.commands.any { it.first == 14 })
        render(painter, "") // the frame after the press
        assertEquals(false, backend.sheet) // closed again: its layer went back
        assertEquals(false, exited)
    }

    @Test
    fun aClickOnThePictureTracksThereAndHeldZoomFollowsTheTrigger() {
        val backend = FakeBackend()
        connected(backend)
        val painter = HudPainter(RuntimeEnvironment.getApplication(), null, names, backend, animated = false) {}
        render(painter, "")
        backend.state[Hud.CLICK_COUNT] = 1f; backend.state[Hud.CLICK_X] = 0.25f; backend.state[Hud.CLICK_Y] = 0.75f
        render(painter, "")
        val track = backend.commands.single { it.first == 1 }
        assertEquals(0.25f, track.second, 0.01f)
        assertEquals(0.75f, track.third, 0.01f)
    }

    @Test
    fun theRoomButtonStepsFromDimmedToDarkToClear() {
        val backend = FakeBackend()
        connected(backend)
        val painter = HudPainter(RuntimeEnvironment.getApplication(), null, names, backend, animated = false) {}
        render(painter, "")
        // The room pill is the fourth from the right in the top bar.
        val w = HudPainter.WIDTH.toFloat()
        val videoWidth = w / (1 + 2 * Hud.MARGIN_SIDE)
        val x = ((w - 16f - 72f * 3 - 10f * 3 - 36f) - videoWidth * Hud.MARGIN_SIDE) / videoWidth
        var clicks = 0f
        fun press() {
            clicks += 1f
            backend.state[Hud.CLICK_COUNT] = clicks; backend.state[Hud.CLICK_X] = x; backend.state[Hud.CLICK_Y] = -0.05f
            render(painter, "")
            backend.state[Hud.ROOM_DIM] = backend.commands.last { it.first == 21 }.second // the native side applies it
        }
        press()
        assertEquals(1f, backend.state[Hud.ROOM_DIM], 0f)      // dimmed -> dark
        press()
        assertEquals(0f, backend.state[Hud.ROOM_DIM], 0f)      // dark -> clear
        press()
        assertEquals(0.35f, backend.state[Hud.ROOM_DIM], 0.001f) // clear -> dimmed, at the level it had
        // Without passthrough there is no button: the same press does nothing.
        backend.state[Hud.PASSTHROUGH_ACTIVE] = 0f
        val before = backend.commands.size
        clicks += 1f
        backend.state[Hud.CLICK_COUNT] = clicks
        render(painter, "")
        assertEquals(before, backend.commands.size)
    }

    @Test
    fun disconnectedAndFaultStates() {
        val backend = FakeBackend()
        val painter = HudPainter(RuntimeEnvironment.getApplication(), null, names, backend, animated = false) {}
        render(painter, "disconnected.png")
        connected(backend)
        backend.state[Hud.VIEW_SOURCE] = 1f
        backend.state[Hud.HEAD_FAULT] = 2f; backend.state[Hud.HEAD_FOLLOWING] = 0f
        backend.state[Hud.VIDEO_ACTIVE] = 0f; backend.state[Hud.RECORDING] = 0f
        render(painter, "fault.png")
    }
}
