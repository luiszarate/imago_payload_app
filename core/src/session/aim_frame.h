#pragma once

#include <atomic>
#include <cmath>

namespace imago {

// The yaw frame angle setpoints are worked out in (fine aim and head tracking by angles).
//
// The Orus L takes an angle's yaw in its own absolute frame (MOUNT_ORIENTATION's yaw_absolute,
// fixed over the ground), and both aimers think in yaw relative to the nose, so the session
// added (yaw_absolute - yaw) to every setpoint, live. On the ground that is the same thing.
// In flight it is not (2026-11-13, a loiter at 3-9 deg/s with the gimbal in lock): the live
// offset turned with the aircraft, so a held target was a direction off the nose, and the
// gimbal, which had been holding a point on the ground, was dragged round with the aircraft
// (22 deg in 2 s at 19x: the point was gone at the first touch of the stick).
//
// So while the gimbal holds its yaw over the ground (YAW_LOCK) and something aims, the offset
// is frozen where the aiming started: the frame stays fixed over the ground, zero being where
// the nose pointed then, and the gimbal's yaw in it comes from yaw_absolute alone (one
// message: no skew between two reports while the aircraft turns). In follow, or with nothing
// aiming, the frame is the nose, as before.
//
// Pure; the session keeps one per aimer on its network thread. rebase() may come from any
// thread.
class AimFrame {
  public:
    struct In {
        bool locked = false;   // the gimbal holds its yaw over the ground (YAW_LOCK)
        bool engaged = false;  // an aimer is using the frame now
        bool yawValid = false; // yaw relative to the nose known
        float yawDeg = 0;
        bool offsetValid = false; // MOUNT_ORIENTATION fresh
        float offsetDeg = 0;      // yaw_absolute - yaw
        float absoluteDeg = 0;    // yaw_absolute
    };
    struct Out {
        bool yawValid = false;
        float yawDeg = 0; // the gimbal's yaw in the frame
        bool offsetValid = false;
        float offsetDeg = 0; // what a yaw in the frame needs added for the gimbal
        bool overGround = false; // the frame is fixed over the ground right now
    };

    Out update(const In& in) {
        Out out;
        out.yawValid = in.yawValid;
        out.yawDeg = in.yawDeg;
        out.offsetValid = in.offsetValid;
        out.offsetDeg = in.offsetDeg;
        const bool rebase = rebase_.exchange(false, std::memory_order_relaxed);
        if (!in.offsetValid) {
            // Nothing to hold on to: start again from the nose when the reports come back.
            frozen_ = false;
            return out;
        }
        if (!in.locked || !in.engaged || rebase || !frozen_) {
            frozenOffset_ = in.offsetDeg;
            frozen_ = in.locked && in.engaged;
            return out; // the nose's frame (also on the tick that freezes: the same thing)
        }
        out.offsetDeg = frozenOffset_;
        out.yawValid = true;
        out.yawDeg = wrap180(in.absoluteDeg - frozenOffset_);
        out.overGround = true;
        return out;
    }

    // The next update starts the frame again from the nose (head tracking's recentre).
    void rebase() { rebase_.store(true, std::memory_order_relaxed); }

  private:
    static float wrap180(float deg) {
        deg = std::fmod(deg + 180.0f, 360.0f);
        if (deg < 0) {
            deg += 360.0f;
        }
        return deg - 180.0f;
    }

    bool frozen_ = false;
    float frozenOffset_ = 0;
    std::atomic<bool> rebase_{false};
};

} // namespace imago
