#pragma once

#include <array>
#include <cstdint>

namespace imago {

// Turns joystick rates under the gimbal's floor into a moving angle target
// (imago_gimbal_set_fine_aim in imago_core.h).
//
// The Orus L ignores rates under about 2 deg/s (ground test 2026-11-09: 1.5 did not move it, 2
// gave 2, at 1x and at 10x), so a zoomed-in joystick had a dead stretch. Pulsing 2 deg/s and 0
// gave 20 to 140 % of the rate asked, in visible jerks; angle setpoints stepped every 1/30 s
// followed 1, 0.5 and 0.25 deg/s exactly and smoothly (same day, docs/protocol/orus-l.md).
//
// The field tests of that afternoon shaped the rest:
// - The Orus L takes an angle's yaw in its own absolute frame, MOUNT_ORIENTATION's
//   yaw_absolute, not to the nose: angles landed 1.5-2.2 deg off one session and 18 the next,
//   always by (yaw_absolute - yaw). The yaw sent is the target plus that offset.
// - The target once stayed within 2 deg of the reported attitude. With the 18 deg offset that
//   made it chase the gimbal, which ran off to one side; the target is now open loop.
// - Starting from the reported attitude while it still coasted from a fast rate (it keeps going
//   ~0.7 s) pulled it back. After fast rates it is stopped first and aimed once it settles.
// - A stick near the floor flipped between rates and angles; leaving takes kExitFactor more.
// - PX4's gimbal manager keeps resending its last setpoint after a release, and an old angle
//   fought direct commands. Letting go holds the angle, then leaves a stop.
//
// Pure and unlocked: Session keeps it under its gimbal mutex. The network thread calls update()
// every gimbal period and sends what it returns.
class FineAim {
  public:
    struct Attitude {
        bool valid = false; // pitch, and yaw relative to the nose, known
        float pitchDeg = 0;
        float yawDeg = 0;
        // yaw_absolute - yaw from MOUNT_ORIENTATION: what to add to a nose yaw for the gimbal.
        bool yawOffsetValid = false;
        float yawOffsetDeg = 0;
    };

    struct Command {
        enum class Kind { None, Angle, Stop } kind = Kind::None;
        float pitchDeg = 0; // Angle: the setpoint to send, in the gimbal's frame
        float yawDeg = 0;
    };

    // Rates under floorDps (both axes) become angles; 0 turns it off.
    void setFloor(float floorDps);
    float floor() const { return floorDps_; }
    // Whether a joystick rate is fine aim's: some axis moving, every axis under the floor, or
    // under floor * kExitFactor once it aims.
    bool takes(float pitchDps, float yawDps) const;
    // Something still to send or watch: aiming, waiting, holding, or stops due.
    bool engaged() const { return phase_ != Phase::Idle || stopsDue_ > 0; }
    // A rate went out as a rate: one the gimbal obeys may leave it coasting for a while.
    void rateSent(int64_t nowNs, float pitchDps, float yawDps);
    // Rates took over: nothing is held or stopped.
    void cancel();
    // One tick with the joystick's rate and the gimbal's reported attitude.
    Command update(int64_t nowNs, float pitchDps, float yawDps, const Attitude& gimbal);

    static constexpr float kExitFactor = 1.25f;
    // Letting go holds the angle at least this long (the gimbal lags ~0.5-0.7 s) and at most
    // kMaxHoldNs while it still moves, then sends kStopRepeats stops.
    static constexpr int64_t kHoldNs = 1'200'000'000;
    static constexpr int64_t kMaxHoldNs = 2'500'000'000;
    static constexpr int kStopRepeats = 3;
    // After a fast rate: how long it may coast, and the longest wait for it to settle.
    static constexpr int64_t kCoastNs = 1'500'000'000;
    static constexpr int64_t kMaxWaitNs = 1'500'000'000;
    // What the stick asks while waiting is added to the start, up to this much.
    static constexpr float kMaxOwedDeg = 2.0f;
    // Still: no report in the last kStillWindowNs further than kStillDeg from the newest.
    static constexpr int64_t kStillWindowNs = 500'000'000;
    static constexpr float kStillDeg = 0.3f;
    static constexpr int64_t kSampleNs = 30'000'000; // history: at most one report per this
    static constexpr float kMinPitchDeg = -90.0f; // quaternion setpoints end at the nadir
    static constexpr float kMaxPitchDeg = 40.0f;  // the Orus L tilts up to +40
    static constexpr float kMaxYawDeg = 179.5f;   // angle setpoints wrap at +-180

  private:
    enum class Phase { Idle, Waiting, Aiming, Holding };

    void observe(int64_t nowNs, const Attitude& gimbal);
    void clearHistory();
    // Reports covering the still window, none moving.
    bool settled(int64_t nowNs) const;
    void start(int64_t nowNs, const Attitude& gimbal, float extraPitch, float extraYaw);
    void advance(int64_t nowNs, float pitchDps, float yawDps);
    Command angle() const;
    Command stopOrNone();

    float floorDps_ = 0;
    Phase phase_ = Phase::Idle;
    float targetPitch_ = 0; // yaw to the nose, as the gimbal reports it
    float targetYaw_ = 0;
    float yawOffset_ = 0;   // the latest reported, kept while reports lack it
    int64_t lastNs_ = 0;
    int64_t phaseNs_ = 0;   // when the wait or the hold began
    float owedPitch_ = 0;   // what the stick asked while waiting
    float owedYaw_ = 0;
    int stopsDue_ = 0;
    int64_t lastFastNs_ = 0;

    struct Sample {
        int64_t ns;
        float pitch;
        float yaw;
    };
    std::array<Sample, 32> history_{}; // about a second at kSampleNs
    int historyCount_ = 0;
    int historyNext_ = 0;
};

} // namespace imago
