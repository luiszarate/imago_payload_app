package aero.imago.payloadcontrol.app

/**
 * The AI button and what it can know. The payload reports no AI state and its acknowledgement
 * seldom comes back through Herelink (flight 2026-11-13), so the button shows what was asked;
 * a second press within [REPRESS_MS] of the last (inclusive, as on the Quest) is the same
 * press (not "off again"); and when AI was switched on and no detection box has come
 * [PATIENCE_MS] later, the operator is told once instead of pressing the button again (through
 * Herelink with PX4 forwarding the boxes never came, same flight). Times are elapsed
 * milliseconds; on the main thread only.
 */
class DetectionWatch {
    /** What was last asked: on or off. */
    var enabled = false
        private set
    private var lastPressMs = Long.MIN_VALUE
    /** The payload's detection packet count when AI was switched on. */
    private var startFrame = 0
    /** When the wait for a box started (link up); 0 = not yet. */
    private var sinceMs = 0L
    private var warned = false

    /**
     * A press of the button (on screen or gamepad). Returns the new state to send, or null when
     * the press is taken as the previous one.
     */
    fun press(nowMs: Long, detectionFrame: Int): Boolean? {
        if (lastPressMs != Long.MIN_VALUE && nowMs - lastPressMs <= REPRESS_MS) return null
        lastPressMs = nowMs
        enabled = !enabled
        startFrame = detectionFrame
        sinceMs = 0
        warned = false
        return enabled
    }

    /**
     * Called on every poll. True once, when AI has been on for [PATIENCE_MS] with the link up
     * and not a single detection packet arrived.
     */
    fun check(nowMs: Long, linkUp: Boolean, detectionFrame: Int, detectionCount: Int): Boolean {
        if (!enabled || !linkUp || warned) return false
        if (sinceMs == 0L) {
            sinceMs = nowMs
            return false
        }
        if (nowMs - sinceMs <= PATIENCE_MS) return false
        warned = true
        return detectionFrame == startFrame && detectionCount == 0
    }

    /** Link lost: wait afresh once it is back, from what arrives then. */
    fun linkLost() {
        sinceMs = 0
        warned = false
    }

    /** A new session: AI off, as the button then shows. */
    fun reset() {
        enabled = false
        lastPressMs = Long.MIN_VALUE
        startFrame = 0
        sinceMs = 0
        warned = false
    }

    companion object {
        const val REPRESS_MS = 1000L
        const val PATIENCE_MS = 6000L
        const val NO_BOXES_NOTICE = "AI detection is on, but no boxes reach the app on this link"
    }
}
