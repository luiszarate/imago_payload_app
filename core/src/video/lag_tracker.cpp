#include "video/lag_tracker.h"

#include <cstdlib>

namespace imago {

namespace {

constexpr int64_t kDriftPerSecondNs = 1'000'000; // 1000 ppm, far above real clock drift
constexpr int64_t kNsPerSecond = 1'000'000'000;
constexpr int64_t kMaxJumpSeconds = 10;          // larger timestamp jumps restart the measure

} // namespace

int64_t LagTracker::update(uint32_t rtpTimestamp, int64_t arrivalNs) {
    if (!started_) {
        started_ = true;
        lastTimestamp_ = rtpTimestamp;
        mediaTicks_ = 0;
        referenceNs_ = arrivalNs;
        referenceAtNs_ = arrivalNs;
        return 0;
    }
    const auto delta = static_cast<int32_t>(rtpTimestamp - lastTimestamp_);
    lastTimestamp_ = rtpTimestamp;
    if (std::llabs(delta) > kMaxJumpSeconds * static_cast<int64_t>(clockRate_)) {
        // The sender restarted its clock: start over from this frame.
        started_ = false;
        return update(rtpTimestamp, arrivalNs);
    }
    mediaTicks_ += delta;
    const int64_t mediaNs = mediaTicks_ * kNsPerSecond / clockRate_;
    const int64_t offsetNs = arrivalNs - mediaNs;

    if (arrivalNs > referenceAtNs_) {
        referenceNs_ += (arrivalNs - referenceAtNs_) * kDriftPerSecondNs / kNsPerSecond;
        referenceAtNs_ = arrivalNs;
    }
    if (offsetNs < referenceNs_) {
        referenceNs_ = offsetNs;
    }
    return offsetNs - referenceNs_;
}

} // namespace imago
