package aero.imago.payloadcontrol.quest

import kotlinx.serialization.encodeToString
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class ProfilesTest {
    @Test
    fun defaultsAreValidAndRoundTrip() {
        Profiles.defaults.forEach { assertNull(it.name, it.validationError) }
        val text = Profiles.json.encodeToString(ProfilesFile(Profiles.defaults))
        assertEquals(Profiles.defaults, Profiles.parse(text))
    }

    @Test
    fun invalidProfilesAreDropped() {
        val parsed = Profiles.parse(
            """{"profiles":[
                {"name":"bad ip","mavlinkMode":"direct","payloadIP":"300.1.1.1"},
                {"name":"listen without video","mavlinkMode":"listen"},
                {"name":"ok","mavlinkMode":"listen","videoURL":"rtsp://192.168.43.1:8554/fpv_stream","unknownField":1}
            ]}""",
        )
        assertEquals(listOf("ok"), parsed.map { it.name })
        assertTrue(parsed[0].isListen)
    }

    @Test
    fun rawValuesMatchTheCoreEnums() {
        val p = ConnectionProfile(payloadIP = "10.0.0.1", videoTransport = "tcp", gimbalProtocol = "manager",
            headTracking = HeadTrackingSettings(mode = "angle"))
        assertEquals(2, p.transportRaw)
        assertEquals(2, p.gimbalProtocolRaw)
        assertEquals(2, p.headTracking.modeRaw)
        assertEquals(1, ConnectionProfile(payloadIP = "10.0.0.1").headTracking.modeRaw)
        assertNotNull(ConnectionProfile(payloadIP = "10.0.0.1", videoURL = "http://x").validationError)
    }
}
