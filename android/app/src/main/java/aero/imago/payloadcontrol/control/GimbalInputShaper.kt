package aero.imago.payloadcontrol.control

import aero.imago.payloadcontrol.core.AppJson
import android.content.SharedPreferences
import androidx.core.content.edit
import kotlinx.serialization.Serializable
import kotlin.math.abs
import kotlin.math.min
import kotlin.math.pow

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
    /**
     * How much the rate shrinks as the camera zooms in: rate × zoom^-compensation. 0 keeps the
     * same °/s at any zoom, 1 keeps the picture moving at the same pace on screen, 0.5 (√zoom)
     * is in between. Operators found 0.5 too slow at full zoom to follow a target (2026-10-28).
     */
    val zoomCompensation: Float = 0.5f,
    /**
     * Fine aim: slow stick moves become angle setpoints in the core (imago_gimbal_set_fine_aim).
     * The Orus L ignores rates under about 2 deg/s, which left a dead stretch of stick when
     * zoomed in, and follows slow angles smoothly (ground tests 2026-11-09).
     */
    val fineAim: Boolean = true,
) {
    /** What the core's fine aim gets: the floor, or 0 when off. */
    val fineAimFloor: Float get() = if (fineAim) FINE_AIM_FLOOR_DPS else 0f

    data class Rates(val pitch: Float, val yaw: Float)

    /**
     * x: right positive, y: up positive, both in -1..1. zoom: current EO zoom (x).
     * digital: the input is a D-pad or button (on or off) rather than a stick.
     */
    fun rates(x: Float, y: Float, zoom: Float, digitalX: Boolean = false, digitalY: Boolean = false): Rates {
        val zoomFactor = maxOf(1f, zoom).pow(-zoomCompensation)
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
        val zoomCompensationRange = 0f..1f
        /** The slowest rate the Orus L obeys (2026-11-09); fine aim takes over under it. */
        const val FINE_AIM_FLOOR_DPS = 2f
        private const val KEY = "controls.gimbalShaper"

        fun load(prefs: SharedPreferences): GimbalInputShaper =
            prefs.getString(KEY, null)?.let { runCatching { AppJson.decodeFromString<GimbalInputShaper>(it) }.getOrNull() }
                ?: GimbalInputShaper()
    }
}
