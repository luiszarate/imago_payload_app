#pragma once

#include <chrono>
#include <cstdint>

namespace imago {

// Monotonic time in nanoseconds. Every timestamp in the core uses this clock.
inline int64_t monotonicNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

constexpr int64_t kNsPerMs = 1'000'000;
constexpr int64_t kNsPerSec = 1'000'000'000;

} // namespace imago
