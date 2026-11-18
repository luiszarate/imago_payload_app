import XCTest

/// The AI button: what was asked, a repeated press, and the notice when no box comes.
final class DetectionWatchTests: XCTestCase {
    func testASecondPressWithinASecondIsTheSamePress() {
        var watch = DetectionWatch()
        XCTAssertEqual(watch.press(at: 10), true)
        XCTAssertNil(watch.press(at: 10.5)) // not "off again"
        XCTAssertNil(watch.press(at: 11))
        XCTAssertTrue(watch.enabled)
        XCTAssertEqual(watch.press(at: 11.2), false)
        XCTAssertFalse(watch.enabled)
    }

    func testNoBoxesInSixSecondsIsSaidOnce() {
        var watch = DetectionWatch()
        _ = watch.press(at: 0)
        XCTAssertFalse(watch.check(at: 0.03, linkUp: true, frame: 40, count: 0)) // the 6 s start
        XCTAssertFalse(watch.check(at: 5, linkUp: true, frame: 40, count: 0))
        XCTAssertTrue(watch.check(at: 6.1, linkUp: true, frame: 40, count: 0))
        XCTAssertFalse(watch.check(at: 7, linkUp: true, frame: 40, count: 0))
        XCTAssertFalse(watch.check(at: 30, linkUp: true, frame: 40, count: 0))
    }

    func testBoxesThatCameSayNothing() {
        var watch = DetectionWatch()
        _ = watch.press(at: 0)
        _ = watch.check(at: 0.03, linkUp: true, frame: 40, count: 0)
        // A box came and went (the core clears them 1.5 s after the last packet).
        XCTAssertFalse(watch.check(at: 6.1, linkUp: true, frame: 52, count: 0))
        var other = DetectionWatch()
        _ = other.press(at: 0)
        _ = other.check(at: 0.03, linkUp: true, frame: 40, count: 0)
        XCTAssertFalse(other.check(at: 6.1, linkUp: true, frame: 40, count: 2))
    }

    func testOffLinkLossAndANewSessionStartAgain() {
        var watch = DetectionWatch()
        _ = watch.press(at: 0)
        _ = watch.check(at: 0.03, linkUp: true, frame: 1, count: 0)
        // Link lost at 4 s: the 6 s start again once it is back.
        XCTAssertFalse(watch.check(at: 4, linkUp: false, frame: 1, count: 0))
        XCTAssertFalse(watch.check(at: 8, linkUp: true, frame: 1, count: 0))
        XCTAssertFalse(watch.check(at: 13, linkUp: true, frame: 1, count: 0))
        XCTAssertTrue(watch.check(at: 14.1, linkUp: true, frame: 1, count: 0))
        // Off: nothing more is said.
        _ = watch.press(at: 20)
        XCTAssertFalse(watch.check(at: 20.1, linkUp: true, frame: 1, count: 0))
        XCTAssertFalse(watch.check(at: 40, linkUp: true, frame: 1, count: 0))
        // Back on: watched again, and said again.
        _ = watch.press(at: 41)
        _ = watch.check(at: 41.03, linkUp: true, frame: 1, count: 0)
        XCTAssertTrue(watch.check(at: 47.1, linkUp: true, frame: 1, count: 0))
        // A new session: off, and the next press turns it on.
        watch.reset()
        XCTAssertFalse(watch.enabled)
        XCTAssertEqual(watch.press(at: 47.2), true)
    }
}
