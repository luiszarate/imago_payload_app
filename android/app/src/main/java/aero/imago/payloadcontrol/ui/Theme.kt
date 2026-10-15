package aero.imago.payloadcontrol.ui

import aero.imago.payloadcontrol.R
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/**
 * Imago Aerospace visual identity (www.imago.aero): near-black background, magenta-to-purple
 * accent, Space Grotesk for display text and figures, Manrope for body text.
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
    val accent = Brush.linearGradient(listOf(magenta, fuchsia, purple),
                                      start = Offset.Zero, end = Offset.Infinite)

    // Status colours. Red is reserved for recording and errors.
    val danger = Color(0xFFEF4444)
    val ok = Color(0xFF34D399)
    val warning = Color(0xFFFBBF24)

    /** Colours for detection classes: distinct hues that stay readable on video. */
    val classColors = listOf(
        Color(0xFF38BDF8), Color(0xFFFBBF24), Color(0xFF34D399), Color(0xFFF472B6),
        Color(0xFFA78BFA), Color(0xFFFB923C), Color(0xFF22D3EE), Color(0xFFA3E635),
    )
}

/**
 * The fonts as static instances of the variable fonts (Android 7 cannot set the weight axis).
 * Weights in between fall back to the nearest instance.
 */
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
    fun display(size: Int, weight: Int = 600, color: Color = Theme.text, tracking: TextUnit = TextUnit.Unspecified) =
        TextStyle(fontFamily = spaceGrotesk, fontWeight = FontWeight(weight), fontSize = size.sp,
                  color = color, letterSpacing = tracking)

    /** Manrope: body text. */
    fun body(size: Int, weight: Int = 500, color: Color = Theme.text) =
        TextStyle(fontFamily = manrope, fontWeight = FontWeight(weight), fontSize = size.sp, color = color)

    /** Space Grotesk with tabular figures, for telemetry that changes while you read it. */
    fun figures(size: Int, weight: Int = 500, color: Color = Theme.text) =
        display(size, weight, color).copy(fontFeatureSettings = "tnum", textAlign = TextAlign.Start)
}

/** Phone in landscape: smaller controls, the zoom beside the capture column. */
val LocalCompact = staticCompositionLocalOf { false }

/** Translucent panel used for every control group over the video. */
fun Modifier.panel(cornerRadius: Dp = 18.dp): Modifier {
    val shape = RoundedCornerShape(cornerRadius)
    return this
        .background(Theme.surface.copy(alpha = 0.78f), shape)
        .border(1.dp, Theme.stroke, shape)
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
