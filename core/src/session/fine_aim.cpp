#include "session/fine_aim.h"

#include <algorithm>
#include <cmath>

namespace imago {

namespace {

// A late tick (the network thread held up) moves the target at most this much time's worth.
constexpr int64_t kMaxStepNs = 100'000'000;

float wrap180(float deg) {
    deg = std::fmod(deg + 180.0f, 360.0f);
    if (deg < 0) {
        deg += 360.0f;
    }
    return deg - 180.0f;
}

float seconds(int64_t ns) {
    return static_cast<float>(std::clamp<int64_t>(ns, 0, kMaxStepNs)) / 1e9f;
}

} // namespace

void FineAim::setFloor(float floorDps) {
    floorDps_ = std::isfinite(floorDps) && floorDps > 0 ? floorDps : 0.0f;
    if (floorDps_ == 0) {
        cancel();
    }
}

bool FineAim::takes(float pitchDps, float yawDps) const {
    if (floorDps_ <= 0 || (pitchDps == 0 && yawDps == 0)) {
        return false;
    }
    const float limit = phase_ == Phase::Idle ? floorDps_ : floorDps_ * kExitFactor;
    return std::fabs(pitchDps) < limit && std::fabs(yawDps) < limit;
}

void FineAim::rateSent(int64_t nowNs, float pitchDps, float yawDps) {
    // Under the floor the Orus L does not move at all, so nothing coasts.
    if (std::max(std::fabs(pitchDps), std::fabs(yawDps)) >= floorDps_) {
        lastFastNs_ = nowNs;
    }
}

void FineAim::cancel() {
    phase_ = Phase::Idle;
    stopsDue_ = 0;
    clearHistory();
}

void FineAim::clearHistory() {
    historyCount_ = 0;
    historyNext_ = 0;
}

void FineAim::observe(int64_t nowNs, const Attitude& gimbal) {
    if (gimbal.yawOffsetValid) {
        yawOffset_ = gimbal.yawOffsetDeg;
    }
    if (!gimbal.valid) {
        return;
    }
    // The network thread may tick every 5 ms; a sample per gimbal period keeps a second of
    // history (reports come at ~6 Hz anyway, 2026-11-09).
    if (historyCount_ > 0) {
        const int newest = (historyNext_ + static_cast<int>(history_.size()) - 1) %
                           static_cast<int>(history_.size());
        if (nowNs - history_[static_cast<size_t>(newest)].ns < kSampleNs) {
            return;
        }
    }
    history_[static_cast<size_t>(historyNext_)] = {nowNs, gimbal.pitchDeg, gimbal.yawDeg};
    historyNext_ = (historyNext_ + 1) % static_cast<int>(history_.size());
    historyCount_ = std::min(historyCount_ + 1, static_cast<int>(history_.size()));
}

bool FineAim::settled(int64_t nowNs) const {
    if (historyCount_ == 0) {
        return false;
    }
    const int newestIndex = (historyNext_ + static_cast<int>(history_.size()) - 1) %
                            static_cast<int>(history_.size());
    const Sample& newest = history_[static_cast<size_t>(newestIndex)];
    int64_t oldestNs = newest.ns;
    for (int i = 0; i < historyCount_; ++i) {
        const Sample& s = history_[static_cast<size_t>(i)];
        if (nowNs - s.ns > kStillWindowNs) {
            continue;
        }
        oldestNs = std::min(oldestNs, s.ns);
        if (std::fabs(s.pitch - newest.pitch) > kStillDeg ||
            std::fabs(wrap180(s.yaw - newest.yaw)) > kStillDeg) {
            return false;
        }
    }
    // Watched for (nearly) the whole window: one report alone proves nothing.
    return newest.ns - oldestNs >= kStillWindowNs * 8 / 10;
}

void FineAim::start(int64_t nowNs, const Attitude& gimbal, float extraPitch, float extraYaw) {
    phase_ = Phase::Aiming;
    stopsDue_ = 0;
    lastNs_ = nowNs;
    targetPitch_ = std::clamp(gimbal.pitchDeg + std::clamp(extraPitch, -kMaxOwedDeg, kMaxOwedDeg),
                              kMinPitchDeg, kMaxPitchDeg);
    targetYaw_ = std::clamp(wrap180(gimbal.yawDeg + std::clamp(extraYaw, -kMaxOwedDeg, kMaxOwedDeg)),
                            -kMaxYawDeg, kMaxYawDeg);
}

void FineAim::advance(int64_t nowNs, float pitchDps, float yawDps) {
    // Open loop on purpose: tying the target to the reported attitude made it chase a gimbal
    // that landed 18 deg off, and run away (2026-11-09).
    const float dt = seconds(nowNs - lastNs_);
    lastNs_ = nowNs;
    targetPitch_ = std::clamp(targetPitch_ + pitchDps * dt, kMinPitchDeg, kMaxPitchDeg);
    targetYaw_ = std::clamp(wrap180(targetYaw_ + yawDps * dt), -kMaxYawDeg, kMaxYawDeg);
}

FineAim::Command FineAim::angle() const {
    Command c;
    c.kind = Command::Kind::Angle;
    c.pitchDeg = targetPitch_;
    c.yawDeg = wrap180(targetYaw_ + yawOffset_);
    if (std::fabs(c.pitchDeg) < 0.005f && std::fabs(c.yawDeg) < 0.005f) {
        c.pitchDeg = 0.01f; // the Orus L ignores exactly 0/0, the identity quaternion (2026-10-27)
    }
    return c;
}

FineAim::Command FineAim::stopOrNone() {
    Command c;
    if (stopsDue_ > 0) {
        --stopsDue_;
        c.kind = Command::Kind::Stop;
    }
    return c;
}

FineAim::Command FineAim::update(int64_t nowNs, float pitchDps, float yawDps,
                                 const Attitude& gimbal) {
    observe(nowNs, gimbal);
    if (takes(pitchDps, yawDps)) {
        switch (phase_) {
        case Phase::Idle:
            if (!gimbal.valid) {
                return stopOrNone(); // nothing to start from; the gimbal ignores the rate anyway
            }
            if (lastFastNs_ == 0 || nowNs - lastFastNs_ >= kCoastNs) {
                start(nowNs, gimbal, 0, 0);
                return angle();
            }
            // It may still coast from the fast rate: stop it, then aim from where it settles.
            phase_ = Phase::Waiting;
            phaseNs_ = nowNs;
            lastNs_ = nowNs;
            owedPitch_ = owedYaw_ = 0;
            stopsDue_ = kStopRepeats;
            clearHistory();
            observe(nowNs, gimbal);
            return stopOrNone();
        case Phase::Waiting: {
            const float dt = seconds(nowNs - lastNs_);
            lastNs_ = nowNs;
            owedPitch_ += pitchDps * dt;
            owedYaw_ += yawDps * dt;
            if (gimbal.valid && (settled(nowNs) || nowNs - phaseNs_ >= kMaxWaitNs)) {
                start(nowNs, gimbal, owedPitch_, owedYaw_);
                return angle();
            }
            return stopOrNone();
        }
        case Phase::Holding:
            phase_ = Phase::Aiming; // touched again: carry on from the same target
            [[fallthrough]];
        case Phase::Aiming:
            advance(nowNs, pitchDps, yawDps);
            return angle();
        }
    }
    switch (phase_) {
    case Phase::Aiming:
        phase_ = Phase::Holding;
        phaseNs_ = nowNs;
        [[fallthrough]];
    case Phase::Holding: {
        lastNs_ = nowNs;
        const int64_t held = nowNs - phaseNs_;
        if (held < kHoldNs || (!settled(nowNs) && held < kMaxHoldNs)) {
            return angle();
        }
        phase_ = Phase::Idle;
        stopsDue_ = kStopRepeats;
        clearHistory();
        return stopOrNone();
    }
    case Phase::Waiting:
        phase_ = Phase::Idle; // let go before it settled: the stops still go out
        clearHistory();
        return stopOrNone();
    case Phase::Idle:
        return stopOrNone();
    }
    return {};
}

} // namespace imago
