#pragma once

#include "imago/imago_core.h"
#include "platform/socket.h"

#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace imago::test {

// Waits until predicate() is true or timeoutMs passes.
inline bool waitFor(const std::function<bool()>& predicate, int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return predicate();
}

// A UDP port that was free a moment ago.
inline uint16_t freeUdpPort() {
    net::UdpSocket s;
    s.open(0, 0, net::TrafficClass::BestEffort, nullptr);
    return s.localPort();
}

struct RecordedEvent {
    imago_event_type type;
    uint32_t requestId;
    uint32_t code;
    int32_t result;
    std::string text;
};

class EventLog {
  public:
    static void callback(const imago_event* e, void* user) {
        auto* self = static_cast<EventLog*>(user);
        std::lock_guard lock(self->mutex_);
        self->events_.push_back({e->type, e->request_id, e->code, e->result,
                                 e->text != nullptr ? e->text : ""});
    }

    std::vector<RecordedEvent> events() const {
        std::lock_guard lock(mutex_);
        return events_;
    }

    bool has(imago_event_type type) const {
        std::lock_guard lock(mutex_);
        for (const auto& e : events_) {
            if (e.type == type) return true;
        }
        return false;
    }

    // The result event for a request ID, if it arrived.
    bool result(uint32_t requestId, int32_t& out) const {
        std::lock_guard lock(mutex_);
        for (const auto& e : events_) {
            if ((e.type == IMAGO_EVENT_COMMAND_RESULT || e.type == IMAGO_EVENT_PARAM_RESULT ||
                 e.type == IMAGO_EVENT_WEB_RESULT || e.type == IMAGO_EVENT_MEDIA_RESULT) &&
                e.requestId == requestId) {
                out = e.result;
                return true;
            }
        }
        return false;
    }

  private:
    mutable std::mutex mutex_;
    std::vector<RecordedEvent> events_;
};

} // namespace imago::test
