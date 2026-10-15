package aero.imago.payloadcontrol

import aero.imago.payloadcontrol.app.AppModel
import aero.imago.payloadcontrol.ui.ImagoTheme
import aero.imago.payloadcontrol.ui.OperatorScreen
import android.annotation.SuppressLint
import android.os.Build
import android.os.Bundle
import android.view.HapticFeedbackConstants
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.viewModels
import androidx.compose.runtime.snapshotFlow
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.launch

/**
 * The only activity: full screen, landscape, no system bars. Gamepad input is taken here before
 * the UI sees it, so a controller drives the payload whatever has focus.
 */
class MainActivity : ComponentActivity() {
    private val model: AppModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        WindowCompat.setDecorFitsSystemWindows(window, false)
        hideSystemBars()

        model.haptics = { haptic ->
            val view = window.decorView
            val constant = when (haptic) {
                AppModel.Haptic.TAP -> HapticFeedbackConstants.KEYBOARD_TAP
                AppModel.Haptic.CONFIRM ->
                    if (Build.VERSION.SDK_INT >= 30) HapticFeedbackConstants.CONFIRM else HapticFeedbackConstants.VIRTUAL_KEY
                AppModel.Haptic.ERROR ->
                    if (Build.VERSION.SDK_INT >= 30) HapticFeedbackConstants.REJECT else HapticFeedbackConstants.LONG_PRESS
            }
            view.performHapticFeedback(constant)
        }

        // The screen stays on while connected.
        lifecycleScope.launch {
            snapshotFlow { model.isConnected }.collect { connected ->
                if (connected) window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                else window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
            }
        }

        setContent {
            ImagoTheme {
                OperatorScreen(model)
            }
        }
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemBars() else model.onPause()
    }

    override fun onStart() {
        super.onStart()
        model.onStart()
    }

    override fun onPause() {
        // A dialog, a call or the recent apps screen: never leave anything moving.
        model.onPause()
        super.onPause()
    }

    override fun onStop() {
        // In the background the sockets are of no use; reconnect on return.
        if (!isChangingConfigurations) model.onStop()
        super.onStop()
    }

    override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean =
        model.gamepad.onMotionEvent(event) || super.dispatchGenericMotionEvent(event)

    // ComponentActivity marks dispatchKeyEvent as library-internal, but overriding the platform
    // Activity method is the documented way to see gamepad buttons before the views do.
    @SuppressLint("RestrictedApi")
    override fun dispatchKeyEvent(event: KeyEvent): Boolean =
        model.gamepad.onKeyEvent(event) || super.dispatchKeyEvent(event)

    private fun hideSystemBars() {
        WindowInsetsControllerCompat(window, window.decorView).apply {
            hide(WindowInsetsCompat.Type.systemBars())
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
    }
}
