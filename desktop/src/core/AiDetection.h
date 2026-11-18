#pragma once

#include <QtGlobal>

#include <cstdint>
#include <optional>

/// The AI button. The payload reports no AI state and its acknowledgement seldom comes back
/// through Herelink (flight 2026-11-13), so the button shows what was asked; a second press
/// within kRepressMs of the last one is the same press, not "off again"; and when AI was
/// switched on and no detection box has come kNoBoxesMs later, the operator is told once
/// instead of being left pressing the button again. Times are any monotonic clock, in ms.
class AiDetectionButton {
  public:
    static constexpr qint64 kRepressMs = 1000;
    static constexpr qint64 kNoBoxesMs = 6000;

    bool enabled() const { return enabled_; }

    /// A press of the button (on screen or on a controller). The state to ask the payload for,
    /// or nothing when it repeats the previous press.
    std::optional<bool> press(qint64 nowMs, uint32_t detectionFrame);

    /// Each poll of the payload's state. true once when the "no boxes" notice is due. The wait
    /// starts at the first poll with the link up after switching on; a link loss clears it and
    /// re-arms the notice.
    bool update(qint64 nowMs, bool linkUp, uint32_t detectionFrame, int detectionCount);

    /// A new session: off, and nothing pending.
    void reset();

  private:
    void restartWait(uint32_t detectionFrame);

    bool enabled_ = false;
    std::optional<qint64> lastPressMs_;
    /// When the wait for the first box started (link up with AI on); none while not waiting.
    std::optional<qint64> waitingSinceMs_;
    uint32_t frameAtStart_ = 0;
    bool warned_ = false;
};
