// Fine aim: the pure controller (FineAim). The whole core against an orus-sim that ignores
// rates under 2 deg/s and takes angle yaw in an offset frame, like the Orus L, is in
// test_session.cpp.

#include "session/aim_frame.h"
#include "session/fine_aim.h"

#include <doctest.h>

#include <cmath>

using namespace imago;

namespace {

constexpr int64_t kMs = 1'000'000;
constexpr int64_t kTick = 33 * kMs;

FineAim::Attitude at(float pitch, float yaw) {
    FineAim::Attitude a;
    a.valid = true;
    a.pitchDeg = pitch;
    a.yawDeg = yaw;
    return a;
}

FineAim::Attitude withOffset(FineAim::Attitude a, float offset) {
    a.yawOffsetValid = true;
    a.yawOffsetDeg = offset;
    return a;
}

bool isAngle(const FineAim::Command& c) { return c.kind == FineAim::Command::Kind::Angle; }
bool isStop(const FineAim::Command& c) { return c.kind == FineAim::Command::Kind::Stop; }
bool isNone(const FineAim::Command& c) { return c.kind == FineAim::Command::Kind::None; }

} // namespace

TEST_CASE("fine aim: takes over under the floor and lets go past it, with some slack") {
    FineAim aim;
    CHECK_FALSE(aim.takes(0, 0.5f)); // off by default
    aim.setFloor(2);
    CHECK(aim.takes(0, 0.5f));
    CHECK(aim.takes(1.9f, -1.9f));
    CHECK_FALSE(aim.takes(0, 0));     // nothing asked
    CHECK_FALSE(aim.takes(0, 2.2f));  // the gimbal obeys 2 itself
    CHECK_FALSE(aim.takes(0.5f, 3)); // one axis above: rates for both

    // Once aiming, a stick wandering just over the floor stays on angles.
    REQUIRE(isAngle(aim.update(0, 0, 1, at(0, 10))));
    CHECK(aim.takes(0, 2.2f));
    CHECK_FALSE(aim.takes(0, 2.6f)); // past floor * kExitFactor
    aim.setFloor(0);
    CHECK_FALSE(aim.takes(0, 0.5f));
    CHECK_FALSE(aim.engaged());
}

TEST_CASE("fine aim: starts where the gimbal is and moves the target at the rate asked") {
    FineAim aim;
    aim.setFloor(2);
    CHECK(isNone(aim.update(0, 0, 1, FineAim::Attitude{}))); // nothing to start from yet
    CHECK_FALSE(aim.engaged());

    auto c = aim.update(0, 0, 1.5f, at(-10, 20));
    REQUIRE(isAngle(c));
    CHECK(c.pitchDeg == doctest::Approx(-10));
    CHECK(c.yawDeg == doctest::Approx(20)); // no jump at the start
    for (int i = 1; i <= 30; ++i) { // one second at 30 Hz
        c = aim.update(i * kTick, 0, 1.5f, at(-10, 20));
        REQUIRE(isAngle(c));
    }
    CHECK(c.yawDeg == doctest::Approx(20 + 1.5f * 30 * 0.033f).epsilon(0.01));
    CHECK(c.pitchDeg == doctest::Approx(-10));
}

TEST_CASE("fine aim: the yaw sent allows for the gimbal's absolute frame") {
    // The Orus L takes an angle's yaw as MOUNT_ORIENTATION's yaw_absolute: angles landed
    // (yaw_absolute - yaw) off, -2 deg one session and +17 the next (2026-11-09).
    FineAim aim;
    aim.setFloor(2);
    auto c = aim.update(0, 0, 0.5f, withOffset(at(-5, -0.3f), 17.0f));
    REQUIRE(isAngle(c));
    CHECK(c.yawDeg == doctest::Approx(16.7f)); // where it already is, in its own frame
    CHECK(c.pitchDeg == doctest::Approx(-5));  // pitch has no offset

    // The offset may drift; the latest one counts, and stays when a report lacks it.
    c = aim.update(kTick, 0, 0.5f, withOffset(at(-5, -0.3f), 16.0f));
    CHECK(c.yawDeg == doctest::Approx(-0.3f + 0.5f * 0.033f + 16.0f).epsilon(0.001));
    c = aim.update(2 * kTick, 0, 0.5f, at(-5, -0.3f));
    CHECK(c.yawDeg == doctest::Approx(-0.3f + 0.5f * 0.066f + 16.0f).epsilon(0.001));

    // The sum wraps at +-180.
    FineAim wraps;
    wraps.setFloor(2);
    c = wraps.update(0, 0, 0.5f, withOffset(at(0, 170), 17.0f));
    CHECK(c.yawDeg == doctest::Approx(-173.0f));
}

TEST_CASE("fine aim: the target never chases the gimbal") {
    // With the target tied to the reported attitude, a gimbal that landed 18 deg left of each
    // setpoint dragged it along and spun away to one side (2026-11-09).
    FineAim aim;
    aim.setFloor(2);
    float gimbalYaw = 0;
    auto c = aim.update(0, 0, 0.5f, at(0, gimbalYaw));
    float last = c.yawDeg;
    for (int i = 1; i <= 300; ++i) { // 10 s at +0.5 deg/s, offset unknown
        gimbalYaw = c.yawDeg - 18.0f; // lands 18 deg left of what it was told
        c = aim.update(i * kTick, 0, 0.5f, at(0, gimbalYaw));
        REQUIRE(isAngle(c));
        CHECK(c.yawDeg >= last); // only ever the way the stick asks
        last = c.yawDeg;
    }
    CHECK(last == doctest::Approx(0.5f * 300 * 0.033f).epsilon(0.01));
}

TEST_CASE("fine aim: letting go holds the angle, then leaves a stop") {
    // A stop, not an angle, must be the last setpoint: PX4's gimbal manager keeps resending
    // its last one after a release, and an old angle fought direct commands (2026-11-09).
    FineAim aim;
    aim.setFloor(2);
    int64_t now = 0;
    aim.update(now, 0, 1, at(0, 10));
    now += kTick;
    const float held = aim.update(now, 0, 1, at(0, 10)).yawDeg;
    int angles = 0, stops = 0;
    bool stopAfterAngle = true;
    for (int i = 0; i < 200 && aim.engaged(); ++i) {
        now += kTick;
        const auto h = aim.update(now, 0, 0, at(0, held));
        if (isAngle(h)) {
            ++angles;
            stopAfterAngle = stops == 0; // no angle once the stops began
            CHECK(h.yawDeg == doctest::Approx(held));
        }
        stops += isStop(h);
    }
    CHECK(angles * kTick >= FineAim::kHoldNs - kTick); // held long enough to get there
    CHECK(stops == FineAim::kStopRepeats);
    CHECK(stopAfterAngle);
    CHECK_FALSE(aim.engaged());
    CHECK(isNone(aim.update(now + kTick, 0, 0, at(0, held))));
}

TEST_CASE("fine aim: touching the stick again during the hold carries on, no new start") {
    FineAim aim;
    aim.setFloor(2);
    int64_t now = 0;
    aim.update(now, 0, 1, at(0, 10));
    for (int i = 0; i < 15; ++i) {
        now += kTick;
        aim.update(now, 0, 1, at(0, 10.2f));
    }
    now += kTick;
    const auto held = aim.update(now, 0, 0, at(0, 10.2f)); // let go
    REQUIRE(isAngle(held));
    now += kTick;
    // The gimbal reports elsewhere (it lags): the target carries on from itself.
    const auto again = aim.update(now, 0, 1, at(0, held.yawDeg + 3.0f));
    REQUIRE(isAngle(again));
    CHECK(again.yawDeg == doctest::Approx(held.yawDeg + 0.033f).epsilon(0.01));
}

TEST_CASE("fine aim: after fast rates it stops the gimbal and waits for it to settle") {
    FineAim aim;
    aim.setFloor(2);
    int64_t now = 1000 * kMs;
    aim.rateSent(now, 0, 20); // the joystick was past the floor until now
    aim.rateSent(now, 0, 1);  // a rate under the floor does not count: the gimbal ignores it
    now += kTick;
    // The gimbal still coasts: the first ticks stop it instead of aiming from a stale report.
    float yaw = 40;
    int stops = 0;
    FineAim::Command c;
    for (int i = 0; i < 6; ++i) {
        yaw += 5 * 0.033f;
        c = aim.update(now, 0, 1, at(0, yaw));
        stops += isStop(c);
        CHECK_FALSE(isAngle(c));
        now += kTick;
    }
    CHECK(stops == FineAim::kStopRepeats);
    // Settled: it starts from there, plus what the stick asked meanwhile (up to kMaxOwedDeg).
    for (int i = 0; i < 40 && !isAngle(c); ++i) {
        c = aim.update(now, 0, 1, at(0, yaw));
        now += kTick;
    }
    REQUIRE(isAngle(c));
    CHECK(c.yawDeg > yaw);
    CHECK(c.yawDeg <= yaw + FineAim::kMaxOwedDeg);

    // It never waits longer than kMaxWaitNs, even if the reports keep moving.
    FineAim busy;
    busy.setFloor(2);
    now = 1000 * kMs;
    busy.rateSent(now, 0, 20);
    int64_t t = now;
    FineAim::Command b;
    for (yaw = 0; t - now < FineAim::kMaxWaitNs + 4 * kTick && !isAngle(b); t += kTick) {
        yaw += 0.5f;
        b = busy.update(t, 0, 1, at(0, yaw));
    }
    CHECK(isAngle(b));

    // Rates the gimbal ignores leave nothing to wait for.
    FineAim slow;
    slow.setFloor(2);
    slow.rateSent(1000 * kMs, 0, 1);
    CHECK(isAngle(slow.update(1000 * kMs + kTick, 0, 1, at(0, 0))));
}

TEST_CASE("fine aim: limits, a late tick and the angle the Orus L ignores") {
    FineAim aim;
    aim.setFloor(2);
    aim.update(0, 1.9f, 1.9f, at(39.9f, 179.4f));
    auto c = aim.update(kTick * 10, 1.9f, 1.9f, at(39.9f, 179.4f));
    REQUIRE(isAngle(c));
    CHECK(c.pitchDeg <= FineAim::kMaxPitchDeg);
    CHECK(c.yawDeg <= FineAim::kMaxYawDeg);

    // A tick that comes 2 s late moves the target at most 0.1 s worth.
    aim.cancel();
    aim.update(0, 0, 1.9f, at(0, 50));
    c = aim.update(2000 * kMs, 0, 1.9f, at(0, 50));
    REQUIRE(isAngle(c));
    CHECK(c.yawDeg <= 50 + 1.9f * 0.1f + 1e-4f);

    // Exactly 0/0 (the identity quaternion) is nudged: the Orus L ignores it (2026-10-27).
    aim.cancel();
    c = aim.update(0, 0, 1, at(0, 0));
    REQUIRE(isAngle(c));
    CHECK((c.pitchDeg != 0 || c.yawDeg != 0));
}

TEST_CASE("fine aim: a fast rate cancels it outright") {
    FineAim aim;
    aim.setFloor(2);
    aim.update(0, 0, 1, at(0, 10));
    aim.cancel();
    CHECK_FALSE(aim.engaged());
    CHECK(isNone(aim.update(kTick, 0, 0, at(0, 10))));
}

// Flight 2026-11-13: loitering at 3-9 deg/s with the gimbal in lock, a held angle was a
// direction off the nose and dragged the gimbal round with the aircraft.
TEST_CASE("aim frame: fixed over the ground while the gimbal is in lock and something aims") {
    AimFrame frame;
    AimFrame::In in;
    in.yawValid = true;
    in.offsetValid = true;
    // The gimbal holds 100 deg over the ground; the nose is at 70, so it sits 30 right of it.
    in.locked = true;
    in.engaged = true;
    in.yawDeg = 30;
    in.absoluteDeg = 100;
    in.offsetDeg = 70;
    AimFrame::Out out = frame.update(in);
    CHECK(out.yawDeg == doctest::Approx(30));
    CHECK(out.offsetDeg == doctest::Approx(70));

    // The aircraft turns 40 deg right: the gimbal is now 10 left of the nose, and has not
    // moved over the ground. In the frame it has not moved either, and a yaw of 30 in the
    // frame still goes out as 100.
    in.yawDeg = -10;
    in.offsetDeg = 110;
    out = frame.update(in);
    CHECK(out.overGround);
    CHECK(out.yawDeg == doctest::Approx(30));
    CHECK(out.yawDeg + out.offsetDeg == doctest::Approx(100));

    // The gimbal is turned 5 deg right over the ground: 5 deg in the frame.
    in.absoluteDeg = 105;
    in.yawDeg = -5;
    out = frame.update(in);
    CHECK(out.yawDeg == doctest::Approx(35));

    // A rebase (recentre, or a new aim) starts again from the nose.
    frame.rebase();
    out = frame.update(in);
    CHECK(out.yawDeg == doctest::Approx(-5));
    CHECK(out.offsetDeg == doctest::Approx(110));
    in.offsetDeg = 130; // 20 deg more of turn
    in.yawDeg = -25;
    out = frame.update(in);
    CHECK(out.yawDeg == doctest::Approx(-5));
    CHECK(out.offsetDeg == doctest::Approx(110));

    // In follow the frame is the nose, live; so it is with nothing aiming.
    in.locked = false;
    out = frame.update(in);
    CHECK_FALSE(out.overGround);
    CHECK(out.yawDeg == doctest::Approx(-25));
    CHECK(out.offsetDeg == doctest::Approx(130));
    in.locked = true;
    in.engaged = false;
    in.offsetDeg = 150;
    in.yawDeg = -45;
    out = frame.update(in);
    CHECK(out.offsetDeg == doctest::Approx(150));

    // Across the +-180 seam.
    in.engaged = true;
    in.absoluteDeg = 175;
    in.offsetDeg = -170;
    in.yawDeg = -15;
    frame.rebase();
    frame.update(in);
    in.absoluteDeg = -175; // 10 deg further right
    out = frame.update(in);
    CHECK(out.yawDeg == doctest::Approx(-5));

    // Without MOUNT_ORIENTATION there is nothing to hold on to.
    in.offsetValid = false;
    out = frame.update(in);
    CHECK_FALSE(out.offsetValid);
    CHECK_FALSE(out.overGround);
}
