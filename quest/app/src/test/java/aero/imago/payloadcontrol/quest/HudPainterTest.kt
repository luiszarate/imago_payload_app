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
        override fun streamResolutions(camera: Int): IntArray = when (camera) {
            1 -> intArrayOf(1920, 1080, 1280, 720, 960, 540)
            2 -> intArrayOf(1280, 1024, 640, 512, 480, 384, 320, 256, 160, 128)
            else -> IntArray(0)
        }
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
        s[Hud.YAW_VALID] = 1f; s[Hud.PAN_VALID] = 1f; s[Hud.PAN] = 45f
        s[Hud.HEADING_VALID] = 1f; s[Hud.HEADING] = 300f
        s[Hud.YAW_NORTH_VALID] = 1f; s[Hud.YAW_NORTH] = 345f; s[Hud.HFOV] = 24f
        s[Hud.VEHICLE_ALTITUDE_VALID] = 1f; s[Hud.VEHICLE_ALTITUDE] = 120f
        s[Hud.VEHICLE_SPEED_VALID] = 1f; s[Hud.VEHICLE_GROUNDSPEED] = 22f; s[Hud.VEHICLE_AIRSPEED] = 20f
        s[Hud.VEHICLE_BATTERY_VALID] = 1f; s[Hud.VEHICLE_BATTERY_V] = 22.8f; s[Hud.VEHICLE_BATTERY_PERCENT] = 64f
        s[Hud.LOG_RECORDING] = 1f; s[Hud.LOG_SECONDS] = 754f; s[Hud.LOG_DETAILED] = 1f
        // 19.4326 N, 99.1332 W in the snapshot's two exact halves.
        s[Hud.VEHICLE_POSITION_VALID] = 1f
        s[Hud.VEHICLE_LAT_HI] = Math.floorDiv(194326000L, 65536L).toFloat(); s[Hud.VEHICLE_LAT_LO] = Math.floorMod(194326000L, 65536L).toFloat()
        s[Hud.VEHICLE_LON_HI] = Math.floorDiv(-991332000L, 65536L).toFloat(); s[Hud.VEHICLE_LON_LO] = Math.floorMod(-991332000L, 65536L).toFloat()
        s[Hud.TARGET_VALID] = 1f; s[Hud.TARGET_EAST_M] = -120f; s[Hud.TARGET_NORTH_M] = 180f
        s[Hud.CTL_MAX_SPEED] = 60f; s[Hud.CTL_ZOOM_COMP] = 0.5f; s[Hud.CTL_EXPO] = 0.55f; s[Hud.CTL_FINE_AIM] = 2f
        s[Hud.ZOOM] = 12.5f; s[Hud.VIEW_SOURCE] = 0f
        s[Hud.LRF_VALID] = 1f; s[Hud.LRF_RANGE_M] = 842f
        s[Hud.STORAGE_VALID] = 1f; s[Hud.STORAGE_FREE_MIB] = 52000f
        s[Hud.RECORDING] = 1f; s[Hud.RECORDING_TIME_MS] = 83000f
        s[Hud.HEAD_MODE] = 1f; s[Hud.HEAD_FOLLOWING] = 1f; s[Hud.HEAD_POSE_FRESH] = 1f; s[Hud.HEAD_ERROR] = 1.4f
        s[Hud.HEAD_GAIN] = 0.6f; s[Hud.HEAD_MODE_SETTING] = 2f
        s[Hud.GIMBAL_PROTOCOL] = 2f; s[Hud.GIMBAL_CONTROL] = 1f
        s[Hud.PASSTHROUGH_ACTIVE] = 1f; s[Hud.ROOM_DIM] = 0.35f
        s[Hud.HEAD_SCALE] = 0.08f; s[Hud.HEAD_ZOOM_COMP] = 1f; s[Hud.FOLLOW_HEAD] = 1f
        s[Hud.VIGNETTE] = 1f
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
    fun theAttitudeDialShowsTheCameraAgainstTheAircraft() {
        val backend = FakeBackend()
        connected(backend)
        backend.state[Hud.YAW] = 90f // the camera looks out of the right wing
        val painter = HudPainter(RuntimeEnvironment.getApplication(), null, names, backend, animated = false) {}
        val bitmap = render(painter, "")
        // The dial sits under the camera rail: its centre, as drawAttitude lays it out.
        val vw = bitmap.width / (1 + 2 * Hud.MARGIN_SIDE)
        val vh = bitmap.height / (1 + Hud.MARGIN_TOP + Hud.MARGIN_BOTTOM)
        val top = vh * Hud.MARGIN_TOP + HudPainter.CAMERA_RAIL + 8f
        val radius = minOf(64f, (bitmap.height - 90f - top - 57f) / 2)
        val cx = (vw * Hud.MARGIN_SIDE) / 2 - 8f
        val cy = top + 10f + radius
        fun magenta(x: Float, y: Float): Boolean {
            val p = bitmap.getPixel(x.toInt(), y.toInt())
            return Color.red(p) > 150 && Color.red(p) > Color.green(p) + 60 && Color.blue(p) > Color.green(p) + 20
        }
        assertTrue("the cone points right", magenta(cx + radius * 0.75f, cy))
        assertTrue("and not left", !magenta(cx - radius * 0.75f, cy))
        // The aircraft, nose up, in the middle: light.
        val nose = bitmap.getPixel(cx.toInt(), (cy - radius * 0.2f).toInt())
        assertTrue("the aircraft is drawn", Color.red(nose) > 180 && Color.green(nose) > 180)
    }

    @Test
    fun theClearViewKeepsThePictureTheDialAndTheWarnings() {
        val backend = FakeBackend()
        connected(backend)
        val painter = HudPainter(RuntimeEnvironment.getApplication(), null, names, backend, animated = false) {}
        val w = HudPainter.WIDTH.toFloat()
        val vw = w / (1 + 2 * Hud.MARGIN_SIDE)
        val vh = HudPainter.canvasHeight(16f / 9f) / (1 + Hud.MARGIN_TOP + Hud.MARGIN_BOTTOM)
        val pillX = ((w - 16f - 72f - 10f - 36f) - vw * Hud.MARGIN_SIDE) / vw
        render(painter, "")
        // Its pill in the top bar asks for it; the native side then says it is on.
        backend.state[Hud.CLICK_COUNT] = 1f; backend.state[Hud.CLICK_X] = pillX; backend.state[Hud.CLICK_Y] = -0.05f
        render(painter, "")
        assertEquals(27 to 1f, backend.commands.last().let { it.first to it.second })
        backend.state[Hud.PICTURE_ONLY] = 1f
        backend.state[Hud.PAN_NEAR_LIMIT] = 1f; backend.state[Hud.PAN] = 301f
        val bitmap = render(painter, "clear.png")
        // The gimbal rail is gone: the room's bands show where HEAD was.
        val rail = bitmap.getPixel((vw * Hud.MARGIN_SIDE + vw + 80f).toInt(), (vh * Hud.MARGIN_TOP + 80f).toInt())
        assertTrue("the rail is hidden", rail == Color.rgb(58, 64, 60) || rail == Color.rgb(36, 40, 46))
        // The pan warning is on the picture, amber, at the bottom centre.
        val warning = bitmap.getPixel((w / 2 - 300f).toInt(), (vh * Hud.MARGIN_TOP + vh - 42f).toInt())
        assertTrue("the pan warning shows", Color.red(warning) > 200 && Color.green(warning) > 150 && Color.blue(warning) < 100)
        // The same pill brings the interface back.
        backend.state[Hud.CLICK_COUNT] = 2f
        render(painter, "")
        assertEquals(27 to 0f, backend.commands.last().let { it.first to it.second })
    }

    @Test
    fun theControlsSheetDragsASliderAndSavesItWhenLetGo() {
        val backend = FakeBackend()
        connected(backend)
        var kept: HudPainter.StickSettings? = null
        val painter = HudPainter(RuntimeEnvironment.getApplication(), null, names, backend, animated = false,
            onControls = { _, stick -> kept = stick }) {}
        val w = HudPainter.WIDTH.toFloat()
        val vw = w / (1 + 2 * Hud.MARGIN_SIDE)
        val vh = HudPainter.canvasHeight(16f / 9f) / (1 + Hud.MARGIN_TOP + Hud.MARGIN_BOTTOM)
        fun picture(x: Float, y: Float) = (x - vw * Hud.MARGIN_SIDE) / vw to (y - vh * Hud.MARGIN_TOP) / vh
        render(painter, "")
        // The tune pill: third from the right in the top bar.
        val (px, py) = picture(w - 16f - 72f * 2 - 10f * 2 - 36f, vh * Hud.MARGIN_TOP / 2)
        backend.state[Hud.CLICK_COUNT] = 1f; backend.state[Hud.CLICK_X] = px; backend.state[Hud.CLICK_Y] = py
        render(painter, "")
        render(painter, "controls.png")
        assertEquals(true, backend.sheet)
        // The second slider ("slower with zoom"): press at its left end, drag to three quarters.
        val sheet = 96f + 4 * 96f + 60f + 22f
        val top = vh * Hud.MARGIN_TOP + vh / 2 - sheet / 2
        val left = w / 2 - 410f + 34f
        val right = w / 2 + 410f - 190f
        val rowY = top + 96f + 96f + 54f
        val (sx, sy) = picture(left + 2f, rowY)
        backend.state[Hud.CLICK_COUNT] = 2f; backend.state[Hud.CLICK_X] = sx; backend.state[Hud.CLICK_Y] = sy
        backend.state[Hud.TRIGGER_DOWN] = 1f
        backend.state[Hud.POINTER_VALID] = 1f; backend.state[Hud.POINTER_X] = sx; backend.state[Hud.POINTER_Y] = sy
        render(painter, "")
        val (dx, _) = picture(left + (right - left) * 0.75f, rowY)
        backend.state[Hud.POINTER_X] = dx
        render(painter, "")
        val sent = backend.commands.last()
        assertEquals(30, sent.first)
        assertEquals(1f, sent.second, 0f)          // zoom compensation
        assertEquals(0.75f, sent.third, 0.026f)
        // Let go: the profile gets it, with the other settings as they were.
        backend.state[Hud.CTL_ZOOM_COMP] = sent.third
        backend.state[Hud.TRIGGER_DOWN] = 0f
        render(painter, "")
        val stick = kept!!
        assertEquals(0.75f, stick.zoomCompensation, 0.026f)
        assertEquals(60f, stick.maxSpeedDps, 0f)
        assertEquals(2f, stick.fineAimDps, 0f)
    }

    @Test
    fun theAiButtonShowsWhatWasAskedAndSendsItOnce() {
        val backend = FakeBackend()
        connected(backend)
        val painter = HudPainter(RuntimeEnvironment.getApplication(), null, names, backend, animated = false) {}
        val w = HudPainter.WIDTH.toFloat()
        val vw = w / (1 + 2 * Hud.MARGIN_SIDE)
        val vh = HudPainter.canvasHeight(16f / 9f) / (1 + Hud.MARGIN_TOP + Hud.MARGIN_BOTTOM)
        render(painter, "")
        // AI: the right button of the gimbal rail's last row (with the gimbal manager's row).
        val railTop = vh * Hud.MARGIN_TOP + 10f + 26f * 3 + (HudPainter.BUTTON + 8f) * 3 + 6f * 2 + (52f + 8f) * 2 + 64f + 8f
        val x = (vw * Hud.MARGIN_SIDE + vw + vw * Hud.MARGIN_SIDE * 0.75f - vw * Hud.MARGIN_SIDE) / vw
        val y = (railTop + HudPainter.BUTTON / 2 - vh * Hud.MARGIN_TOP) / vh
        backend.state[Hud.CLICK_COUNT] = 1f; backend.state[Hud.CLICK_X] = x; backend.state[Hud.CLICK_Y] = y
        render(painter, "")
        assertEquals(11 to 1f, backend.commands.last().let { it.first to it.second })
        backend.state[Hud.AI_ON] = 1f // the native side keeps what was asked
        backend.state[Hud.CLICK_COUNT] = 2f
        render(painter, "")
        assertEquals(11 to 0f, backend.commands.last().let { it.first to it.second })
    }

    @Test
    fun theMapShowsTheAircraftOverItsTilesAndCanBePutAway() {
        val backend = FakeBackend()
        connected(backend)
        // A tile source that paints each tile by its column's parity: blue or green.
        val asked = HashSet<TileId>()
        val blue = Bitmap.createBitmap(16, 16, Bitmap.Config.ARGB_8888).apply { eraseColor(Color.rgb(20, 40, 160)) }
        val green = Bitmap.createBitmap(16, 16, Bitmap.Config.ARGB_8888).apply { eraseColor(Color.rgb(20, 120, 50)) }
        var kept: MapSettings? = null
        val painter = HudPainter(RuntimeEnvironment.getApplication(), null, names, backend, animated = false,
            tiles = { style, id -> assertEquals("satellite", style); asked += id; if (id.x % 2 == 0) blue else green },
            mapSettings = MapSettings(zoom = 16), onMap = { kept = it }) {}
        val bitmap = render(painter, "map.png")
        val w = HudPainter.WIDTH.toFloat()
        val vw = w / (1 + 2 * Hud.MARGIN_SIDE)
        val vh = bitmap.height / (1 + Hud.MARGIN_TOP + Hud.MARGIN_BOTTOM)
        val right = vw * Hud.MARGIN_SIDE + vw - 16f
        val bottom = vh * Hud.MARGIN_TOP + vh - 16f
        val cx = right - HudPainter.MAP_SIZE / 2
        val cy = bottom - HudPainter.MAP_SIZE / 2
        // The tiles around Mexico City at zoom 16 were asked for, the aircraft's own among them.
        assertTrue(TileId(16, TileMath.x(-99.1332, 16).toInt(), TileMath.y(19.4326, 16).toInt()) in asked)
        // Map, not picture, near its lower left corner (blue or green tile, a little translucent).
        val corner = bitmap.getPixel((right - HudPainter.MAP_SIZE + 40f).toInt(), (bottom - 60f).toInt())
        assertTrue("a tile shows", Color.blue(corner) > 110 || Color.green(corner) > 100)
        assertTrue(Color.red(corner) < 60)
        // The aircraft, light, in the middle.
        val middle = bitmap.getPixel(cx.toInt(), cy.toInt())
        assertTrue("the aircraft is drawn", Color.red(middle) > 170 && Color.green(middle) > 170)
        assertEquals(19.4326, painter.lastPosition!!.first, 1e-6)
        assertEquals(-99.1332, painter.lastPosition!!.second, 1e-6)

        // Pointed at, its buttons appear; the rightmost puts it away, and that is kept.
        fun picture(x: Float, y: Float) = (x - vw * Hud.MARGIN_SIDE) / vw to (y - vh * Hud.MARGIN_TOP) / vh
        val (bx, by) = picture(right - 8f - 23f, bottom - HudPainter.MAP_SIZE + 31f)
        backend.state[Hud.POINTER_VALID] = 1f; backend.state[Hud.POINTER_X] = bx; backend.state[Hud.POINTER_Y] = by
        backend.state[Hud.CLICK_COUNT] = 1f; backend.state[Hud.CLICK_X] = bx; backend.state[Hud.CLICK_Y] = by
        render(painter, "")
        assertEquals(false, kept!!.visible)
        assertTrue("a press on the map is not a track", backend.commands.none { it.first == 1 })
        // Away: the picture shows through where the map was.
        backend.state[Hud.POINTER_VALID] = 0f
        val away = render(painter, "")
        assertEquals(Color.rgb(96, 104, 112), away.getPixel((right - HudPainter.MAP_SIZE + 40f).toInt(), (bottom - 200f).toInt()))
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

    /** Presses the trigger at the middle of a control, as the native side reports it. */
    private fun press(backend: FakeBackend, painter: HudPainter, at: android.graphics.RectF) {
        val vw = HudPainter.WIDTH / (1 + 2 * Hud.MARGIN_SIDE)
        val vh = HudPainter.canvasHeight(16f / 9f) / (1 + Hud.MARGIN_TOP + Hud.MARGIN_BOTTOM)
        backend.state[Hud.CLICK_COUNT] += 1f
        backend.state[Hud.CLICK_X] = (at.centerX() - vw * Hud.MARGIN_SIDE) / vw
        backend.state[Hud.CLICK_Y] = (at.centerY() - vh * Hud.MARGIN_TOP) / vh
        render(painter, "")
    }

    @Test
    fun theIsoButtonOfTheThermalBarSwitchesTheIsotherm() {
        val backend = FakeBackend()
        connected(backend)
        backend.state[Hud.VIEW_SOURCE] = 2f // IR in view: the thermal bar is there
        val painter = HudPainter(RuntimeEnvironment.getApplication(), null, names, backend, animated = false) {}
        render(painter, "")
        // The payload's web page was not read (no payload address, or no answer): no button.
        assertTrue(painter.isoButton.isEmpty)

        backend.state[Hud.ISO_VALID] = 1f; backend.state[Hud.ISO_THRESHOLD_C] = 30f
        render(painter, "")
        assertTrue(!painter.isoButton.isEmpty)
        press(backend, painter, painter.isoButton)
        assertEquals(31 to 1f, backend.commands.last().let { it.first to it.second })

        backend.state[Hud.ISO_ON] = 1f // the page now says so
        render(painter, "thermal-bar.png")
        press(backend, painter, painter.isoButton)
        assertEquals(31 to 0f, backend.commands.last().let { it.first to it.second })
    }

    @Test
    fun theCameraSheetSetsTheRangeFinderAndTheStream() {
        val backend = FakeBackend()
        connected(backend)
        backend.state[Hud.LRF_MODE] = 1f
        backend.state[Hud.STREAM_LEVEL_EO] = -1f; backend.state[Hud.STREAM_LEVEL_IR] = -1f
        val painter = HudPainter(RuntimeEnvironment.getApplication(), null, names, backend, animated = false) {}
        render(painter, "")
        press(backend, painter, painter.cameraButton)
        render(painter, "camera.png")
        assertEquals(true, backend.sheet)
        assertEquals(4, painter.lrfButtons.size)
        assertEquals(3, painter.resolutionButtons.size) // the EO stream's sizes

        press(backend, painter, painter.lrfButtons[2]) // 10 Hz
        assertEquals(Triple(32, 2f, 0f), backend.commands.last())
        press(backend, painter, painter.bitrateButtons[1]) // 2 Mb/s, EO
        assertEquals(Triple(33, 1f, 2000f), backend.commands.last())
        press(backend, painter, painter.resolutionButtons[1]) // 1280 x 720
        assertEquals(Triple(34, 1f, 1f), backend.commands.last())

        // The IR camera's stream has its own sizes; choosing the camera sends nothing.
        val before = backend.commands.size
        press(backend, painter, painter.streamCameraButtons[1])
        assertEquals(before, backend.commands.size)
        render(painter, "")
        assertEquals(5, painter.resolutionButtons.size)
        press(backend, painter, painter.resolutionButtons[4]) // 160 x 128
        assertEquals(Triple(34, 2f, 4f), backend.commands.last())
        // A press on the sheet is never a track on the picture.
        assertTrue(backend.commands.none { it.first == 1 })
    }

    @Test
    fun theLogButtonOfTheTopBarStopsAndStartsTheLog() {
        val backend = FakeBackend()
        connected(backend) // the log is running
        val painter = HudPainter(RuntimeEnvironment.getApplication(), null, names, backend, animated = false) {}
        render(painter, "")
        val vw = HudPainter.WIDTH / (1 + 2 * Hud.MARGIN_SIDE)
        val vh = HudPainter.canvasHeight(16f / 9f) / (1 + Hud.MARGIN_TOP + Hud.MARGIN_BOTTOM)
        fun press(count: Float) {
            backend.state[Hud.CLICK_COUNT] = count
            backend.state[Hud.CLICK_X] = (painter.logButton.centerX() - vw * Hud.MARGIN_SIDE) / vw
            backend.state[Hud.CLICK_Y] = (painter.logButton.centerY() - vh * Hud.MARGIN_TOP) / vh
            render(painter, "")
        }
        press(1f)
        assertEquals(28 to 0f, backend.commands.last().let { it.first to it.second })
        backend.state[Hud.LOG_RECORDING] = 0f // the native side stopped it
        render(painter, "") // the button is narrower now
        press(2f)
        assertEquals(28 to 1f, backend.commands.last().let { it.first to it.second })
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
        // The room pill is the sixth from the right in the top bar (exit, clear view, controls,
        // camera and placing the screen come first).
        val w = HudPainter.WIDTH.toFloat()
        val videoWidth = w / (1 + 2 * Hud.MARGIN_SIDE)
        val x = ((w - 16f - 72f * 5 - 10f * 5 - 36f) - videoWidth * Hud.MARGIN_SIDE) / videoWidth
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
    fun theScreenIsHeldByItsHandlesWhileTheTriggerIsDown() {
        val backend = FakeBackend()
        connected(backend)
        val painter = HudPainter(RuntimeEnvironment.getApplication(), null, names, backend, animated = false) {}
        render(painter, "")
        val w = HudPainter.WIDTH.toFloat()
        val h = HudPainter.canvasHeight(16f / 9f).toFloat()
        val vw = w / (1 + 2 * Hud.MARGIN_SIDE)
        val vh = h / (1 + Hud.MARGIN_TOP + Hud.MARGIN_BOTTOM)
        fun picture(x: Float, y: Float) = (x - vw * Hud.MARGIN_SIDE) / vw to (y - vh * Hud.MARGIN_TOP) / vh
        // The bottom right corner: resize while the trigger is held, let go on release.
        val (cx, cy) = picture(w - 40f, h - 40f)
        backend.state[Hud.CLICK_COUNT] = 1f; backend.state[Hud.CLICK_X] = cx; backend.state[Hud.CLICK_Y] = cy
        backend.state[Hud.TRIGGER_DOWN] = 1f
        render(painter, "")
        assertEquals(Triple(26, 2f, 0f), backend.commands.last())
        render(painter, "")
        assertEquals(1, backend.commands.count { it.first == 26 }) // held: nothing more
        backend.state[Hud.TRIGGER_DOWN] = 0f
        render(painter, "")
        assertEquals(Triple(26, 0f, 0f), backend.commands.last())
        // The move bar beside it.
        val (mx, my) = picture(w - 76f - 6f - 72f, h - 34f)
        backend.state[Hud.CLICK_COUNT] = 2f; backend.state[Hud.CLICK_X] = mx; backend.state[Hud.CLICK_Y] = my
        backend.state[Hud.TRIGGER_DOWN] = 1f
        render(painter, "")
        assertEquals(Triple(26, 1f, 0f), backend.commands.last())
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
        // Clutch on and not pressed, in a dark room: both said in the bottom bar.
        backend.state[Hud.HEAD_FAULT] = 0f; backend.state[Hud.VIDEO_ACTIVE] = 1f
        backend.state[Hud.CLUTCH] = 1f; backend.state[Hud.HEAD_CLUTCHED] = 1f; backend.state[Hud.ROOM_DIM] = 1f
        render(painter, "clutch-dark.png")
    }
}
