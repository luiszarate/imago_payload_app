#pragma once

#include <cstdint>

namespace imago {

// Measures how much later than its own pace a stream arrives.
//
// The sender stamps every frame with its capture time (the RTP timestamp). Ideally
// arrival - capture is constant; a frame that arrives later than the fastest frame seen recently
// has waited in a queue somewhere: the sender, the datalink, Wi-Fi or our own socket. A player
// that paces frames by their timestamps keeps such a delay forever; this one shows frames as soon
// as they arrive, so a lasting lag means a queue upstream that only a restart can drain.
//
// The sender's clock and ours drift apart by a few ppm, so the reference creeps up slowly
// (1 ms per second), which also forgets an old lasting lag after a few minutes.
class LagTracker {
  public:
    explicit LagTracker(uint32_t clockRate = 90000) : clockRate_(clockRate) {}

    // Returns the lag of one frame, in ns (>= 0). arrivalNs: when its first packet arrived.
    int64_t update(uint32_t rtpTimestamp, int64_t arrivalNs);
    void reset() { started_ = false; }

  private:
    const uint32_t clockRate_;
    bool started_ = false;
    uint32_t lastTimestamp_ = 0;
    int64_t mediaTicks_ = 0;   // unwrapped timestamp since the first frame
    int64_t referenceNs_ = 0;  // arrival - media time of the fastest frame, with drift allowance
    int64_t referenceAtNs_ = 0;
};

} // namespace imago
