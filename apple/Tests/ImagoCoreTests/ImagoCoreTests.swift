import ImagoCore
import XCTest

// Smoke tests: the C API is visible from Swift and links on Apple platforms.
// Protocol behaviour is covered by the C++ tests in core/tests.
final class ImagoCoreTests: XCTestCase {
    func testConfigDefaults() {
        var config = imago_config()
        imago_config_init(&config)
        XCTAssertEqual(config.payload_port, 14566)
        XCTAssertEqual(config.gimbal_rate_hz, 30)
        XCTAssertEqual(config.nal_format, IMAGO_NAL_AVCC)
    }

    func testSessionLifecycle() {
        var config = imago_config()
        imago_config_init(&config)
        let session = "127.0.0.1".withCString { ip -> OpaquePointer? in
            config.payload_ip = ip
            config.local_port = 0
            return imago_session_create(&config, nil, nil, nil)
        }
        XCTAssertNotNil(session)
        var state = imago_state()
        imago_session_get_state(session, &state)
        XCTAssertFalse(state.link_up)
        imago_session_destroy(session)
    }

    func testMonotonicClock() {
        let a = imago_now_ns()
        let b = imago_now_ns()
        XCTAssertGreaterThanOrEqual(b, a)
    }
}
