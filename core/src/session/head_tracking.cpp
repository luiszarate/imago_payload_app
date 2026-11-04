#include "session/head_tracking.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace imago {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kRadToDeg = 180.0f / kPi;
constexpr int64_t kNsPerMs = 1'000'000;
// ANGLE mode: a setpoint is repeated this often while the target holds still, in case one
// was lost (an absolute setpoint repeated is harmless).
constexpr int64_t kAngleRefreshNs = 500 * kNsPerMs;
// The Orus L ignores exactly the identity quaternion (docs/protocol/orus-l.md).
constexpr float kIdentityNudgeDeg = 0.01f;
// A tick after a long pause (the app was suspended) must not integrate a huge trim step.
constexpr float kMaxDtS = 0.2f;

float orDefault(float value, float fallback) {
    return std::isfinite(value) && value > 0 ? value : fallback;
}

} // namespace

HeadTracking::HeadTracking() { configure(nullptr); }

void HeadTracking::configure(const imago_head_track_config* cfg) {
    imago_head_track_config c{};
    if (cfg != nullptr) {
        c = *cfg;
    }
    c.gain_dps_per_deg = orDefault(c.gain_dps_per_deg, 3.0f);
    c.max_rate_dps = orDefault(c.max_rate_dps, 90.0f);
    c.deadband_deg = orDefault(c.deadband_deg, 0.7f);
    c.smoothing_ms = c.smoothing_ms > 0 ? c.smoothing_ms : 80;
    if (!std::isfinite(c.pitch_min_deg) || !std::isfinite(c.pitch_max_deg) ||
        (c.pitch_min_deg == 0 && c.pitch_max_deg == 0) || c.pitch_min_deg >= c.pitch_max_deg) {
        c.pitch_min_deg = -90.0f;
        c.pitch_max_deg = 40.0f;
    }
    c.pitch_min_deg = std::clamp(c.pitch_min_deg, -90.0f, 90.0f);
    c.pitch_max_deg = std::clamp(c.pitch_max_deg, -90.0f, 90.0f);
    c.yaw_range_deg = std::clamp(orDefault(c.yaw_range_deg, 150.0f), 1.0f, 180.0f);
    c.pose_timeout_ms = c.pose_timeout_ms > 0 ? c.pose_timeout_ms : 500;
    std::lock_guard lock(mu_);
    cfg_ = c;
}

HeadTracking::Command HeadTracking::setMode(int32_t mode) {
    if (mode != IMAGO_HEAD_TRACK_OFF && mode != IMAGO_HEAD_TRACK_RATE &&
        mode != IMAGO_HEAD_TRACK_ANGLE) {
        return {};
    }
    std::lock_guard lock(mu_);
    if (mode == mode_) {
        return {};
    }
    const bool wasOff = mode_ == IMAGO_HEAD_TRACK_OFF;
    mode_ = mode;
    filterInit_ = false;
    lastAngleNs_ = 0;
    following_ = false;
    heldByTracker_ = false;
    errorDeg_ = 0;
    if (mode == IMAGO_HEAD_TRACK_OFF) {
        offsetKnown_ = false;
        recentrePending_ = false;
        trimPitchDps_ = trimYawDps_ = 0;
    } else if (wasOff) {
        offsetKnown_ = false; // captured against the gimbal at the first tick with a pose
    }
    return stopLocked();
}

int32_t HeadTracking::mode() const {
    std::lock_guard lock(mu_);
    return mode_;
}

void HeadTracking::recentre() {
    std::lock_guard lock(mu_);
    recentrePending_ = true;
}

void HeadTracking::setPose(float yawDeg, float pitchDeg, int64_t timeNs) {
    if (!std::isfinite(yawDeg) || !std::isfinite(pitchDeg)) {
        return;
    }
    std::lock_guard lock(mu_);
    havePose_ = true;
    poseYaw_ = wrap180(yawDeg);
    posePitch_ = std::clamp(pitchDeg, -90.0f, 90.0f);
    poseNs_ = timeNs;
}

void HeadTracking::setTrim(float pitchDps, float yawDps) {
    std::lock_guard lock(mu_);
    trimPitchDps_ = std::isfinite(pitchDps) ? pitchDps : 0;
    trimYawDps_ = std::isfinite(yawDps) ? yawDps : 0;
}

bool HeadTracking::poseFreshLocked(int64_t nowNs) const {
    return havePose_ && nowNs - poseNs_ <= static_cast<int64_t>(cfg_.pose_timeout_ms) * kNsPerMs;
}

HeadTracking::Command HeadTracking::stopLocked() {
    if (!commanding_) {
        return {};
    }
    commanding_ = false;
    return Command{Command::Kind::Stop, 0, 0};
}

HeadTracking::Command HeadTracking::update(int64_t nowNs, const Gimbal& gimbal, bool trackerActive,
                                           int64_t gimbalPeriodNs) {
    std::lock_guard lock(mu_);
    float dt = 0;
    if (lastUpdateNs_ != 0) {
        dt = std::clamp(static_cast<float>(nowNs - lastUpdateNs_) / 1e9f, 0.0f, kMaxDtS);
    }
    lastUpdateNs_ = nowNs;

    if (mode_ == IMAGO_HEAD_TRACK_OFF) {
        return stopLocked();
    }
    heldByTracker_ = trackerActive;
    const bool gimbalKnown = gimbal.valid && (mode_ != IMAGO_HEAD_TRACK_RATE || gimbal.yawValid);
    if (!poseFreshLocked(nowNs) || trackerActive || !gimbalKnown) {
        following_ = false;
        filterInit_ = false;
        return stopLocked();
    }

    // Offset: where the head points now is where the gimbal points now (no jump on
    // switching on), or the nose after a recentre; a joystick trims it.
    if (!offsetKnown_) {
        offsetPitch_ = gimbal.pitchDeg - posePitch_;
        offsetYaw_ = gimbal.yawValid ? wrap180(gimbal.yawDeg - poseYaw_) : -poseYaw_;
        offsetKnown_ = true;
    }
    if (recentrePending_) {
        offsetPitch_ = -posePitch_;
        offsetYaw_ = -poseYaw_;
        recentrePending_ = false;
    }
    offsetPitch_ += trimPitchDps_ * dt;
    offsetYaw_ = wrap180(offsetYaw_ + trimYawDps_ * dt);

    const float targetPitch =
        std::clamp(posePitch_ + offsetPitch_, cfg_.pitch_min_deg, cfg_.pitch_max_deg);
    const float targetYaw =
        std::clamp(wrap180(poseYaw_ + offsetYaw_), -cfg_.yaw_range_deg, cfg_.yaw_range_deg);
    if (!filterInit_) {
        filteredPitch_ = targetPitch;
        filteredYaw_ = targetYaw;
        filterInit_ = true;
    } else {
        const float tau = static_cast<float>(cfg_.smoothing_ms) / 1000.0f;
        const float alpha = 1.0f - std::exp(-dt / tau);
        filteredPitch_ += (targetPitch - filteredPitch_) * alpha;
        filteredYaw_ = wrap180(filteredYaw_ + wrap180(targetYaw - filteredYaw_) * alpha);
    }
    following_ = true;

    const float errorPitch = filteredPitch_ - gimbal.pitchDeg;
    const float errorYaw = gimbal.yawValid ? wrap180(filteredYaw_ - gimbal.yawDeg) : 0;
    errorDeg_ = std::max(std::fabs(errorPitch), std::fabs(errorYaw));

    if (mode_ == IMAGO_HEAD_TRACK_RATE) {
        // Proportional. Within the deadband nothing is sent; outside it the rate is taken
        // against half the deadband, so the gimbal still has some speed when it reaches the
        // edge and crosses into the deadband instead of creeping up to it forever.
        const auto rate = [&](float error) {
            if (std::fabs(error) <= cfg_.deadband_deg) {
                return 0.0f;
            }
            const float magnitude = std::fabs(error) - cfg_.deadband_deg / 2;
            const float r = std::min(magnitude * cfg_.gain_dps_per_deg, cfg_.max_rate_dps);
            return error < 0 ? -r : r;
        };
        const float pitchDps = rate(errorPitch);
        const float yawDps = rate(errorYaw);
        if (pitchDps == 0 && yawDps == 0) {
            return stopLocked();
        }
        commanding_ = true;
        return Command{Command::Kind::Rate, pitchDps, yawDps};
    }

    // ANGLE: a setpoint when the target moved, at the gimbal period, and a refresh otherwise.
    const int64_t since = nowNs - lastAngleNs_;
    const bool moved = std::fabs(filteredPitch_ - lastAnglePitch_) > cfg_.deadband_deg ||
                       std::fabs(wrap180(filteredYaw_ - lastAngleYaw_)) > cfg_.deadband_deg;
    if (lastAngleNs_ != 0 && !((moved && since >= gimbalPeriodNs) || since >= kAngleRefreshNs)) {
        return {};
    }
    lastAngleNs_ = nowNs;
    lastAnglePitch_ = filteredPitch_;
    lastAngleYaw_ = filteredYaw_;
    commanding_ = true;
    float yaw = filteredYaw_;
    if (std::fabs(filteredPitch_) < kIdentityNudgeDeg && std::fabs(yaw) < kIdentityNudgeDeg) {
        yaw = kIdentityNudgeDeg;
    }
    return Command{Command::Kind::Angle, filteredPitch_, yaw};
}

HeadTracking::Status HeadTracking::status(int64_t nowNs) const {
    std::lock_guard lock(mu_);
    Status s;
    s.mode = mode_;
    s.poseFresh = poseFreshLocked(nowNs);
    s.following = mode_ != IMAGO_HEAD_TRACK_OFF && following_;
    s.heldByTracker = mode_ != IMAGO_HEAD_TRACK_OFF && heldByTracker_;
    s.targetPitchDeg = filteredPitch_;
    s.targetYawDeg = filteredYaw_;
    s.errorDeg = s.following ? errorDeg_ : 0;
    return s;
}

float HeadTracking::wrap180(float deg) {
    deg = std::fmod(deg + 180.0f, 360.0f);
    if (deg < 0) {
        deg += 360.0f;
    }
    return deg - 180.0f;
}

void HeadTracking::anglesFromQuaternion(float w, float x, float y, float z, float& yawDeg,
                                        float& pitchDeg) {
    const float norm = std::sqrt(w * w + x * x + y * y + z * z);
    if (!std::isfinite(norm) || norm < 1e-6f) {
        yawDeg = pitchDeg = 0;
        return;
    }
    w /= norm;
    x /= norm;
    y /= norm;
    z /= norm;
    // The look direction is the rotated -Z axis: minus the third column of the rotation matrix.
    const float dx = -2.0f * (x * z + w * y);
    const float dy = 2.0f * (w * x - y * z);
    const float dz = 2.0f * (x * x + y * y) - 1.0f;
    yawDeg = std::atan2(dx, -dz) * kRadToDeg; // right positive, 0 straight ahead (-Z)
    pitchDeg = std::asin(std::clamp(dy, -1.0f, 1.0f)) * kRadToDeg;
}

} // namespace imago
