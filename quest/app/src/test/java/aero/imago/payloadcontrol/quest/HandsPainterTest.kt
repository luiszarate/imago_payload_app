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

@RunWith(RobolectricTestRunner::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = [34])
class HandsPainterTest {
    @Test
    fun drawsACardForEachController() {
        val bitmap = Bitmap.createBitmap(HandsPainter.WIDTH, HandsPainter.HEIGHT, Bitmap.Config.ARGB_8888)
        HandsPainter(RuntimeEnvironment.getApplication()).paintOn(Canvas(bitmap))
        val dir = File("build/outputs/hud").apply { mkdirs() }
        FileOutputStream(File(dir, "hands.png")).use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
        // Clear outside the cards (the corners), a card in each half, and something on each.
        assertEquals(0, Color.alpha(bitmap.getPixel(2, 2)))
        assertEquals(0, Color.alpha(bitmap.getPixel(HandsPainter.WIDTH / 2, 2)))
        for (half in 0..1) {
            val x0 = half * HandsPainter.WIDTH / 2
            assertTrue(Color.alpha(bitmap.getPixel(x0 + 60, 320)) > 200)
            var light = 0
            for (x in x0 + 40 until x0 + 600 step 3) for (y in 60 until 580 step 3) {
                if (Color.red(bitmap.getPixel(x, y)) > 200) light++
            }
            assertTrue("labels are drawn on card $half: $light", light > 300)
        }
    }
}
