import XCTest

/// Saved profiles load with the defaults of the fields added since they were saved.
final class ConnectionProfileTests: XCTestCase {
    func testAircraftTelemetryIsOnByDefault() {
        XCTAssertTrue(ConnectionProfile.herelink.aircraftTelemetry)
        XCTAssertTrue(ConnectionProfile(name: "New", payloadIP: "", videoURL: "").aircraftTelemetry)
        XCTAssertEqual(ConnectionProfile.aircraftTelemetryPort, 14550)
    }

    func testAnOldProfileLoadsWithAircraftTelemetryOn() throws {
        // As an earlier version saved it, before the aircraft telemetry setting.
        let old = #"""
        [{"id": "6F1B3C1E-8C40-4B8B-9D5E-2B1F7A7E2C11", "name": "Orus L · Herelink",
          "mavlinkMode": "direct", "payloadIP": "192.168.144.240", "mavlinkPort": 14566,
          "localPort": 0, "videoURL": "rtsp://192.168.43.1:8554/fpv_stream",
          "videoTransport": "auto", "gimbalProtocol": "attitude", "setStandardRCMode": false,
          "maxVideoLagMs": 500, "keepWifiAwake": false, "videoSmoothingMs": 0}]
        """#
        let profiles = try JSONDecoder().decode([ConnectionProfile].self, from: Data(old.utf8))
        XCTAssertEqual(profiles.count, 1)
        XCTAssertTrue(profiles[0].aircraftTelemetry)
        XCTAssertEqual(profiles[0].payloadIP, "192.168.144.240")
    }

    func testTurnedOffItStaysOff() throws {
        var profile = ConnectionProfile.direct
        profile.aircraftTelemetry = false
        let back = try JSONDecoder().decode(ConnectionProfile.self, from: JSONEncoder().encode(profile))
        XCTAssertFalse(back.aircraftTelemetry)
    }
}
