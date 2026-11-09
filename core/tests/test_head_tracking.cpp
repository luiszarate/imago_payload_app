// Head tracking: the pure controller (HeadTracking) and the whole core against orus-sim.

#include "imago/imago_core.h"
#include "integration_helpers.h"
#include "orus_simulator.h"
#include "session/head_tracking.h"

#include <doctest.h>

#include <chrono>
#include <cmath>
#include <thread>

using namespace imago;
using namespace imago::test;

namespace {

constexpr int64_t kMs = 1'000'000;
constexpr int64_t kPeriod = 33 * kMs;
constexpr float kDegToRad = 3.14159265f / 180.0f;

// OpenXR orientation: a rotation of `deg` about the +Y axis (left positive) then about +X.
void quaternionYawPitch(float yawRightDeg, float pitchUpDeg, float q[4]) {
    // q = qYaw * qPitch, rotating about the body's X after the yaw.
    const float hy = -yawRightDeg * kDegToRad / 2;
    const float hp = pitchUpDeg * kDegToRad / 2;
    const float cy = std::cos(hy), sy = std::sin(hy), cp = std::cos(hp), sp = std::sin(hp);
    // (w, x, y, z) of qYaw = (cy, 0, sy, 0), qPitch = (cp, sp, 0, 0)
    q[0] = cy * cp;
    q[1] = cy * sp;
    q[2] = sy * cp;
    q[3] = -sy * sp;
}

HeadTracking::Gimbal gimbalAt(float pitch, float yaw) {
    HeadTracking::Gimbal g;
    g.valid = true;
    g.pitchDeg = pitch;
    g.yawValid = true;
    g.yawDeg = yaw;
    return g;
}

} // namespace

TEST_CASE("head tracking: look direction from an OpenXR quaternion") {
    float yaw = 1, pitch = 1;
    HeadTracking::anglesFromQuaternion(1, 0, 0, 0, yaw, pitch);
    CHECK(yaw == doctest::Approx(0));
    CHECK(pitch == doctest::Approx(0));

    float q[4];
    quaternionYawPitch(30, 0, q);
    HeadTracking::anglesFromQuaternion(q[0], q[1], q[2], q[3], yaw, pitch);
    CHECK(yaw == doctest::Approx(30).epsilon(0.001));
    CHECK(pitch == doctest::Approx(0).epsilon(0.001));

    quaternionYawPitch(0, 20, q);
    HeadTracking::anglesFromQuaternion(q[0], q[1], q[2], q[3], yaw, pitch);
    CHECK(yaw == doctest::Approx(0).epsilon(0.001));
    CHECK(pitch == doctest::Approx(20).epsilon(0.001));

    quaternionYawPitch(-120, -45, q);
    HeadTracking::anglesFromQuaternion(q[0], q[1], q[2], q[3], yaw, pitch);
    CHECK(yaw == doctest::Approx(-120).epsilon(0.001));
    CHECK(pitch == doctest::Approx(-45).epsilon(0.001));

    // Not normalized: the same direction. Zero: straight ahead, no NaN.
    HeadTracking::anglesFromQuaternion(2 * q[0], 2 * q[1], 2 * q[2], 2 * q[3], yaw, pitch);
    CHECK(yaw == doctest::Approx(-120).epsilon(0.001));
    HeadTracking::anglesFromQuaternion(0, 0, 0, 0, yaw, pitch);
    CHECK(yaw == 0);
    CHECK(pitch == 0);
    imago_head_track_angles_from_quaternion(1, 0, 0, 0, nullptr, nullptr); // tolerated

    CHECK(HeadTracking::wrap180(190) == doctest::Approx(-170));
    CHECK(HeadTracking::wrap180(-190) == doctest::Approx(170));
    CHECK(HeadTracking::wrap180(360) == doctest::Approx(0));
}

TEST_CASE("head tracking controller: off sends nothing, on captures the offset, then rates") {
    HeadTracking h;
    int64_t now = 1'000 * kMs;
    CHECK(h.update(now, gimbalAt(0, 0), false, kPeriod).kind == HeadTracking::Command::Kind::None);

    // Switching on with the gimbal at pitch -20, yaw 50 and the head straight ahead: no jump.
    CHECK(h.setMode(IMAGO_HEAD_TRACK_RATE).kind == HeadTracking::Command::Kind::None);
    h.setPose(0, 0, now);
    auto cmd = h.update(now, gimbalAt(-20, 50), false, kPeriod);
    CHECK(cmd.kind == HeadTracking::Command::Kind::None);
    auto st = h.status(now);
    CHECK(st.following);
    CHECK(st.targetPitchDeg == doctest::Approx(-20));
    CHECK(st.targetYawDeg == doctest::Approx(50));

    // The head turns 10 deg right: a yaw rate to the right, proportional, pitch untouched.
    // The filter (80 ms) needs some ticks to reach the target.
    for (int i = 0; i < 200; ++i) {
        now += 5 * kMs;
        h.setPose(10, 0, now);
        cmd = h.update(now, gimbalAt(-20, 50), false, kPeriod);
    }
    CHECK(cmd.kind == HeadTracking::Command::Kind::Rate);
    CHECK(cmd.pitch == doctest::Approx(0));
    CHECK(cmd.yaw == doctest::Approx((10 - 0.35f) * 3.0f).epsilon(0.02)); // half deadband out, gain 3
    CHECK(h.status(now).errorDeg == doctest::Approx(10).epsilon(0.02));

    // The gimbal arrives: within the deadband a stop, once; then nothing.
    now += 5 * kMs;
    h.setPose(10, 0, now);
    cmd = h.update(now, gimbalAt(-20, 59.5f), false, kPeriod);
    CHECK(cmd.kind == HeadTracking::Command::Kind::Stop);
    now += 5 * kMs;
    h.setPose(10, 0, now);
    CHECK(h.update(now, gimbalAt(-20, 59.5f), false, kPeriod).kind == HeadTracking::Command::Kind::None);

    // Saturation: a large error asks at most max_rate_dps.
    for (int i = 0; i < 100; ++i) {
        now += 5 * kMs;
        h.setPose(-80, -60, now);
        cmd = h.update(now, gimbalAt(-20, 60), false, kPeriod);
    }
    CHECK(cmd.kind == HeadTracking::Command::Kind::Rate);
    CHECK(cmd.yaw == doctest::Approx(-90));
    CHECK(cmd.pitch == doctest::Approx(-90));

    // Off: a stop, and the joystick path is the app's again.
    CHECK(h.setMode(IMAGO_HEAD_TRACK_OFF).kind == HeadTracking::Command::Kind::Stop);
    CHECK(h.update(now + kPeriod, gimbalAt(0, 0), false, kPeriod).kind == HeadTracking::Command::Kind::None);
    CHECK_FALSE(h.status(now).following);
}

TEST_CASE("head tracking controller: pose timeout, tracker hold, unknown yaw, recentre, trim") {
    HeadTracking h;
    int64_t now = 1'000 * kMs;
    h.setMode(IMAGO_HEAD_TRACK_RATE);
    h.setPose(0, 0, now);
    h.update(now, gimbalAt(0, 0), false, kPeriod);
    for (int i = 0; i < 50; ++i) {
        now += 5 * kMs;
        h.setPose(20, 0, now);
        h.update(now, gimbalAt(0, 0), false, kPeriod);
    }
    auto cmd = h.update(now, gimbalAt(0, 0), false, kPeriod);
    REQUIRE(cmd.kind == HeadTracking::Command::Kind::Rate);

    SUBCASE("no pose for 500 ms: stop, resume with the next pose") {
        now += 600 * kMs;
        cmd = h.update(now, gimbalAt(0, 0), false, kPeriod);
        CHECK(cmd.kind == HeadTracking::Command::Kind::Stop);
        CHECK_FALSE(h.status(now).poseFresh);
        CHECK_FALSE(h.status(now).following);
        CHECK(h.status(now).mode == IMAGO_HEAD_TRACK_RATE);
        h.setPose(20, 0, now);
        cmd = h.update(now, gimbalAt(0, 0), false, kPeriod);
        CHECK(cmd.kind == HeadTracking::Command::Kind::Rate);
        CHECK(h.status(now).following);
    }

    SUBCASE("the payload's tracker holds it") {
        now += 5 * kMs;
        h.setPose(20, 0, now);
        cmd = h.update(now, gimbalAt(0, 0), true, kPeriod);
        CHECK(cmd.kind == HeadTracking::Command::Kind::Stop);
        CHECK(h.status(now).heldByTracker);
        CHECK_FALSE(h.status(now).following);
        now += 5 * kMs;
        h.setPose(20, 0, now);
        CHECK(h.update(now, gimbalAt(0, 0), true, kPeriod).kind == HeadTracking::Command::Kind::None);
        cmd = h.update(now, gimbalAt(0, 0), false, kPeriod);
        CHECK(cmd.kind == HeadTracking::Command::Kind::Rate);
        CHECK_FALSE(h.status(now).heldByTracker);
    }

    SUBCASE("yaw against the nose unknown: nothing in RATE mode") {
        HeadTracking::Gimbal g = gimbalAt(0, 0);
        g.yawValid = false;
        now += 5 * kMs;
        h.setPose(20, 0, now);
        CHECK(h.update(now, g, false, kPeriod).kind == HeadTracking::Command::Kind::Stop);
        CHECK_FALSE(h.status(now).following);
    }

    SUBCASE("recentre: the head's direction becomes the nose") {
        h.recentre();
        for (int i = 0; i < 200; ++i) {
            now += 5 * kMs;
            h.setPose(20, 0, now);
            cmd = h.update(now, gimbalAt(0, 0), false, kPeriod);
        }
        CHECK(cmd.kind == HeadTracking::Command::Kind::None);
        CHECK(h.status(now).targetYawDeg == doctest::Approx(0).epsilon(0.01));
        for (int i = 0; i < 200; ++i) {
            now += 5 * kMs;
            h.setPose(25, 0, now);
            cmd = h.update(now, gimbalAt(0, 0), false, kPeriod);
        }
        CHECK(h.status(now).targetYawDeg == doctest::Approx(5).epsilon(0.01));
    }

    SUBCASE("a joystick trims the offset at its rate") {
        h.recentre();
        now += 5 * kMs;
        h.setPose(20, 0, now);
        h.update(now, gimbalAt(0, 0), false, kPeriod);
        h.setTrim(0, 10); // 10 deg/s to the right
        for (int i = 0; i < 200; ++i) { // 1 s
            now += 5 * kMs;
            h.setPose(20, 0, now);
            h.update(now, gimbalAt(0, 0), false, kPeriod);
        }
        h.setTrim(0, 0);
        for (int i = 0; i < 100; ++i) { // the filter settles
            now += 5 * kMs;
            h.setPose(20, 0, now);
            h.update(now, gimbalAt(0, 0), false, kPeriod);
        }
        CHECK(h.status(now).targetYawDeg == doctest::Approx(10).epsilon(0.03));
    }

    SUBCASE("limits: pitch within the tilt travel, yaw within the range") {
        imago_head_track_config cfg;
        imago_head_track_config_init(&cfg);
        cfg.yaw_range_deg = 60;
        h.configure(&cfg);
        h.recentre(); // applied at the next tick, against the pose of that moment (20, 0)
        now += 5 * kMs;
        h.setPose(20, 0, now);
        h.update(now, gimbalAt(0, 0), false, kPeriod);
        for (int i = 0; i < 200; ++i) {
            now += 5 * kMs;
            h.setPose(150, 80, now);
            h.update(now, gimbalAt(0, 0), false, kPeriod);
        }
        CHECK(h.status(now).targetYawDeg == doctest::Approx(60).epsilon(0.01));
        CHECK(h.status(now).targetPitchDeg == doctest::Approx(40).epsilon(0.01));
        for (int i = 0; i < 200; ++i) {
            now += 5 * kMs;
            h.setPose(0, -89, now);
            h.update(now, gimbalAt(0, 0), false, kPeriod);
        }
        CHECK(h.status(now).targetPitchDeg == doctest::Approx(-89).epsilon(0.01));
    }
}

TEST_CASE("head tracking controller: ANGLE mode paces setpoints and never sends 0/0") {
    HeadTracking h;
    int64_t now = 1'000 * kMs;
    h.setMode(IMAGO_HEAD_TRACK_ANGLE);
    h.recentre();
    h.setPose(0, 0, now);
    HeadTracking::Gimbal g = gimbalAt(5, 5);
    g.yawValid = false; // ANGLE mode works without the yaw frame
    auto cmd = h.update(now, g, false, kPeriod);
    REQUIRE(cmd.kind == HeadTracking::Command::Kind::Angle);
    CHECK(cmd.pitch == doctest::Approx(0));
    CHECK(cmd.yaw == doctest::Approx(0.01f)); // the identity quaternion is ignored by the Orus L

    // Still: nothing until the 500 ms refresh.
    int sent = 0;
    for (int i = 0; i < 80; ++i) { // 400 ms
        now += 5 * kMs;
        h.setPose(0, 0, now);
        if (h.update(now, g, false, kPeriod).kind == HeadTracking::Command::Kind::Angle) ++sent;
    }
    CHECK(sent == 0);
    for (int i = 0; i < 40; ++i) { // 200 ms more
        now += 5 * kMs;
        h.setPose(0, 0, now);
        if (h.update(now, g, false, kPeriod).kind == HeadTracking::Command::Kind::Angle) ++sent;
    }
    CHECK(sent == 1);

    // Moving: one setpoint per gimbal period, following the smoothed pose.
    sent = 0;
    for (int i = 0; i < 100; ++i) { // 500 ms
        now += 5 * kMs;
        h.setPose(30, -10, now);
        cmd = h.update(now, g, false, kPeriod);
        if (cmd.kind == HeadTracking::Command::Kind::Angle) ++sent;
    }
    CHECK(sent >= 8);
    CHECK(sent <= 16);
    CHECK(h.status(now).targetYawDeg == doctest::Approx(30).epsilon(0.01));
    CHECK(h.status(now).targetPitchDeg == doctest::Approx(-10).epsilon(0.01));
    CHECK(h.setMode(IMAGO_HEAD_TRACK_OFF).kind == HeadTracking::Command::Kind::Stop);
}

namespace {

// A gimbal that integrates the rates it is given, with its own sign and speed; the attitude
// status a real one reports is updated at every tick.
struct FakeGimbal {
    float sign = 1;
    float scale = 1;
    float pitch = 0, yaw = 0;
    void apply(const HeadTracking::Command& cmd, float dtS) {
        if (cmd.kind == HeadTracking::Command::Kind::Rate) {
            pitch += cmd.pitch * sign * scale * dtS;
            yaw = HeadTracking::wrap180(yaw + cmd.yaw * sign * scale * dtS);
        }
    }
    HeadTracking::Gimbal state() const { return gimbalAt(pitch, yaw); }
};

// Runs the loop with the head at (yaw, pitch) for `ticks` of 5 ms; returns the last command.
HeadTracking::Command run(HeadTracking& h, FakeGimbal& g, int64_t& now, float yaw, float pitch, int ticks) {
    HeadTracking::Command cmd;
    for (int i = 0; i < ticks; ++i) {
        now += 5 * kMs;
        h.setPose(yaw, pitch, now);
        cmd = h.update(now, g.state(), false, kPeriod);
        g.apply(cmd, 0.005f);
    }
    return cmd;
}

} // namespace

TEST_CASE("head tracking controller: a gimbal that turns the other way is followed anyway") {
    HeadTracking h;
    FakeGimbal g;
    g.sign = -1; // positive rate, attitude goes down and left: what the Orus L looked like
    int64_t now = 1'000 * kMs;
    h.setMode(IMAGO_HEAD_TRACK_RATE);
    run(h, g, now, 0, 0, 10); // offset captured at the nose
    CHECK_FALSE(h.status(now).yawInverted);

    // The head turns 30 deg right: the first two degrees go the wrong way, then the sign
    // flips and the gimbal arrives.
    run(h, g, now, 30, 0, 1200); // 6 s
    auto st = h.status(now);
    CHECK(st.yawInverted);
    CHECK_FALSE(st.pitchInverted); // untouched: no pitch asked yet
    CHECK(st.fault == IMAGO_HEAD_TRACK_FAULT_NONE);
    CHECK(g.yaw == doctest::Approx(30).epsilon(0.03));
    CHECK(st.following);

    // Pitch learns on its own axis.
    run(h, g, now, 30, -20, 1200);
    st = h.status(now);
    CHECK(st.pitchInverted);
    CHECK(g.pitch == doctest::Approx(-20).epsilon(0.05));

    // Off and on again: back to the configured signs, learnt again.
    h.setMode(IMAGO_HEAD_TRACK_OFF);
    h.setMode(IMAGO_HEAD_TRACK_RATE);
    CHECK_FALSE(h.status(now).yawInverted);
    run(h, g, now, 30, -20, 10);
    run(h, g, now, -10, -20, 1200);
    CHECK(h.status(now).yawInverted);
    CHECK(g.yaw == doctest::Approx(-10).epsilon(0.1));
}

TEST_CASE("head tracking controller: invert_* starts the other way, a normal gimbal still works") {
    HeadTracking h;
    imago_head_track_config cfg;
    imago_head_track_config_init(&cfg);
    cfg.invert_yaw = true;
    h.configure(&cfg);
    FakeGimbal g;
    int64_t now = 1'000 * kMs;
    h.setMode(IMAGO_HEAD_TRACK_RATE);
    CHECK(h.status(now).yawInverted);
    run(h, g, now, 0, 0, 10);
    run(h, g, now, 40, 0, 1200);
    CHECK_FALSE(h.status(now).yawInverted); // learnt back
    CHECK(g.yaw == doctest::Approx(40).epsilon(0.03));
}

TEST_CASE("head tracking controller: an attitude that never moves stops the loop") {
    HeadTracking h;
    FakeGimbal g;
    g.scale = 0; // commands reach nothing
    int64_t now = 1'000 * kMs;
    h.setMode(IMAGO_HEAD_TRACK_RATE);
    run(h, g, now, 0, 0, 10);
    auto cmd = run(h, g, now, 20, 0, 400); // 2 s: still trying
    CHECK(cmd.kind == HeadTracking::Command::Kind::Rate);
    CHECK(h.status(now).fault == IMAGO_HEAD_TRACK_FAULT_NONE);
    bool stopped = false;
    for (int i = 0; i < 400 && !stopped; ++i) { // within the next 2 s
        cmd = run(h, g, now, 20, 0, 1);
        stopped = cmd.kind == HeadTracking::Command::Kind::Stop;
    }
    CHECK(stopped);
    auto st = h.status(now);
    CHECK(st.fault == IMAGO_HEAD_TRACK_FAULT_NO_MOTION);
    CHECK_FALSE(st.following);
    CHECK(st.mode == IMAGO_HEAD_TRACK_RATE);
    // Nothing more is sent until the operator asks again.
    cmd = run(h, g, now, 20, 0, 200);
    CHECK(cmd.kind == HeadTracking::Command::Kind::None);
    h.recentre();
    cmd = run(h, g, now, 20, 0, 5);
    CHECK(h.status(now).fault == IMAGO_HEAD_TRACK_FAULT_NONE);
    g.scale = 1;
    run(h, g, now, 20, 0, 5);
    run(h, g, now, 40, 0, 1200); // the head is the nose again after the recentre: 20 deg
    CHECK(g.yaw == doctest::Approx(20).epsilon(0.05));
}

TEST_CASE("head tracking controller: a gimbal that runs away after learning is stopped") {
    HeadTracking h;
    FakeGimbal g;
    int64_t now = 1'000 * kMs;
    h.setMode(IMAGO_HEAD_TRACK_RATE);
    run(h, g, now, 0, 0, 10);
    run(h, g, now, 30, 0, 1200); // learnt and arrived
    REQUIRE(g.yaw == doctest::Approx(30).epsilon(0.03));
    // Then the gimbal's response reverses (as if the aircraft swung under a locked gimbal).
    g.sign = -1;
    bool faulted = false;
    for (int i = 0; i < 2000 && !faulted; ++i) {
        run(h, g, now, 60, 0, 1);
        faulted = h.status(now).fault == IMAGO_HEAD_TRACK_FAULT_RAN_AWAY;
    }
    CHECK(faulted);
    CHECK(std::fabs(g.yaw - 30) < 20); // stopped well before the pan stop
    // A fast head turn while following is never a fault.
    h.recentre();
    g.sign = 1;
    run(h, g, now, 60, 0, 10);
    run(h, g, now, -80, 20, 400);
    run(h, g, now, 70, -40, 400);
    CHECK(h.status(now).fault == IMAGO_HEAD_TRACK_FAULT_NONE);
}

// ----------------------------------------------------------------------------------------------
// Against the simulator

namespace {

struct HeadFixture {
    sim::OrusSimulator simulator;
    EventLog events;
    imago_session* session = nullptr;

    explicit HeadFixture(float rateScale = 1.0f, float rateSign = 1.0f) {
        sim::OrusSimulator::Options simOptions;
        simOptions.port = 0;
        simOptions.gimbalRateScale = rateScale;
        simOptions.gimbalRateSign = rateSign;
        std::string error;
        REQUIRE(simulator.start(simOptions, &error));
        imago_config cfg;
        imago_config_init(&cfg);
        cfg.payload_ip = "127.0.0.1";
        cfg.payload_port = simulator.port();
        cfg.local_port = freeUdpPort();
        cfg.auto_start_video = false;
        session = imago_session_create(&cfg, &EventLog::callback, nullptr, &events);
        REQUIRE(session != nullptr);
        REQUIRE(imago_session_start(session) == 0);
        REQUIRE(waitFor([&] { return state().gimbal_valid && state().gimbal_yaw_valid; }, 2000));
    }

    ~HeadFixture() {
        imago_session_destroy(session);
        simulator.stop();
    }

    imago_state state() const {
        imago_state s;
        imago_session_get_state(session, &s);
        return s;
    }

    // Switches on with the head straight ahead and makes that the nose: the recentre takes
    // the pose of the tick it is applied on, so the pose must be there first.
    void switchOn(imago_head_track_mode mode) {
        imago_head_track_set_mode(session, mode);
        REQUIRE(waitFor([&] {
            imago_head_track_pose(session, 0, 0, 0);
            return state().head_track_following;
        }, 1000));
        imago_head_track_recentre(session);
        for (int i = 0; i < 5; ++i) {
            imago_head_track_pose(session, 0, 0, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    // Keeps feeding the head pose while waiting for the gimbal to get there.
    bool follow(float yawDeg, float pitchDeg, int timeoutMs, float toleranceDeg = 1.0f) {
        return waitFor(
            [&] {
                imago_head_track_pose(session, yawDeg, pitchDeg, 0);
                const imago_state s = state();
                return std::fabs(HeadTracking::wrap180(s.gimbal_yaw_deg - yawDeg)) < toleranceDeg &&
                       std::fabs(s.gimbal_pitch_deg - pitchDeg) < toleranceDeg;
            },
            timeoutMs);
    }
};

} // namespace

TEST_CASE("session: the gimbal follows the head in closed loop, by rates") {
    HeadFixture f;
    f.switchOn(IMAGO_HEAD_TRACK_RATE);
    CHECK(f.state().head_track_mode == IMAGO_HEAD_TRACK_RATE);

    // Head straight ahead with the gimbal at the nose: nothing to do.
    f.simulator.clearReceived();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK(f.simulator.received().gimbal.empty());

    // Look up and to the right: the simulated gimbal gets there.
    REQUIRE(f.follow(40, 15, 4000));
    const auto received = f.simulator.received().gimbal;
    REQUIRE_FALSE(received.empty());
    for (const auto& [t, m] : received) {
        CHECK(std::isnan(m.q[0])); // rates, never angles
        CHECK((m.flags & GIMBAL_DEVICE_FLAGS_YAW_LOCK) != 0); // the gimbal's mode preserved
    }
    // It then settles inside the deadband and holds: no command, the state says so.
    for (int i = 0; i < 100; ++i) {
        imago_head_track_pose(f.session, 40, 15, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    f.simulator.clearReceived();
    for (int i = 0; i < 40; ++i) {
        imago_head_track_pose(f.session, 40, 15, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(f.simulator.received().gimbal.empty());
    auto st = f.state();
    CHECK(st.head_track_following);
    CHECK(st.head_track_error_deg < 1.0f);
    CHECK(st.head_track_target_yaw_deg == doctest::Approx(40).epsilon(0.02));

    // Back to the left and down, through the quaternion entry point.
    float q[4];
    quaternionYawPitch(-60, -30, q);
    REQUIRE(waitFor([&] {
        imago_head_track_pose_quaternion(f.session, q[0], q[1], q[2], q[3], 0);
        const imago_state s = f.state();
        return std::fabs(s.gimbal_yaw_deg + 60) < 1 && std::fabs(s.gimbal_pitch_deg + 30) < 1;
    }, 4000));

    // Poses stop (headset taken off): the gimbal is stopped and stays.
    f.simulator.clearReceived();
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    st = f.state();
    CHECK_FALSE(st.head_track_pose_fresh);
    CHECK_FALSE(st.head_track_following);
    CHECK(st.head_track_mode == IMAGO_HEAD_TRACK_RATE);
    CHECK(std::fabs(st.gimbal_yaw_deg + 60) < 1.5f);

    // Off: the joystick path is back.
    imago_head_track_set_mode(f.session, IMAGO_HEAD_TRACK_OFF);
    CHECK(f.state().head_track_mode == IMAGO_HEAD_TRACK_OFF);
    f.simulator.clearReceived();
    imago_gimbal_set_rate(f.session, 0, 20);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    imago_gimbal_set_rate(f.session, 0, 0);
    CHECK(f.simulator.received().gimbal.size() >= 8);
}

TEST_CASE("session: head tracking with a gimbal that turns at a quarter of the rate asked") {
    HeadFixture f(0.25f);
    f.switchOn(IMAGO_HEAD_TRACK_RATE);
    // Slower, as the loop's effective gain is a quarter: about 7 s for 30 deg.
    CHECK(f.follow(-30, 10, 15000));
    CHECK(f.follow(20, -20, 15000));
}

TEST_CASE("session: a gimbal that turns the other way for a positive rate is followed") {
    HeadFixture f(0.25f, -1.0f); // the Orus L as the first Quest test saw it (2026-11-09)
    f.switchOn(IMAGO_HEAD_TRACK_RATE);
    CHECK(f.follow(30, -15, 15000));
    const imago_state s = f.state();
    CHECK(s.head_track_yaw_inverted);
    CHECK(s.head_track_pitch_inverted);
    CHECK(s.head_track_fault == IMAGO_HEAD_TRACK_FAULT_NONE);
    CHECK(f.follow(-20, 10, 15000));
}

TEST_CASE("session: head tracking stops itself when the gimbal does not move") {
    HeadFixture f(0.0f);
    f.switchOn(IMAGO_HEAD_TRACK_RATE);
    REQUIRE(waitFor([&] {
        imago_head_track_pose(f.session, 30, 0, 0);
        return f.state().head_track_fault == IMAGO_HEAD_TRACK_FAULT_NO_MOTION;
    }, 6000));
    CHECK_FALSE(f.state().head_track_following);
    f.simulator.clearReceived();
    for (int i = 0; i < 50; ++i) {
        imago_head_track_pose(f.session, 30, 0, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    size_t moving = 0;
    for (const auto& [t, m] : f.simulator.received().gimbal) {
        if (m.angular_velocity_y != 0 || m.angular_velocity_z != 0) ++moving;
    }
    CHECK(moving == 0);
}

TEST_CASE("session: the head loop's rates go out at the gimbal period, not faster") {
    HeadFixture f;
    f.switchOn(IMAGO_HEAD_TRACK_RATE);
    f.simulator.clearReceived();
    const int64_t start = imago_now_ns();
    for (int i = 0; i < 100; ++i) { // 1 s with a far target: a rate every period
        imago_head_track_pose(f.session, 120, 0, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const float seconds = static_cast<float>(imago_now_ns() - start) / 1e9f;
    const size_t sent = f.simulator.received().gimbal.size();
    CHECK(static_cast<float>(sent) <= seconds * 30.0f * 1.2f + 3.0f);
    CHECK(static_cast<float>(sent) >= seconds * 30.0f * 0.7f);
}

TEST_CASE("session: switching head tracking on does not move the gimbal; a joystick trims") {
    HeadFixture f;
    // Gimbal away from the nose first.
    imago_gimbal_set_rate(f.session, 0, 30);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    imago_gimbal_set_rate(f.session, 0, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const float yawBefore = f.state().gimbal_yaw_deg;
    REQUIRE(yawBefore > 20);

    imago_head_track_set_mode(f.session, IMAGO_HEAD_TRACK_RATE);
    f.simulator.clearReceived();
    for (int i = 0; i < 50; ++i) {
        imago_head_track_pose(f.session, 0, 0, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(f.simulator.received().gimbal.empty());
    CHECK(f.state().gimbal_yaw_deg == doctest::Approx(yawBefore).epsilon(0.05));
    CHECK(f.state().head_track_target_yaw_deg == doctest::Approx(yawBefore).epsilon(0.05));

    // The head turns 10 deg: the gimbal moves 10 deg from where it was, not to 10.
    REQUIRE(waitFor([&] {
        imago_head_track_pose(f.session, 10, 0, 0);
        return std::fabs(f.state().gimbal_yaw_deg - (yawBefore + 10)) < 1.5f;
    }, 4000));

    // A joystick while on trims instead of fighting: 20 deg/s for about 0.5 s shifts about
    // 10 deg. The shift is taken from the time really spent (a slow CI runner sleeps longer).
    const int64_t trimStart = imago_now_ns();
    imago_gimbal_set_rate(f.session, 0, 20);
    for (int i = 0; i < 50; ++i) {
        imago_head_track_pose(f.session, 10, 0, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    imago_gimbal_set_rate(f.session, 0, 0);
    const float trimmed = 20.0f * static_cast<float>(imago_now_ns() - trimStart) / 1e9f;
    REQUIRE(waitFor([&] {
        imago_head_track_pose(f.session, 10, 0, 0);
        return std::fabs(f.state().gimbal_yaw_deg - (yawBefore + 10 + trimmed)) < 3.0f;
    }, 6000));
}

TEST_CASE("session: the payload's tracker holds head tracking until it stops") {
    HeadFixture f;
    f.switchOn(IMAGO_HEAD_TRACK_RATE);
    REQUIRE(f.follow(0, 0, 2000));

    int32_t result = 99;
    const uint32_t track = imago_tracking_start_at(f.session, 0.5f, 0.5f, 0.1f, 0.1f);
    REQUIRE(waitFor([&] { return f.events.result(track, result); }, 1000));
    REQUIRE(waitFor([&] { return f.state().tracking_state == IMAGO_TRACKING_TRACKED; }, 1000));
    // The head turns; the gimbal is the tracker's.
    f.simulator.clearReceived();
    for (int i = 0; i < 60; ++i) {
        imago_head_track_pose(f.session, 45, 0, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    auto st = f.state();
    CHECK(st.head_track_held_by_tracker);
    CHECK_FALSE(st.head_track_following);
    size_t moving = 0;
    for (const auto& [t, m] : f.simulator.received().gimbal) {
        if (m.angular_velocity_y != 0 || m.angular_velocity_z != 0) ++moving;
    }
    CHECK(moving == 0);

    imago_tracking_stop(f.session);
    REQUIRE(waitFor([&] { return f.state().tracking_state == IMAGO_TRACKING_IDLE; }, 1000));
    CHECK(f.follow(45, 0, 4000));
    CHECK_FALSE(f.state().head_track_held_by_tracker);
}

TEST_CASE("session: ANGLE mode sends attitude setpoints") {
    HeadFixture f;
    f.switchOn(IMAGO_HEAD_TRACK_ANGLE);
    REQUIRE(f.follow(25, -15, 3000));
    bool angles = false;
    for (const auto& [t, m] : f.simulator.received().gimbal) {
        if (!std::isnan(m.q[0])) {
            angles = true;
            CHECK(std::isnan(m.angular_velocity_y));
        }
    }
    CHECK(angles);
}

TEST_CASE("C API: head tracking tolerates a null session and bad values") {
    imago_head_track_configure(nullptr, nullptr);
    imago_head_track_set_mode(nullptr, IMAGO_HEAD_TRACK_RATE);
    imago_head_track_recentre(nullptr);
    imago_head_track_pose(nullptr, 0, 0, 0);
    imago_head_track_pose_quaternion(nullptr, 1, 0, 0, 0, 0);
    HeadTracking h;
    h.setMode(IMAGO_HEAD_TRACK_RATE);
    h.setPose(NAN, 0, 1); // ignored
    CHECK_FALSE(h.status(1).poseFresh);
    h.setMode(99); // ignored
    CHECK(h.mode() == IMAGO_HEAD_TRACK_RATE);
}
