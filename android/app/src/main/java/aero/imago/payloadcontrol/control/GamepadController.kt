package aero.imago.payloadcontrol.control

import android.content.Context
import android.hardware.input.InputManager
import android.os.Handler
import android.os.Looper
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import kotlin.math.abs

/**
 * Physical controllers: Xbox, PlayStation and generic Bluetooth or USB gamepads, and HOTAS or
 * flight sticks (Android exposes any HID joystick). Every axis and button can be assigned; what
 * each one does comes from a [GamepadMapping] the user edits ("press the button to assign it").
 *
 * Android delivers input on the main thread; the activity hands every gamepad event here before
 * the UI sees it, and gimbal rates and zoom go straight to the core from there.
 */
class GamepadController(context: Context) {
    /** An input the controller reported while learning. */
    sealed interface Learned {
        data class Button(val keyCode: Int) : Learned
        /** inverted: the axis was moved toward its negative end. */
        data class Axis(val axis: Int, val inverted: Boolean) : Learned
    }

    /** Gimbal rates in degrees per second. */
    var onGimbalRate: ((pitch: Float, yaw: Float) -> Unit)? = null
    /** -1 zoom out, 0 stop, 1 zoom in. */
    var onZoom: ((Int) -> Unit)? = null
    /** Actions that are not held (zoom goes through onZoom). */
    var onAction: ((GamepadAction) -> Unit)? = null
    /** What was pressed or moved while learning. */
    var onLearned: ((GamepadLearnTarget, Learned) -> Unit)? = null
    /** The controller's name, or null when none is connected. */
    var onConnectionChange: ((String?) -> Unit)? = null

    /** Latest EO zoom, used to scale the gimbal rate. */
    var zoomLevel = 1f

    private var shaper = GimbalInputShaper()
    private var mapping = GamepadMapping.standard
    /** Set while the user assigns an input; the next input is reported instead of acted on. */
    private var learning: GamepadLearnTarget? = null

    private var zoomDirection = 0
    private var zoomButtonDirection = 0
    private val pressed = mutableSetOf<Int>()
    private var lastRates = GimbalInputShaper.Rates(0f, 0f)
    /** Latest value of every axis, per device, so a button event does not reset the sticks. */
    private val axisValues = mutableMapOf<Int, Float>()
    /** Axis positions when learning began: throttles and pedals rest away from zero. */
    private val learnBaseline = mutableMapOf<Int, Float>()

    private val inputManager = context.getSystemService(Context.INPUT_SERVICE) as InputManager
    private val deviceListener = object : InputManager.InputDeviceListener {
        override fun onInputDeviceAdded(deviceId: Int) = devicesChanged()
        override fun onInputDeviceRemoved(deviceId: Int) {
            stopAll() // never leave the gimbal or the zoom moving
            devicesChanged()
        }
        override fun onInputDeviceChanged(deviceId: Int) = devicesChanged()
    }

    fun start() {
        inputManager.registerInputDeviceListener(deviceListener, Handler(Looper.getMainLooper()))
        devicesChanged()
    }

    fun stop() {
        inputManager.unregisterInputDeviceListener(deviceListener)
        stopAll()
    }

    // MARK: Configuration
    //
    // Each change stops whatever the controller is driving: a held button or stick may no
    // longer be mapped, or its release may go to learning, and neither must leave the gimbal
    // or the zoom moving. The next input starts it again with the new settings.

    fun setMapping(newMapping: GamepadMapping) {
        mapping = newMapping
        stopAll()
    }

    fun setShaper(newShaper: GimbalInputShaper) {
        shaper = newShaper
        stopAll()
    }

    fun setLearning(target: GamepadLearnTarget?) {
        learning = target
        learnBaseline.clear()
        stopAll()
    }

    /** Releases everything, e.g. when the app loses focus. */
    fun stopAll() {
        lastRates = GimbalInputShaper.Rates(0f, 0f)
        onGimbalRate?.invoke(0f, 0f)
        pressed.clear()
        axisValues.clear()
        zoomButtonDirection = 0
        setZoom(0)
    }

    private fun devicesChanged() {
        onConnectionChange?.invoke(gamepads().firstOrNull()?.name)
    }

    // MARK: Input

    /** A joystick or gamepad motion event. Returns true if it was consumed. */
    fun onMotionEvent(event: MotionEvent): Boolean {
        if (!isController(event.device) || event.action != MotionEvent.ACTION_MOVE) return false
        val device = event.device ?: return false
        val target = learning
        if (target != null) {
            if (target is GamepadLearnTarget.Axis) {
                learnAxis(target, event, device)
            }
            return true
        }
        for (range in device.motionRanges) {
            if (range.isFromSource(InputDevice.SOURCE_JOYSTICK) || range.isFromSource(InputDevice.SOURCE_GAMEPAD)) {
                axisValues[range.axis] = centered(event.getAxisValue(range.axis), range)
            }
        }
        updateAxes()
        return true
    }

    /** A gamepad button event. Returns true if it was consumed. */
    fun onKeyEvent(event: KeyEvent): Boolean {
        if (!isController(event.device) || !isGamepadKey(event)) return false
        if (event.repeatCount > 0) return true
        val isDown = event.action == KeyEvent.ACTION_DOWN
        val target = learning
        if (target != null) {
            if (isDown && target is GamepadLearnTarget.Action) {
                learning = null
                onLearned?.invoke(target, Learned.Button(event.keyCode))
            }
            return true
        }
        val action = mapping.buttons[event.keyCode] ?: return true
        val wasDown = event.keyCode in pressed
        if (isDown == wasDown) return true
        if (isDown) pressed += event.keyCode else pressed -= event.keyCode
        when (action) {
            GamepadAction.ZOOM_IN -> zoomButton(1, isDown)
            GamepadAction.ZOOM_OUT -> zoomButton(-1, isDown)
            else -> if (isDown) onAction?.invoke(action)
        }
        return true
    }

    private fun updateAxes() {
        val yawBinding = mapping.axes[GamepadAxisFunction.GIMBAL_YAW]
        val pitchBinding = mapping.axes[GamepadAxisFunction.GIMBAL_PITCH]
        val rates = shaper.rates(
            x = value(yawBinding), y = value(pitchBinding), zoom = zoomLevel,
            digitalX = yawBinding?.let { GamepadMapping.isDigitalAxis(it.axis) } ?: false,
            digitalY = pitchBinding?.let { GamepadMapping.isDigitalAxis(it.axis) } ?: false,
        )
        if (rates != lastRates) {
            lastRates = rates
            onGimbalRate?.invoke(rates.pitch, rates.yaw)
        }
        // Unbound means centred, so a zoom running from an axis that was just cleared stops.
        zoomAxisChanged(value(mapping.axes[GamepadAxisFunction.ZOOM]))
    }

    private fun value(binding: GamepadAxisBinding?): Float {
        binding ?: return 0f
        val raw = axisValues[binding.axis] ?: 0f
        return if (binding.inverted) -raw else raw
    }

    private fun zoomButton(direction: Int, isDown: Boolean) {
        if (isDown) {
            zoomButtonDirection = direction
            setZoom(direction)
        } else if (zoomButtonDirection == direction) {
            zoomButtonDirection = 0
            setZoom(0)
        }
    }

    private fun zoomAxisChanged(value: Float) {
        if (zoomButtonDirection != 0) return // a held zoom button wins
        // Hysteresis: start at 0.5, stop below 0.3, so the zoom does not chatter.
        var direction = zoomDirection
        if (value > 0.5f) direction = 1
        else if (value < -0.5f) direction = -1
        else if (abs(value) < 0.3f) direction = 0
        setZoom(direction)
    }

    private fun setZoom(direction: Int) {
        if (direction != zoomDirection) {
            zoomDirection = direction
            onZoom?.invoke(direction)
        }
    }

    // MARK: Learning

    private fun learnAxis(target: GamepadLearnTarget.Axis, event: MotionEvent, device: InputDevice) {
        // The axis moved furthest from where it was when learning began, once past the threshold.
        var best: InputDevice.MotionRange? = null
        var bestDelta = 0f
        for (range in device.motionRanges) {
            val value = centered(event.getAxisValue(range.axis), range)
            val delta = value - learnBaseline.getOrPut(range.axis) { if (abs(value) < LEARN_THRESHOLD) 0f else value }
            if (abs(delta) >= LEARN_THRESHOLD && abs(delta) > abs(bestDelta)) {
                best = range
                bestDelta = delta
            }
        }
        best ?: return
        learning = null
        onLearned?.invoke(target, Learned.Axis(best.axis, inverted = bestDelta < 0))
    }

    private fun gamepads(): List<InputDevice> = InputDevice.getDeviceIds().toList()
        .mapNotNull { InputDevice.getDevice(it) }
        .filter { isController(it) }

    companion object {
        /** Deflection that counts as "this input" while learning. */
        private const val LEARN_THRESHOLD = 0.6f

        private fun isController(device: InputDevice?): Boolean {
            device ?: return false
            if (device.isVirtual) return false
            val sources = device.sources
            return sources and InputDevice.SOURCE_GAMEPAD == InputDevice.SOURCE_GAMEPAD ||
                sources and InputDevice.SOURCE_JOYSTICK == InputDevice.SOURCE_JOYSTICK
        }

        private fun isGamepadKey(event: KeyEvent): Boolean {
            if (KeyEvent.isGamepadButton(event.keyCode)) return true
            return when (event.keyCode) {
                KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_DPAD_LEFT,
                KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_DPAD_CENTER -> true
                else -> false
            }
        }

        /** Within the device's flat zone the axis is at rest. Triggers and throttles go 0..1. */
        private fun centered(value: Float, range: InputDevice.MotionRange): Float =
            if (abs(value) <= range.flat) 0f else value
    }
}
