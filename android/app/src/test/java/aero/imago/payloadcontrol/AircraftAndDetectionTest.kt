package aero.imago.payloadcontrol

import aero.imago.payloadcontrol.app.DetectionWatch
import aero.imago.payloadcontrol.core.AircraftStatus
import aero.imago.payloadcontrol.core.AppJson
import aero.imago.payloadcontrol.core.ConnectionProfile
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class DetectionWatchTest {
    @Test
    fun aSecondPressWithinASecondIsTheSamePress() {
        val watch = DetectionWatch()
        assertEquals(true, watch.press(10_000, 0))
        assertNull(watch.press(10_400, 0)) // the acknowledgement did not come; pressed again
        assertNull(watch.press(11_000, 0)) // a second exactly: still the same press, as on the Quest
        assertTrue(watch.enabled)
        assertEquals(false, watch.press(11_100, 0)) // a real second press
        assertEquals(true, watch.press(12_200, 0))
    }

    @Test
    fun noBoxSixSecondsAfterSwitchingOnIsSaidOnce() {
        val watch = DetectionWatch()
        watch.press(1_000, 7)
        assertFalse(watch.check(1_000, linkUp = true, detectionFrame = 7, detectionCount = 0)) // the wait starts
        assertFalse(watch.check(6_900, true, 7, 0))
        assertTrue(watch.check(7_100, true, 7, 0))
        assertFalse(watch.check(20_000, true, 7, 0)) // once
        // The link dropped and came back: the wait starts afresh and it may be said again.
        watch.linkLost()
        assertFalse(watch.check(21_000, true, 7, 0))
        assertFalse(watch.check(26_900, true, 7, 0))
        assertTrue(watch.check(27_100, true, 7, 0))
    }

    @Test
    fun boxesThatCameMeanNoNotice() {
        val watch = DetectionWatch()
        watch.press(1_000, 7)
        watch.check(1_000, true, 7, 0)
        assertFalse(watch.check(7_100, true, 9, 0)) // packets came, even if no box now
    }

    @Test
    fun offOrWithoutALinkNothingIsSaid() {
        val watch = DetectionWatch()
        assertFalse(watch.check(1_000, true, 0, 0))
        assertFalse(watch.check(10_000, true, 0, 0)) // never switched on
        watch.press(20_000, 0)
        assertFalse(watch.check(20_000, false, 0, 0))
        assertFalse(watch.check(30_000, false, 0, 0)) // no link: no waiting
        assertFalse(watch.check(30_000, true, 0, 0)) // the wait starts with the link
        assertFalse(watch.check(35_000, true, 0, 0))
        watch.linkLost()
        assertFalse(watch.check(37_000, true, 0, 0)) // again from the link coming back
        assertTrue(watch.check(43_100, true, 0, 0))
        watch.press(50_000, 0) // off
        assertFalse(watch.enabled)
        assertFalse(watch.check(60_000, true, 0, 0))
        watch.reset()
        assertFalse(watch.enabled)
        assertEquals(true, watch.press(60_100, 0)) // a new session takes a press at once
    }
}

class AircraftStatusTest {
    @Test
    fun figuresAsTheOperatorViewShowsThem() {
        val full = AircraftStatus(altitudeM = 120, groundSpeedMps = 14, airSpeedMps = 15, batteryV = 22.8f, batteryPercent = 87)
        assertEquals(listOf("ALT" to "120 m", "GS" to "14 m/s", "AS" to "15 m/s", "BAT" to "22.8 V 87 %"), full.items)
        assertFalse(full.isEmpty)
        assertFalse(full.batteryWarning)
        val low = AircraftStatus(batteryV = 21.1f, batteryPercent = 12)
        assertEquals(listOf("BAT" to "21.1 V 12 %"), low.items)
        assertTrue(low.batteryWarning)
        assertTrue(low.batteryCritical)
        // No estimate from the autopilot: the voltage alone, never a warning.
        val unknown = AircraftStatus(batteryV = 22.0f)
        assertEquals(listOf("BAT" to "22.0 V"), unknown.items)
        assertFalse(unknown.batteryWarning)
        assertTrue(AircraftStatus().isEmpty)
        assertTrue(AircraftStatus().items.isEmpty())
    }
}

class AircraftTelemetryProfileTest {
    @Test
    fun oldProfilesLoadWithTelemetryOn() {
        val json = """[{"id":"a","name":"Old","mavlinkMode":"direct","payloadIP":"192.168.144.240"}]"""
        val loaded = AppJson.decodeFromString<List<ConnectionProfile>>(json)
        assertTrue(loaded.single().aircraftTelemetry)
        assertTrue(ConnectionProfile.herelink.aircraftTelemetry)
        val off = ConnectionProfile.herelink.copy(aircraftTelemetry = false)
        val back = AppJson.decodeFromString<ConnectionProfile>(AppJson.encodeToString(off))
        assertFalse(back.aircraftTelemetry)
    }
}
