package aero.imago.payloadcontrol.control

import aero.imago.payloadcontrol.core.AppJson
import android.content.SharedPreferences
import androidx.core.content.edit
import kotlinx.serialization.Serializable
import kotlin.math.abs
import kotlin.math.min
import kotlin.math.sqrt

/**
 * Turns a stick deflection into gimbal rates. Shared by the on-screen joystick and gamepads so
 * both feel the same. Every field is a user setting (Settings › Gimbal), saved across launches.
 */
@Serializable
data class GimbalInputShaper(
    /** Rate at full deflection with no zoom, degrees per second, for pan (yaw) and tilt (pitch). */
    val maxYawDps: Float = 60f,
    val maxPitchDps: Float = 60f,
    /** Ignore deflections below this, so a resting thumb does not drift the gimbal. */
    val deadzone: Float = 0.08f,
    /** 0 = linear, 1 = fully cubic. Higher values give finer control near the centre. */
    val expo: Float = 0.55f,
    /** Rate of a D-pad or button: always fully "pressed", so it gets its own, usually lower, speed. */
    val digitalYawDps: Float = 20f,
    val digitalPitchDps: Float = 20f,
) {
    data class Rates(val pitch: Float, val yaw: Float)

    /**
     * x: right positive, y: up positive, both in -1..1. zoom: current EO zoom (x).
     * digital: the input is a D-pad or button (on or off) rather than a stick.
     * The rate shrinks with zoom so the image moves at a similar speed on screen.
     */
    fun rates(x: Float, y: Float, zoom: Float, digitalX: Boolean = false, digitalY: Boolean = false): Rates {
        val zoomFactor = 1 / sqrt(maxOf(1f, zoom))
        val yaw = if (digitalX) sign(x) * digitalYawDps else shape(x) * maxYawDps
        val pitch = if (digitalY) sign(y) * digitalPitchDps else shape(y) * maxPitchDps
        return Rates(pitch * zoomFactor, yaw * zoomFactor)
    }

    fun shape(value: Float): Float {
        val magnitude = min(abs(value), 1f)
        if (magnitude <= deadzone) return 0f
        val normalized = (magnitude - deadzone) / (1 - deadzone)
        val curved = (1 - expo) * normalized + expo * normalized * normalized * normalized
        return if (value < 0) -curved else curved
    }

    /** A digital input: -1, 0 or 1 (a half-pressed analog button counts once past half way). */
    private fun sign(value: Float) = if (abs(value) < 0.5f) 0f else if (value < 0) -1f else 1f

    fun save(prefs: SharedPreferences) {
        prefs.edit { putString(KEY, AppJson.encodeToString(this@GimbalInputShaper)) }
    }

    companion object {
        val speedRange = 2f..120f
        val deadzoneRange = 0f..0.3f
        private const val KEY = "controls.gimbalShaper"

        fun load(prefs: SharedPreferences): GimbalInputShaper =
            prefs.getString(KEY, null)?.let { runCatching { AppJson.decodeFromString<GimbalInputShaper>(it) }.getOrNull() }
                ?: GimbalInputShaper()
    }
}
