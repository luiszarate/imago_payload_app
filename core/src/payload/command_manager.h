#pragma once

#include "mavlink/mavlink.h"

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace imago {

// Tracks commands that expect an acknowledgement and decides when to resend them.
//
// COMMAND_LONG is matched to COMMAND_ACK by command ID; PARAM_EXT_SET to PARAM_EXT_ACK by
// parameter ID. When several are pending with the same key, the oldest is matched first, since
// the payload answers in order. Thread-safe.
class CommandManager {
  public:
    using Message = std::variant<mavlink_command_long_t, mavlink_param_ext_set_t>;

    struct Result {
        uint32_t requestId = 0;
        bool isParam = false;
        uint16_t command = 0;  // for COMMAND_LONG
        std::string paramId;   // for PARAM_EXT_SET
        int32_t result = 0;    // MAV_RESULT / PARAM_ACK, or IMAGO_RESULT_TIMEOUT
        float roundTripMs = 0; // from the latest send to the acknowledgement
    };

    CommandManager(int64_t timeoutNs, int retries);

    // Registers a message and returns its request ID. The caller then sends the message.
    // retries < 0 uses the default. Use 0 for commands that must never run twice (photo).
    uint32_t add(Message& message, int64_t nowNs, int retries = -1);

    std::optional<Result> onCommandAck(const mavlink_command_ack_t& ack, int64_t nowNs);
    std::optional<Result> onParamExtAck(const mavlink_param_ext_ack_t& ack, int64_t nowNs);

    // Appends messages due for a resend and results for commands out of retries.
    void tick(int64_t nowNs, std::vector<Message>& resend, std::vector<Result>& expired);

    // Drops everything pending, e.g. when the link goes down. Returns them as timed out.
    std::vector<Result> clear();

    size_t pending() const;

  private:
    struct Pending {
        uint32_t requestId;
        Message message;
        int64_t sentNs;
        int64_t deadlineNs;
        int attempts;
        int maxRetries;
    };

    static std::string paramIdOf(const mavlink_param_ext_set_t& m);
    static Result resultFor(const Pending& p, int32_t result, int64_t nowNs);

    const int64_t timeoutNs_;
    const int retries_;
    mutable std::mutex mutex_;
    std::deque<Pending> pending_;
    uint32_t nextRequestId_ = 1;
};

} // namespace imago
