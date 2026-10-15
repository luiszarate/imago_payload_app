package aero.imago.payloadcontrol.control

import aero.imago.payloadcontrol.core.AppJson
import android.content.SharedPreferences
import androidx.core.content.edit
import android.view.KeyEvent
import android.view.MotionEvent
import kotlinx.serialization.Serializable

/** What a controller button does. */
@Serializable
enum class GamepadAction(val title: String) {
    SHUTTER("Shutter (photo or record)"),
    RECORD("Start / stop recording"),
    STOP_TRACKING("Stop tracking"),
    TOGGLE_DETECTION("AI detection on / off"),
    PREVIOUS_VIEW("Previous view"),
    NEXT_VIEW("Next view"),
    RECENTER("Center gimbal"),
    TOGGLE_POINT_MODE("Point mode on / off"),
    TOGGLE_LOCK_FOLLOW("Gimbal lock / follow"),
    ZOOM_IN("Zoom in (hold)"),
    ZOOM_OUT("Zoom out (hold)");

    /** Acts while the button is held instead of once per press. */
    val isHeld get() = this == ZOOM_IN || this == ZOOM_OUT
}

/** What a controller axis drives. */
@Serializable
enum class GamepadAxisFunction(val title: String) {
    GIMBAL_YAW("Gimbal pan (yaw)"),
    GIMBAL_PITCH("Gimbal tilt (pitch)"),
    ZOOM("Zoom"),
}

/**
 * One analog input by its MotionEvent axis (AXIS_X, AXIS_RZ, AXIS_HAT_Y, AXIS_THROTTLE...).
 * Android reports stick Y axes positive down, so "up" bindings are inverted.
 */
@Serializable
data class GamepadAxisBinding(val axis: Int, val inverted: Boolean = false) {
    val label: String get() = axisLabel(axis)
}

/** An input the controller is waiting for while the user assigns it. */
sealed interface GamepadLearnTarget {
    data class Action(val action: GamepadAction) : GamepadLearnTarget
    data class Axis(val function: GamepadAxisFunction) : GamepadLearnTarget
}

/** Which controller inputs drive what. One mapping for the whole app, saved across launches. */
@Serializable
data class GamepadMapping(
    val axes: Map<GamepadAxisFunction, GamepadAxisBinding>,
    /** Button key code -> action. */
    val buttons: Map<Int, GamepadAction>,
) {
    fun button(action: GamepadAction): Int? = buttons.entries.firstOrNull { it.value == action }?.key

    /** One button per action and one action per button. */
    fun bind(action: GamepadAction, keyCode: Int) =
        copy(buttons = buttons.filterValues { it != action } + (keyCode to action))

    fun unbind(action: GamepadAction) = copy(buttons = buttons.filterValues { it != action })

    /** One function per analog input. A stick's Y axis starts inverted, so up means up. */
    fun bind(function: GamepadAxisFunction, axis: Int, inverted: Boolean) =
        copy(axes = axes.filterValues { it.axis != axis } + (function to GamepadAxisBinding(axis, inverted)))

    fun unbind(function: GamepadAxisFunction) = copy(axes = axes - function)

    fun withInverted(function: GamepadAxisFunction, inverted: Boolean): GamepadMapping {
        val binding = axes[function] ?: return this
        return copy(axes = axes + (function to binding.copy(inverted = inverted)))
    }

    fun save(prefs: SharedPreferences) {
        prefs.edit { putString(KEY, AppJson.encodeToString(this@GamepadMapping)) }
    }

    companion object {
        private const val KEY = "controls.gamepadMapping"

        /**
         * Left stick gimbal, right stick zoom, face buttons for the camera, shoulders for the
         * view, Start to center the gimbal.
         */
        val standard = GamepadMapping(
            axes = mapOf(
                GamepadAxisFunction.GIMBAL_YAW to GamepadAxisBinding(MotionEvent.AXIS_X),
                GamepadAxisFunction.GIMBAL_PITCH to GamepadAxisBinding(MotionEvent.AXIS_Y, inverted = true),
                GamepadAxisFunction.ZOOM to GamepadAxisBinding(MotionEvent.AXIS_RZ, inverted = true),
            ),
            buttons = mapOf(
                KeyEvent.KEYCODE_BUTTON_A to GamepadAction.SHUTTER,
                KeyEvent.KEYCODE_BUTTON_B to GamepadAction.RECORD,
                KeyEvent.KEYCODE_BUTTON_X to GamepadAction.STOP_TRACKING,
                KeyEvent.KEYCODE_BUTTON_Y to GamepadAction.TOGGLE_DETECTION,
                KeyEvent.KEYCODE_BUTTON_L1 to GamepadAction.PREVIOUS_VIEW,
                KeyEvent.KEYCODE_BUTTON_R1 to GamepadAction.NEXT_VIEW,
                KeyEvent.KEYCODE_BUTTON_START to GamepadAction.RECENTER,
            ),
        )

        fun load(prefs: SharedPreferences): GamepadMapping =
            prefs.getString(KEY, null)?.let { runCatching { AppJson.decodeFromString<GamepadMapping>(it) }.getOrNull() }
                ?: standard

        /** Hats (D-pads reported as axes) are on or off. */
        fun isDigitalAxis(axis: Int) = axis == MotionEvent.AXIS_HAT_X || axis == MotionEvent.AXIS_HAT_Y

        /** Stick Y axes: positive is down on Android. */
        fun isDownPositive(axis: Int) = axis == MotionEvent.AXIS_Y || axis == MotionEvent.AXIS_RY ||
            axis == MotionEvent.AXIS_RZ || axis == MotionEvent.AXIS_HAT_Y
    }
}

/** "Left stick ↑↓", "Right trigger", "AXIS_GENERIC_3"... */
fun axisLabel(axis: Int): String = when (axis) {
    MotionEvent.AXIS_X -> "Left stick ←→"
    MotionEvent.AXIS_Y -> "Left stick ↑↓"
    MotionEvent.AXIS_Z -> "Right stick ←→"
    MotionEvent.AXIS_RZ -> "Right stick ↑↓"
    MotionEvent.AXIS_RX -> "Right stick ←→ (RX)"
    MotionEvent.AXIS_RY -> "Right stick ↑↓ (RY)"
    MotionEvent.AXIS_HAT_X -> "D-pad ←→"
    MotionEvent.AXIS_HAT_Y -> "D-pad ↑↓"
    MotionEvent.AXIS_LTRIGGER -> "Left trigger"
    MotionEvent.AXIS_RTRIGGER -> "Right trigger"
    MotionEvent.AXIS_BRAKE -> "Brake"
    MotionEvent.AXIS_GAS -> "Gas"
    MotionEvent.AXIS_THROTTLE -> "Throttle"
    MotionEvent.AXIS_RUDDER -> "Rudder"
    else -> MotionEvent.axisToString(axis).removePrefix("AXIS_").replace('_', ' ').lowercase()
        .replaceFirstChar { it.uppercase() }
}

/** "A", "L1", "Start", "Button 7"... */
fun buttonLabel(keyCode: Int): String = KeyEvent.keyCodeToString(keyCode)
    .removePrefix("KEYCODE_").removePrefix("BUTTON_").replace('_', ' ')
    .let { if (it.length <= 2) it else it.lowercase().replaceFirstChar { c -> c.uppercase() } }
    .let { if (it.firstOrNull()?.isDigit() == true) "Button $it" else it }
