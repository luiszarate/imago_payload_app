package aero.imago.payloadcontrol

import aero.imago.payloadcontrol.control.GamepadAction
import aero.imago.payloadcontrol.control.GamepadAxisFunction
import aero.imago.payloadcontrol.control.GamepadMapping
import aero.imago.payloadcontrol.control.GimbalInputShaper
import aero.imago.payloadcontrol.core.AppJson
import aero.imago.payloadcontrol.core.ConnectionProfile
import aero.imago.payloadcontrol.core.Detection
import aero.imago.payloadcontrol.core.NormRect
import aero.imago.payloadcontrol.core.hit
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test

class GimbalInputShaperTest {
    private val shaper = GimbalInputShaper()

    @Test
    fun deadzoneAndFullDeflection() {
        assertEquals(0f, shaper.shape(0.05f))
        assertEquals(1f, shaper.shape(1f), 1e-6f)
        assertEquals(-1f, shaper.shape(-2f), 1e-6f)
    }

    @Test
    fun rateShrinksWithZoom() {
        val wide = shaper.rates(1f, 0f, zoom = 1f)
        val tele = shaper.rates(1f, 0f, zoom = 4f)
        assertEquals(60f, wide.yaw, 1e-4f)
        assertEquals(30f, tele.yaw, 1e-4f)
    }

    @Test
    fun digitalInputsUseTheirOwnSpeed() {
        val rates = shaper.rates(0.3f, -1f, zoom = 1f, digitalX = true, digitalY = true)
        assertEquals(0f, rates.yaw) // below half way
        assertEquals(-20f, rates.pitch)
    }
}

class ConnectionProfileTest {
    @Test
    fun validation() {
        assertNull(ConnectionProfile.herelink.validationError)
        assertNotNull(ConnectionProfile.herelink.copy(payloadIP = "192.168.1").validationError)
        assertNotNull(ConnectionProfile.herelink.copy(videoURL = "http://x").validationError)
        assertNotNull(ConnectionProfile.herelink.copy(mavlinkMode = ConnectionProfile.MavlinkMode.LISTEN, videoURL = "").validationError)
        assertNull(ConnectionProfile.herelink.copy(mavlinkMode = ConnectionProfile.MavlinkMode.LISTEN, payloadIP = "").validationError)
    }

    @Test
    fun savedProfilesMissingNewerFieldsGetDefaults() {
        val json = """[{"id":"a","name":"Old","payloadIP":"10.0.0.1","videoURL":"","videoTransport":"tcp","wifiKeepAwake":true,"someFutureField":1}]"""
        val profile = AppJson.decodeFromString<List<ConnectionProfile>>(json).single()
        assertEquals("Old", profile.name)
        assertEquals(ConnectionProfile.VideoTransport.TCP, profile.videoTransport)
        assertEquals(14566, profile.mavlinkPort)
        assertEquals(500, profile.maxVideoLagMs)
        assertEquals(false, profile.keepWifiAwake)
        assertEquals(0, profile.videoSmoothingMs)
    }

    @Test
    fun roundTrip() {
        val profile = ConnectionProfile.herelink.copy(gimbalProtocol = ConnectionProfile.GimbalProtocol.MANAGER)
        assertEquals(profile, AppJson.decodeFromString<ConnectionProfile>(AppJson.encodeToString(profile)))
    }
}

class GamepadMappingTest {
    @Test
    fun oneButtonPerActionAndOneActionPerButton() {
        val a = GamepadMapping.standard.button(GamepadAction.SHUTTER)!!
        val moved = GamepadMapping.standard.bind(GamepadAction.RECENTER, a)
        assertEquals(GamepadAction.RECENTER, moved.buttons[a])
        assertNull(moved.button(GamepadAction.SHUTTER))
        assertEquals(1, moved.buttons.values.count { it == GamepadAction.RECENTER })
    }

    @Test
    fun oneFunctionPerAxis() {
        val yawAxis = GamepadMapping.standard.axes.getValue(GamepadAxisFunction.GIMBAL_YAW).axis
        val moved = GamepadMapping.standard.bind(GamepadAxisFunction.ZOOM, yawAxis, inverted = false)
        assertNull(moved.axes[GamepadAxisFunction.GIMBAL_YAW])
        assertEquals(yawAxis, moved.axes.getValue(GamepadAxisFunction.ZOOM).axis)
    }

    @Test
    fun jsonRoundTrip() {
        val mapping = GamepadMapping.standard.withInverted(GamepadAxisFunction.GIMBAL_YAW, true)
        assertEquals(mapping, AppJson.decodeFromString<GamepadMapping>(AppJson.encodeToString(mapping)))
    }
}

class DetectionHitTest {
    @Test
    fun smallestBoxAroundTheTapWins() {
        val big = Detection(0, NormRect(0.1f, 0.1f, 0.8f, 0.8f), 0, 0, 0.9f)
        val small = Detection(1, NormRect(0.4f, 0.4f, 0.1f, 0.1f), 2, 0, 0.8f)
        assertEquals(small, listOf(big, small).hit(0.45f, 0.45f))
        assertEquals(big, listOf(big, small).hit(0.2f, 0.2f))
        assertNull(listOf(small).hit(0.9f, 0.9f))
    }
}
