package aero.imago.payloadcontrol.quest

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp

/**
 * Imago Aerospace visual identity, the same values as the tablet app's Theme.kt
 * (android/…/ui/Theme.kt): near-black background, magenta-to-purple accent, Space Grotesk
 * for display text and figures, Manrope for body text. The immersive interface (HudPainter)
 * takes its colours and fonts from here too, so both modes look like one app.
 */
object Theme {
    val background = Color(0xFF05060D)
    val surface = Color(0xFF0A0B10)
    val surfaceRaised = Color(0xFF14151C)
    val text = Color(0xFFF5F6F8)
    val textSecondary = text.copy(alpha = 0.62f)
    val textTertiary = text.copy(alpha = 0.38f)
    val stroke = Color.White.copy(alpha = 0.10f)

    val magenta = Color(0xFFEE49A7)
    val fuchsia = Color(0xFFC2188F)
    val purple = Color(0xFF7E2D91)
    val accent = Brush.linearGradient(listOf(magenta, fuchsia, purple), start = Offset.Zero, end = Offset.Infinite)

    val danger = Color(0xFFEF4444)
    val ok = Color(0xFF34D399)
    val warning = Color(0xFFFBBF24)
}

object AppFont {
    val spaceGrotesk = FontFamily(
        Font(R.font.space_grotesk_500, FontWeight.Medium),
        Font(R.font.space_grotesk_600, FontWeight.SemiBold),
        Font(R.font.space_grotesk_700, FontWeight.Bold),
    )
    val manrope = FontFamily(
        Font(R.font.manrope_400, FontWeight.Normal),
        Font(R.font.manrope_500, FontWeight.Medium),
        Font(R.font.manrope_600, FontWeight.SemiBold),
        Font(R.font.manrope_700, FontWeight.Bold),
    )

    /** Space Grotesk: titles, labels, numbers. */
    fun display(size: Int, weight: Int = 600, color: Color = Theme.text) =
        TextStyle(fontFamily = spaceGrotesk, fontWeight = FontWeight(weight), fontSize = size.sp, color = color)

    /** Manrope: body text. */
    fun body(size: Int, weight: Int = 500, color: Color = Theme.text) =
        TextStyle(fontFamily = manrope, fontWeight = FontWeight(weight), fontSize = size.sp, color = color)
}

@Composable
fun ImagoTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = darkColorScheme(
            primary = Theme.magenta,
            onPrimary = Theme.text,
            secondary = Theme.fuchsia,
            background = Theme.background,
            onBackground = Theme.text,
            surface = Theme.surface,
            onSurface = Theme.text,
            surfaceVariant = Theme.surfaceRaised,
            onSurfaceVariant = Theme.textSecondary,
            surfaceContainer = Theme.surfaceRaised,
            surfaceContainerHigh = Theme.surfaceRaised,
            outline = Theme.stroke,
            error = Theme.danger,
        ),
        content = content,
    )
}
