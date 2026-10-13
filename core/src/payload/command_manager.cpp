#include "payload/command_manager.h"

#include "imago/imago_core.h"

#include <algorithm>
#include <cstring>

namespace imago {

CommandManager::CommandManager(int64_t timeoutNs, int retries)
    : timeoutNs_(timeoutNs), retries_(retries) {}

void CommandManager::setRoundTrip(int64_t roundTripNs) {
    roundTripNs_.store(std::max<int64_t>(roundTripNs, 0), std::memory_order_relaxed);
}

int64_t CommandManager::retryTimeoutNs() const {
    constexpr int64_t kMarginNs = 100'000'000;
    return std::max(timeoutNs_, 2 * roundTripNs_.load(std::memory_order_relaxed) + kMarginNs);
}

int64_t CommandManager::singleAttemptTimeoutNs() const {
    constexpr int64_t kMinimumNs = 1'500'000'000;
    return std::max(kMinimumNs, 3 * retryTimeoutNs());
}

std::string CommandManager::paramIdOf(const mavlink_param_ext_set_t& m) {
    return std::string(m.param_id, strnlen(m.param_id, sizeof(m.param_id)));
}

CommandManager::Result CommandManager::resultFor(const Pending& p, int32_t result, int64_t nowNs) {
    Result r;
    r.requestId = p.requestId;
    r.result = result;
    r.roundTripMs = static_cast<float>(nowNs - p.sentNs) / 1e6f;
    if (const auto* cmd = std::get_if<mavlink_command_long_t>(&p.message)) {
        r.command = cmd->command;
    } else {
        r.isParam = true;
        r.paramId = paramIdOf(std::get<mavlink_param_ext_set_t>(p.message));
    }
    return r;
}

uint32_t CommandManager::add(Message& message, int64_t nowNs, int retries) {
    std::lock_guard lock(mutex_);
    const uint32_t id = nextRequestId_++;
    if (nextRequestId_ == 0) {
        nextRequestId_ = 1;
    }
    if (auto* cmd = std::get_if<mavlink_command_long_t>(&message)) {
        cmd->confirmation = 0;
    }
    const int maxRetries = retries < 0 ? retries_ : retries;
    const int64_t wait = maxRetries == 0 ? singleAttemptTimeoutNs() : retryTimeoutNs();
    pending_.push_back(Pending{id, message, nowNs, nowNs + wait, 1, maxRetries});
    return id;
}

std::optional<CommandManager::Result> CommandManager::onCommandAck(const mavlink_command_ack_t& ack,
                                                                   int64_t nowNs) {
    std::lock_guard lock(mutex_);
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
        const auto* cmd = std::get_if<mavlink_command_long_t>(&it->message);
        if (cmd == nullptr || cmd->command != ack.command) {
            continue;
        }
        if (ack.result == MAV_RESULT_IN_PROGRESS) {
            // Accepted and running: stop resending, keep waiting for the final answer.
            it->deadlineNs = nowNs + retryTimeoutNs() * 10;
            it->attempts = it->maxRetries + 1;
            return std::nullopt;
        }
        Result r = resultFor(*it, ack.result, nowNs);
        pending_.erase(it);
        return r;
    }
    return std::nullopt;
}

std::optional<CommandManager::Result>
CommandManager::onParamExtAck(const mavlink_param_ext_ack_t& ack, int64_t nowNs) {
    const std::string id(ack.param_id, strnlen(ack.param_id, sizeof(ack.param_id)));
    std::lock_guard lock(mutex_);
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
        const auto* set = std::get_if<mavlink_param_ext_set_t>(&it->message);
        if (set == nullptr || paramIdOf(*set) != id) {
            continue;
        }
        if (ack.param_result == PARAM_ACK_IN_PROGRESS) {
            it->deadlineNs = nowNs + retryTimeoutNs() * 10;
            it->attempts = it->maxRetries + 1;
            return std::nullopt;
        }
        Result r = resultFor(*it, ack.param_result, nowNs);
        pending_.erase(it);
        return r;
    }
    return std::nullopt;
}

void CommandManager::tick(int64_t nowNs, std::vector<Message>& resend,
                          std::vector<Result>& expired) {
    std::lock_guard lock(mutex_);
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (nowNs < it->deadlineNs) {
            ++it;
            continue;
        }
        if (it->attempts > it->maxRetries) {
            expired.push_back(resultFor(*it, IMAGO_RESULT_TIMEOUT, nowNs));
            it = pending_.erase(it);
            continue;
        }
        if (auto* cmd = std::get_if<mavlink_command_long_t>(&it->message)) {
            // MAVLink convention: confirmation counts the resends of the same command.
            cmd->confirmation = static_cast<uint8_t>(std::min(it->attempts, 255));
        }
        ++it->attempts;
        it->sentNs = nowNs;
        it->deadlineNs = nowNs + retryTimeoutNs();
        resend.push_back(it->message);
        ++it;
    }
}

std::vector<CommandManager::Result> CommandManager::clear() {
    std::lock_guard lock(mutex_);
    std::vector<Result> results;
    results.reserve(pending_.size());
    for (const auto& p : pending_) {
        results.push_back(resultFor(p, IMAGO_RESULT_TIMEOUT, p.sentNs));
    }
    pending_.clear();
    return results;
}

size_t CommandManager::pending() const {
    std::lock_guard lock(mutex_);
    return pending_.size();
}

} // namespace imago
