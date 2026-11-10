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
        val text = Profiles.json.encodeToString(ProfilesFile(Profiles.defaults, ProfilesFile.CURRENT))
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
    fun filesFromBeforeTheAngleDefaultMoveToAngles() {
        val old = """{"profiles":[{"name":"a","payloadIP":"10.0.0.1","headTracking":{"mode":"rate","gainDpsPerDeg":3.0}}]}"""
        val migrated = Profiles.parse(old).single()
        assertEquals("angle", migrated.headTracking.mode)
        assertEquals(0f, migrated.headTracking.gainDpsPerDeg, 0f)
        // A current file keeps what the operator chose.
        val current = """{"version":2,"profiles":[{"name":"a","payloadIP":"10.0.0.1","headTracking":{"mode":"rate","gainDpsPerDeg":0.8}}]}"""
        assertEquals("rate", Profiles.parse(current).single().headTracking.mode)
    }

    @Test
    fun rawValuesMatchTheCoreEnums() {
        val p = ConnectionProfile(payloadIP = "10.0.0.1", videoTransport = "tcp", gimbalProtocol = "manager",
            headTracking = HeadTrackingSettings(mode = "angle"))
        assertEquals(2, p.transportRaw)
        assertEquals(2, p.gimbalProtocolRaw)
        assertEquals(2, p.headTracking.modeRaw)
        assertEquals(2, ConnectionProfile(payloadIP = "10.0.0.1").headTracking.modeRaw) // angles by default
        assertEquals(1, HeadTrackingSettings(mode = "rate").modeRaw)
        assertNotNull(ConnectionProfile(payloadIP = "10.0.0.1", videoURL = "http://x").validationError)
    }
}
