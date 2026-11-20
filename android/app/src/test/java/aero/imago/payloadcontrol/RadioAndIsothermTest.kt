package aero.imago.payloadcontrol

import aero.imago.payloadcontrol.core.RadioStatus
import aero.imago.payloadcontrol.core.RadioStatus.Level
import aero.imago.payloadcontrol.core.WebIsotherm
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class RadioStatusTest {
    private fun radio(level: Level, rssi: Int? = -101, snr: Int? = 2) = RadioStatus(level, rssi, snr)

    @Test
    fun levelsFromTheCore() {
        assertEquals(Level.UNKNOWN, Level.of(0))
        assertEquals(Level.WEAK, Level.of(2))
        assertEquals(Level.LOST, Level.of(4))
        assertEquals(Level.UNKNOWN, Level.of(9))
    }

    @Test
    fun criticalIsSaidOnceWithItsFigures() {
        assertEquals("Radio link critical: SNR 2 dB, RSSI -101 dBm", RadioStatus.notice(Level.GOOD, radio(Level.CRITICAL)))
        assertEquals("Radio link critical: SNR 2 dB, RSSI -101 dBm", RadioStatus.notice(Level.WEAK, radio(Level.CRITICAL)))
        assertNull(RadioStatus.notice(Level.CRITICAL, radio(Level.CRITICAL)))
    }

    @Test
    fun lostIsSaidUnlessTheLinkWasAlreadyCritical() {
        assertEquals("Radio link: no status from the Herelink for 5 s",
                     RadioStatus.notice(Level.GOOD, radio(Level.LOST, null, null)))
        // Critical, then lost: one episode, one notice.
        assertNull(RadioStatus.notice(Level.CRITICAL, radio(Level.LOST, null, null)))
        // Back to good, then critical again: a new episode.
        assertEquals("Radio link critical: SNR 2 dB, RSSI -101 dBm", RadioStatus.notice(Level.GOOD, radio(Level.CRITICAL)))
    }

    @Test
    fun weakAndGoodAreOnlyTheChipsColour() {
        assertNull(RadioStatus.notice(Level.GOOD, radio(Level.WEAK, -90, 5)))
        assertNull(RadioStatus.notice(Level.CRITICAL, radio(Level.GOOD, -70, 30)))
        assertNull(RadioStatus.notice(Level.UNKNOWN, radio(Level.GOOD, -70, 30)))
    }
}

class WebIsothermTest {
    @Test
    fun theScaleOutlinesTheThresholdAndEverythingWarmer() {
        assertEquals(35..550, WebIsotherm(true, 35, 0).scaleBand)
        assertEquals(550..551, WebIsotherm(true, 550, 1).scaleBand)
        assertNull(WebIsotherm(false, 35, 0).scaleBand)
    }

    @Test
    fun presetsAsTheThermalBarOffersThem() {
        assertEquals(listOf(20 to "Warm", 35 to "People", 100 to "Hot", 150 to "Fire"), WebIsotherm.PRESETS)
        // Fire stays in the low range, which measures finer; only a threshold over it needs the high one.
        assertEquals(150, WebIsotherm.LOW_RANGE_MAX_C)
    }
}
