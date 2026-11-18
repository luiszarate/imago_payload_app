import Foundation

/// The AI button's state as asked, and the watch for boxes that never come.
///
/// The payload reports no AI state and its acknowledgement seldom comes back through Herelink
/// (flight 2026-11-13). So the button shows what was asked; a second press within a second of
/// the last one is taken as the same press (not as "off again"); and when AI was switched on
/// and no box has arrived 6 s later, the operator is told once instead of being left pressing
/// the button again (through Herelink with PX4 forwarding none did, that flight).
struct DetectionWatch {
    static let repressWindow: TimeInterval = 1
    static let noBoxesAfter: TimeInterval = 6

    /// AI detection as last asked.
    private(set) var enabled = false
    private var lastPress: TimeInterval?
    /// When the 6 s started (AI on and the link up), and the payload's detection frame then.
    private var since: TimeInterval?
    private var frameAtStart: UInt32 = 0
    private var warned = false

    /// A press of the AI button or the gamepad action at `now` (seconds, monotonic). Returns
    /// the state to ask for, or nil when it is the same press again.
    mutating func press(at now: TimeInterval) -> Bool? {
        if let lastPress, now - lastPress <= Self.repressWindow {
            return nil
        }
        lastPress = now
        enabled.toggle()
        since = nil
        warned = false
        return enabled
    }

    /// Once per poll. True, once, when AI has been on for 6 s with the link up and the payload's
    /// detection frame has not moved and there is no box.
    mutating func check(at now: TimeInterval, linkUp: Bool, frame: UInt32, count: Int) -> Bool {
        guard enabled, linkUp else {
            // Link lost: the 6 s start again, and may warn again, once it is back.
            since = nil
            warned = false
            return false
        }
        guard let since else {
            self.since = now
            frameAtStart = frame
            return false
        }
        guard !warned, now - since > Self.noBoxesAfter else { return false }
        warned = true
        return frame == frameAtStart && count == 0
    }

    /// A new session, or the session ended: AI is off as far as this app knows.
    mutating func reset() {
        self = DetectionWatch()
    }
}
