package aero.imago.payloadcontrol

import aero.imago.payloadcontrol.core.CameraMode
import aero.imago.payloadcontrol.core.GimbalControl
import aero.imago.payloadcontrol.core.GimbalMode
import aero.imago.payloadcontrol.core.NormRect
import aero.imago.payloadcontrol.core.PayloadSnapshot
import aero.imago.payloadcontrol.core.TrackingState
import aero.imago.payloadcontrol.core.ViewSource
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.nio.ByteBuffer

/**
 * Reads state_fixture.bin, written by the native state_writer.cpp from a state with a distinct
 * value in every field (src/test/cpp/make_state_fixture.cpp), so a field read out of order fails.
 */
class PayloadSnapshotTest {
    private val snapshot: PayloadSnapshot = run {
        val bytes = javaClass.classLoader!!.getResourceAsStream("state_fixture.bin")!!.use { it.readBytes() }
        PayloadSnapshot.read(ByteBuffer.wrap(bytes), bytes.size)
    }

    @Test
    fun link() {
        val link = snapshot.linkStatus
        assertTrue(link.up)
        assertEquals("192.168.144.240:14566", link.peer)
        assertEquals(3, link.roundTripMs) // 2.5 rounds up
        assertEquals(100, link.cameraComponent)
        assertEquals(154, link.gimbalComponent)
        assertEquals(1002, snapshot.mavlinkTxMessages)
        assertEquals(4, snapshot.commandsPending)
        assertEquals("Orus L", snapshot.payloadInfo.model)
        assertEquals("1.2.3", snapshot.payloadInfo.softwareVersion)
        assertEquals("SN9", snapshot.payloadInfo.serialNumber)
    }

    @Test
    fun gimbal() {
        val gimbal = snapshot.gimbalStatus
        assertTrue(gimbal.valid)
        assertEquals(3.25f, snapshot.gimbalRollDeg)
        assertEquals(-45.5f, gimbal.pitch)
        assertEquals(12.5f, gimbal.yaw)
        assertEquals(270.3f, gimbal.yawNorth!!, 0.001f)
        assertEquals(GimbalMode.FOLLOW, gimbal.mode)
        assertEquals(GimbalControl.Other(255, 190), gimbal.control)
        assertEquals("Ground station", gimbal.control.holder)
        assertEquals(91f, snapshot.vehicleHeading) // 90.5 rounds up
    }

    @Test
    fun camera() {
        val camera = snapshot.cameraStatus
        assertEquals(ViewSource.IR, camera.view)
        assertEquals(CameraMode.VIDEO, camera.mode)
        assertEquals(12.3f, camera.zoom, 0.001f)
        assertEquals(25f, camera.zoomMax)
        assertEquals(2f, snapshot.irZoomLevel)
        assertEquals(33f, camera.horizontalFOV)
        assertTrue(camera.recording)
        assertEquals(61, camera.recordingSeconds)
        assertEquals(7, snapshot.imageCount)
        assertEquals(0.5f, camera.storageAvailableGB)
        assertEquals(123.4f, camera.laserRangeMeters!!, 0.001f)
    }

    @Test
    fun tracking() {
        val tracking = snapshot.trackingStatus
        assertEquals(TrackingState.LOST, tracking.state)
        assertEquals(NormRect(0.1f, 0.2f, 0.3f, 0.4f), tracking.box)
        assertEquals(2, tracking.detections.size)
        val second = tracking.detections[1]
        assertEquals(NormRect(0.2f, 0.3f, 0.05f, 0.06f), second.rect)
        assertEquals(8, second.trackId)
        assertEquals(2, second.classId)
        assertEquals("car", second.label)
        assertEquals(0.75f, second.confidence)
        assertTrue(snapshot.irTempValid)
        assertEquals(-5.5f, snapshot.irTempMinC)
        assertEquals(20.25f, snapshot.irTempMeanC)
    }

    @Test
    fun video() {
        val m = snapshot.videoMetrics
        assertTrue(m.active)
        assertEquals("H.265", m.codec)
        assertEquals("TCP", m.transport)
        assertEquals(29.97f, m.fps)
        assertEquals(4000f, m.kbps)
        assertEquals(5000, snapshot.videoPacketsReceived)
        assertEquals(3, m.packetsLost)
        assertEquals(2, m.packetsLate)
        assertEquals(900, snapshot.videoFramesDelivered)
        assertEquals(1, m.framesCorrupt)
        assertEquals(15.5f, m.transferMs)
        assertEquals(34.5f, m.frameGapP95Ms)
        assertEquals(101f, m.frameGapMaxMs)
        assertEquals(4.5f, m.decodeMs)
        assertEquals(20.5f, snapshot.latencyPresentMs)
        assertEquals(1, m.resyncs)
        assertEquals(100, snapshot.videoLagSteps) // 120 ms in 50 ms steps
    }
}
