#include "util/stats.h"

#include <algorithm>

namespace imago {

void WindowedPercentile::add(int64_t nowNs, float value) {
    values_[next_] = value;
    times_[next_] = nowNs;
    next_ = (next_ + 1) % kCapacity;
    count_ = std::min(count_ + 1, kCapacity);
}

float WindowedPercentile::percentile(int64_t nowNs, int64_t windowNs, float p) const {
    std::array<float, kCapacity> recent{};
    size_t n = 0;
    for (size_t i = 0; i < count_; ++i) {
        if (nowNs - times_[i] <= windowNs) {
            recent[n++] = values_[i];
        }
    }
    if (n == 0) {
        return 0.0f;
    }
    const auto rank = static_cast<size_t>(p * static_cast<float>(n - 1) + 0.5f);
    std::nth_element(recent.begin(), recent.begin() + rank, recent.begin() + n);
    return recent[rank];
}

void RateMeter::add(int64_t nowNs, size_t bytes) {
    if (windowStartNs_ == 0) {
        windowStartNs_ = nowNs;
    }
    ++events_;
    bytes_ += bytes;
}

void RateMeter::update(int64_t nowNs) {
    if (windowStartNs_ == 0) {
        windowStartNs_ = nowNs;
        return;
    }
    const int64_t elapsed = nowNs - windowStartNs_;
    if (elapsed < 1'000'000'000) {
        return;
    }
    const double seconds = static_cast<double>(elapsed) / 1e9;
    eventsPerSecond_ = static_cast<float>(static_cast<double>(events_) / seconds);
    kilobitsPerSecond_ = static_cast<float>(static_cast<double>(bytes_) * 8.0 / 1000.0 / seconds);
    events_ = 0;
    bytes_ = 0;
    windowStartNs_ = nowNs;
}

} // namespace imago
