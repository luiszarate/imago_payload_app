#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace imago {

// Keeps the last N samples with their timestamps and reports a percentile over a time window.
// Not thread-safe: guard it with the owner's lock.
class WindowedPercentile {
  public:
    static constexpr size_t kCapacity = 256;

    void add(int64_t nowNs, float value);
    // Percentile p in [0, 1] of the samples newer than windowNs. Returns 0 with no samples.
    float percentile(int64_t nowNs, int64_t windowNs, float p) const;

  private:
    std::array<float, kCapacity> values_{};
    std::array<int64_t, kCapacity> times_{};
    size_t next_ = 0;
    size_t count_ = 0;
};

// Counts events and bytes per second over a sliding one-second window.
class RateMeter {
  public:
    void add(int64_t nowNs, size_t bytes);
    // Recomputes the rates if a full second has elapsed since the last update.
    void update(int64_t nowNs);
    float eventsPerSecond() const { return eventsPerSecond_; }
    float kilobitsPerSecond() const { return kilobitsPerSecond_; }

  private:
    int64_t windowStartNs_ = 0;
    uint64_t events_ = 0;
    uint64_t bytes_ = 0;
    float eventsPerSecond_ = 0;
    float kilobitsPerSecond_ = 0;
};

} // namespace imago
