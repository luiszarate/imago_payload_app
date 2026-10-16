#pragma once

#include "mavlink/mavlink.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>

namespace imago {

// Decides what a compact recording keeps: what the payload said and was told, without the
// link-debugging bulk. Messages from other senders (the autopilot, the datalink) are left out;
// commands, their answers, settings and status texts are always kept; every other stream
// (attitude, data values, setpoints, heartbeats) is kept once a second per kind, so a
// minutes-long log stays small but still shows where the gimbal and the target were.
//
// Thread-safe: the network thread records what arrives, any sender records what leaves.
class TlogFilter {
  public:
    using SenderFilter = std::function<bool(uint8_t componentId)>;

    explicit TlogFilter(SenderFilter keepSender) : keepSender_(std::move(keepSender)) {}

    bool keep(const mavlink_message_t& message, bool outgoing, int64_t nowNs);

  private:
    SenderFilter keepSender_;
    std::mutex mutex_;
    std::unordered_map<uint64_t, int64_t> lastKeptNs_; // guarded by mutex_
};

} // namespace imago
