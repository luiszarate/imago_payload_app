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
// Sign learning: the attitude must move this far under a steady command before its direction
// is trusted (the Orus L's status is noisy to a few tenths of a degree).
constexpr float kLearnMoveDeg = 2.0f;
// No-motion fault: a rate of at least this, asked for this long, with the attitude within
// kNoMotionDeg of where it started. At a quarter of the rate asked (the Orus L) 2 deg/s for
// 3 s still moves 1.5 degrees.
constexpr float kNoMotionRateDps = 2.0f;
constexpr int64_t kNoMotionNs = 3'000'000'000;
constexpr float kNoMotionDeg = 0.5f;
// Runaway fault: the attitude went this far the wrong way under a steady command.
constexpr float kRunawayDeg = 15.0f;
// ANGLE mode: the gimbal this far from the setpoint for this long does not follow. A quick
// head turn leaves it behind for a while (angles of 30-45 deg took ~2 s, 2026-10-27).
constexpr float kAngleBehindDeg = 15.0f;
constexpr int64_t kAngleBehindNs = 6'000'000'000;

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
    c.gain_dps_per_deg = orDefault(c.gain_dps_per_deg, 0.6f);
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
    c.zoom_compensation = std::isfinite(c.zoom_compensation) ? std::clamp(c.zoom_compensation, 0.0f, 1.0f) : 0.0f;
    std::lock_guard lock(mu_);
    cfg_ = c;
    if (!pitchAxis_.learned) {
        pitchAxis_.sign = c.invert_pitch ? -1.0f : 1.0f;
    }
    if (!yawAxis_.learned) {
        yawAxis_.sign = c.invert_yaw ? -1.0f : 1.0f;
    }
}

void HeadTracking::resetLoopLocked() {
    filterInit_ = false;
    angleInit_ = false;
    angleBehindNs_ = 0;
    lastAngleNs_ = 0;
    following_ = false;
    heldByTracker_ = false;
    errorDeg_ = 0;
    fault_ = IMAGO_HEAD_TRACK_FAULT_NONE;
    pitchAxis_ = Axis{};
    yawAxis_ = Axis{};
    pitchAxis_.sign = cfg_.invert_pitch ? -1.0f : 1.0f;
    yawAxis_.sign = cfg_.invert_yaw ? -1.0f : 1.0f;
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
    resetLoopLocked();
    if (mode == IMAGO_HEAD_TRACK_OFF) {
        hold_ = false;
        headYawKnown_ = false;
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
    // Also the way out of a fault: the operator asks again, the loop starts afresh (the
    // learnt signs are kept: they were checked against the gimbal).
    fault_ = IMAGO_HEAD_TRACK_FAULT_NONE;
    pitchAxis_.direction = yawAxis_.direction = 0;
    angleBehindNs_ = 0;
}

void HeadTracking::setHold(bool hold) {
    std::lock_guard lock(mu_);
    if (hold == hold_) {
        return;
    }
    hold_ = hold;
    // Either way the head-to-gimbal offset is taken again at the next tick that follows, so
    // letting go never jumps: the head may have turned anywhere meanwhile.
    offsetKnown_ = false;
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
    if (!poseFreshLocked(nowNs) || trackerActive || hold_ || !gimbalKnown || fault_ != IMAGO_HEAD_TRACK_FAULT_NONE) {
        following_ = false;
        filterInit_ = false;
        angleInit_ = false; // the next stream starts from where the gimbal is then
        pitchAxis_.direction = yawAxis_.direction = 0;
        return stopLocked();
    }

    // The head's yaw, followed across +-180.
    if (!headYawKnown_) {
        headYaw_ = poseYaw_;
        headYawKnown_ = true;
    } else {
        headYaw_ += wrap180(poseYaw_ - lastPoseYaw_);
    }
    lastPoseYaw_ = poseYaw_;

    // Zoomed in, a degree of head is less than a degree of gimbal. The offset takes up the
    // change, so zooming never moves the gimbal by itself.
    const float scale = cfg_.zoom_compensation > 0
                            ? std::pow(std::max(1.0f, gimbal.zoom), -cfg_.zoom_compensation)
                            : 1.0f;
    if (offsetKnown_ && scale != scale_) {
        offsetPitch_ += (scale_ - scale) * posePitch_;
        offsetYaw_ += (scale_ - scale) * headYaw_;
    }
    scale_ = scale;

    // Offset: where the head points now is where the gimbal points now (no jump on
    // switching on), or the nose after a recentre; a joystick trims it.
    if (!offsetKnown_) {
        offsetPitch_ = gimbal.pitchDeg - scale * posePitch_;
        offsetYaw_ = (gimbal.yawValid ? gimbal.yawDeg : 0.0f) - scale * headYaw_;
        offsetKnown_ = true;
    }
    if (recentrePending_) {
        offsetPitch_ = -scale * posePitch_;
        offsetYaw_ = -scale * headYaw_;
        recentrePending_ = false;
    }
    offsetPitch_ += trimPitchDps_ * dt;
    offsetYaw_ += trimYawDps_ * dt;

    const float targetPitch =
        std::clamp(scale * posePitch_ + offsetPitch_, cfg_.pitch_min_deg, cfg_.pitch_max_deg);
    const float targetYaw =
        std::clamp(scale * headYaw_ + offsetYaw_, -cfg_.yaw_range_deg, cfg_.yaw_range_deg);
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
            pitchAxis_.direction = yawAxis_.direction = 0;
            return stopLocked();
        }
        // Guards, before the sign: the attitude must move, and the right way.
        if (watchAxis(pitchAxis_, pitchDps, gimbal.pitchDeg, false, nowNs) ||
            watchAxis(yawAxis_, yawDps, gimbal.yawDeg, true, nowNs)) {
            following_ = false;
            return stopLocked();
        }
        commanding_ = true;
        return Command{Command::Kind::Rate, pitchDps * pitchAxis_.sign, yawDps * yawAxis_.sign};
    }

    // ANGLE: an open-loop stream of setpoints, the way the Orus L follows best: angles stepped
    // every 1/30 s were followed exactly and smoothly, while rates start ~0.6 s late and coast
    // ~0.7 s after a stop (ground tests 2026-10-28 and 2026-11-09), which no rate loop of
    // useful gain survives: the first head tracking tests ran past the head's angle to the
    // stops (2026-11-09, 2026-11-10).
    if (!gimbal.anglesSupported) {
        fault_ = IMAGO_HEAD_TRACK_FAULT_NO_ANGLES;
        following_ = false;
        return stopLocked();
    }
    if (gimbal.yawOffsetValid) {
        angleYawOffset_ = gimbal.yawOffsetDeg;
    }
    if (!angleInit_) {
        // From where the gimbal is: the offset was captured against it, so nothing jumps.
        anglePitch_ = gimbal.pitchDeg;
        angleYaw_ = gimbal.yawValid ? gimbal.yawDeg : filteredYaw_;
        angleInit_ = true;
        lastAngleNs_ = 0;
        angleBehindNs_ = 0;
    }
    // The setpoint trails the head with a little slack, so a head held still sends a still
    // angle, and never faster than max_rate_dps, so a quick turn is a ramp and not a jump.
    const float slack = cfg_.deadband_deg / 2;
    const float maxStep = cfg_.max_rate_dps * dt;
    const float wantPitch = std::clamp(anglePitch_, filteredPitch_ - slack, filteredPitch_ + slack);
    const float yawGap = wrap180(filteredYaw_ - angleYaw_);
    const float wantYawStep = yawGap > slack ? yawGap - slack : yawGap < -slack ? yawGap + slack : 0.0f;
    anglePitch_ += std::clamp(wantPitch - anglePitch_, -maxStep, maxStep);
    angleYaw_ = std::clamp(wrap180(angleYaw_ + std::clamp(wantYawStep, -maxStep, maxStep)),
                           -cfg_.yaw_range_deg, cfg_.yaw_range_deg);

    // The gimbal must come to the setpoint: far from it for long means the angles do not
    // reach it (no control of the gimbal manager, motors off).
    const float behind = std::max(std::fabs(anglePitch_ - gimbal.pitchDeg),
                                  gimbal.yawValid ? std::fabs(wrap180(angleYaw_ - gimbal.yawDeg)) : 0.0f);
    if (behind < kAngleBehindDeg) {
        angleBehindNs_ = 0;
    } else if (angleBehindNs_ == 0) {
        angleBehindNs_ = nowNs;
    } else if (nowNs - angleBehindNs_ >= kAngleBehindNs) {
        fault_ = IMAGO_HEAD_TRACK_FAULT_NO_MOTION;
        following_ = false;
        angleInit_ = false;
        return stopLocked();
    }

    // One setpoint per gimbal period while it moves; a refresh now and then while it holds.
    const bool moved = std::fabs(anglePitch_ - lastAnglePitch_) > 0.002f ||
                       std::fabs(wrap180(angleYaw_ - lastAngleYaw_)) > 0.002f;
    const int64_t since = nowNs - lastAngleNs_;
    if (lastAngleNs_ != 0 && !((moved && since >= gimbalPeriodNs) || since >= kAngleRefreshNs)) {
        return {};
    }
    lastAngleNs_ = nowNs;
    lastAnglePitch_ = anglePitch_;
    lastAngleYaw_ = angleYaw_;
    commanding_ = true;
    float pitch = anglePitch_;
    const float yaw = wrap180(angleYaw_ + angleYawOffset_);
    if (std::fabs(pitch) < kIdentityNudgeDeg && std::fabs(yaw) < kIdentityNudgeDeg) {
        pitch = kIdentityNudgeDeg;
    }
    return Command{Command::Kind::Angle, pitch, yaw};
}

bool HeadTracking::watchAxis(Axis& axis, float rateAsked, float measuredDeg, bool wrap, int64_t nowNs) {
    const int direction = rateAsked > 0 ? 1 : rateAsked < 0 ? -1 : 0;
    if (direction != axis.direction) {
        axis.direction = direction;
        axis.startDeg = measuredDeg;
        axis.startNs = nowNs;
        return false;
    }
    if (direction == 0) {
        return false;
    }
    const float moved = wrap ? wrap180(measuredDeg - axis.startDeg) : measuredDeg - axis.startDeg;
    // The error asks the attitude to move in `direction`; the sign is only how to get there.
    const bool wrongWay = (moved > 0) != (direction > 0);
    if (!axis.learned && std::fabs(moved) >= kLearnMoveDeg) {
        // The attitude moved: with the sign in use, did it go the way the error asked?
        if (wrongWay) {
            axis.sign = -axis.sign;
        }
        axis.learned = true;
        axis.startDeg = measuredDeg;
        axis.startNs = nowNs;
        return false;
    }
    if (axis.learned && wrongWay && std::fabs(moved) >= kRunawayDeg) {
        // Away from the target, far: whatever was learnt does not hold (the aircraft yawing
        // under a locked gimbal can fool the learning). Stop, and learn again next time.
        fault_ = IMAGO_HEAD_TRACK_FAULT_RAN_AWAY;
        axis.learned = false;
        return true;
    }
    if (std::fabs(rateAsked) >= kNoMotionRateDps && nowNs - axis.startNs >= kNoMotionNs &&
        std::fabs(moved) < kNoMotionDeg) {
        fault_ = IMAGO_HEAD_TRACK_FAULT_NO_MOTION;
        return true;
    }
    return false;
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
    s.fault = mode_ != IMAGO_HEAD_TRACK_OFF ? fault_ : IMAGO_HEAD_TRACK_FAULT_NONE;
    s.scale = scale_;
    s.held = mode_ != IMAGO_HEAD_TRACK_OFF && hold_;
    s.pitchInverted = pitchAxis_.sign < 0;
    s.yawInverted = yawAxis_.sign < 0;
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
