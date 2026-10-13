// Integration: the whole core (C API) against the Orus L simulator over real UDP sockets.

#include "imago/imago_core.h"
#include "integration_helpers.h"
#include "orus_simulator.h"

#include <doctest.h>

#include <cmath>
#include <cstring>
#include <memory>
#include <string>

using namespace imago;
using namespace imago::test;

namespace {

struct Fixture {
    sim::OrusSimulator simulator;
    EventLog events;
    imago_session* session = nullptr;

    explicit Fixture(sim::OrusSimulator::Options simOptions = {},
                     std::function<void(imago_config&)> tweak = {}) {
        simOptions.port = 0;
        std::string error;
        REQUIRE(simulator.start(simOptions, &error));
        imago_config cfg;
        imago_config_init(&cfg);
        cfg.payload_ip = "127.0.0.1";
        cfg.payload_port = simulator.port();
        cfg.local_port = freeUdpPort();
        cfg.auto_start_video = false;
        if (tweak) {
            tweak(cfg);
        }
        session = imago_session_create(&cfg, &EventLog::callback, nullptr, &events);
        REQUIRE(session != nullptr);
        REQUIRE(imago_session_start(session) == 0);
    }

    ~Fixture() {
        imago_session_destroy(session);
        simulator.stop();
    }

    imago_state state() const {
        imago_state s;
        imago_session_get_state(session, &s);
        return s;
    }

    bool waitResult(uint32_t id, int32_t& result, int timeoutMs = 1000) const {
        return waitFor([&] { return events.result(id, result); }, timeoutMs);
    }

    size_t countCommands(uint16_t command) const {
        size_t n = 0;
        for (const auto& c : simulator.received().commands) {
            if (c.command == command) ++n;
        }
        return n;
    }
};

} // namespace

TEST_CASE("session: link comes up and the payload is configured") {
    Fixture f;
    REQUIRE(waitFor([&] { return f.events.has(IMAGO_EVENT_LINK_UP); }, 2000));
    CHECK(waitFor([&] { return f.simulator.received().heartbeats > 0; }, 1500));

    // Data rates, identity and the stream URL are requested on link up.
    CHECK(waitFor([&] { return f.countCommands(MAV_CMD_SET_MESSAGE_INTERVAL) >= 14; }, 1000));
    CHECK(waitFor([&] { return std::strcmp(f.state().model_name, "ORUS L (simulated)") == 0; }, 1000));
    CHECK(waitFor([&] { return f.events.has(IMAGO_EVENT_STREAM_URI); }, 1000));

    const auto st = f.state();
    CHECK(st.link_up);
    CHECK(st.camera_component_id == MAV_COMP_ID_CAMERA2);
    CHECK(st.gimbal_component_id == MAV_COMP_ID_GIMBAL);
    CHECK(st.mavlink_rx_dropped == 0); // no false loss on a lossless link
    CHECK(waitFor([&] { return f.state().storage_valid; }, 3000));
    CHECK(f.state().storage_available_mib == doctest::Approx(104000.0f));
}

TEST_CASE("session: photo and recording switch the camera mode first") {
    Fixture f;
    REQUIRE(waitFor([&] { return f.events.has(IMAGO_EVENT_LINK_UP); }, 2000));

    int32_t result = 99;
    const uint32_t photo = imago_camera_take_photo(f.session);
    REQUIRE(photo != 0);
    REQUIRE(f.waitResult(photo, result));
    CHECK(result == IMAGO_RESULT_ACCEPTED);

    const uint32_t rec = imago_camera_set_recording(f.session, true);
    REQUIRE(f.waitResult(rec, result));
    CHECK(result == IMAGO_RESULT_ACCEPTED);
    CHECK(waitFor([&] { return f.state().recording; }, 3000));

    const uint32_t stop = imago_camera_set_recording(f.session, false);
    REQUIRE(f.waitResult(stop, result));
    CHECK(result == IMAGO_RESULT_ACCEPTED);
    CHECK(f.state().command_rtt_ms_avg > 0.0f);
    CHECK(f.state().command_rtt_ms_avg < 50.0f);
}

TEST_CASE("session: camera settings round trip through PARAM_EXT_SET") {
    Fixture f;
    REQUIRE(waitFor([&] { return f.events.has(IMAGO_EVENT_LINK_UP); }, 2000));
    int32_t result = 99;
    const uint32_t id = imago_camera_set_view(f.session, IMAGO_VIEW_IR);
    REQUIRE(f.waitResult(id, result));
    CHECK(result == IMAGO_RESULT_ACCEPTED);
    CHECK(f.simulator.param("C_SOURCE") == IMAGO_VIEW_IR);
    CHECK(waitFor([&] { return f.state().view_source == IMAGO_VIEW_IR; }, 2000));

    const uint32_t mode = imago_gimbal_set_mode(f.session, IMAGO_GIMBAL_MODE_FOLLOW);
    REQUIRE(f.waitResult(mode, result));
    CHECK(waitFor([&] { return f.state().gimbal_mode == IMAGO_GIMBAL_MODE_FOLLOW; }, 500));
}

TEST_CASE("session: gimbal rate starts immediately, repeats at 30 Hz and stops three times") {
    Fixture f;
    REQUIRE(waitFor([&] { return f.state().gimbal_valid; }, 2000));
    f.simulator.clearReceived();

    const int64_t start = imago_now_ns();
    imago_gimbal_set_rate(f.session, 20.0f, -10.0f);
    REQUIRE(waitFor([&] { return !f.simulator.received().gimbal.empty(); }, 100));
    const int64_t firstArrival = f.simulator.received().gimbal.front().first;
    CHECK((firstArrival - start) / 1'000'000 < 10); // left within a few milliseconds

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    imago_gimbal_set_rate(f.session, 0.0f, 0.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    const auto gimbal = f.simulator.received().gimbal;
    size_t moving = 0;
    size_t stops = 0;
    for (const auto& [t, m] : gimbal) {
        CHECK(std::isnan(m.q[0]));
        CHECK(m.flags == GIMBAL_DEVICE_FLAGS_YAW_LOCK); // mode preserved from the status
        if (m.angular_velocity_y != 0.0f || m.angular_velocity_z != 0.0f) {
            ++moving;
        } else {
            ++stops;
        }
    }
    CHECK(moving >= 13); // 500 ms at 30 Hz
    CHECK(moving <= 20);
    CHECK(stops == 3);
    // The simulated gimbal moved up by about 20 deg/s * 0.5 s.
    CHECK(f.state().gimbal_pitch_deg == doctest::Approx(10.0f).epsilon(0.3));
}

TEST_CASE("session: unacknowledged commands are retried, photos are not") {
    sim::OrusSimulator::Options options;
    options.acknowledgeCommands = false;
    Fixture f(options, [](imago_config& cfg) {
        cfg.command_timeout_ms = 40;
        cfg.command_retries = 2;
    });
    REQUIRE(waitFor([&] { return f.events.has(IMAGO_EVENT_LINK_UP); }, 2000));

    int32_t result = 99;
    const uint32_t zoom = imago_camera_zoom(f.session, 1);
    REQUIRE(f.waitResult(zoom, result));
    CHECK(result == IMAGO_RESULT_TIMEOUT);
    CHECK(f.countCommands(MAV_CMD_SET_CAMERA_ZOOM) == 3);

    const uint32_t photo = imago_camera_take_photo(f.session);
    REQUIRE(f.waitResult(photo, result));
    CHECK(result == IMAGO_RESULT_TIMEOUT);
    CHECK(f.countCommands(MAV_CMD_IMAGE_START_CAPTURE) == 1);
}

TEST_CASE("session: tracking and AI detection") {
    Fixture f;
    REQUIRE(waitFor([&] { return f.events.has(IMAGO_EVENT_LINK_UP); }, 2000));

    int32_t result = 99;
    const uint32_t track = imago_tracking_start_at(f.session, 0.5f, 0.5f, 0.1f, 0.1f);
    REQUIRE(f.waitResult(track, result));
    CHECK(result == IMAGO_RESULT_ACCEPTED);
    REQUIRE(waitFor([&] { return f.state().tracking_box_valid; }, 1000));
    const auto st = f.state();
    CHECK(st.tracking_mode == IMAGO_TRACKING_ACTIVE);
    CHECK(st.tracking_state == IMAGO_TRACKING_TRACKED);
    CHECK(st.tracking_box.w == doctest::Approx(0.1f));
    CHECK(st.tracking_box.x == doctest::Approx(0.45f));

    imago_tracking_stop(f.session);
    CHECK(waitFor([&] { return f.state().tracking_state == IMAGO_TRACKING_IDLE; }, 1000));

    imago_tracker_set_function(f.session, IMAGO_TRACKER_OBJECT_DETECTION);
    REQUIRE(waitFor([&] { return f.state().detection_count == 2; }, 1500));
    CHECK(f.state().detections[0].class_id == 2);
    imago_tracker_set_function(f.session, IMAGO_TRACKER_OBJECT_TRACKING);
    CHECK(waitFor([&] { return f.state().detection_count == 0; }, 2500)); // stale boxes expire
}

TEST_CASE("session: listen mode learns the peer, as through Herelink") {
    const uint16_t listenPort = freeUdpPort();
    sim::OrusSimulator::Options options;
    options.pushTo = net::Endpoint::parse("127.0.0.1", listenPort);
    Fixture f(options, [&](imago_config& cfg) {
        cfg.payload_ip = nullptr;
        cfg.local_port = listenPort;
    });
    REQUIRE(waitFor([&] { return f.events.has(IMAGO_EVENT_LINK_UP); }, 2000));
    CHECK(waitFor([&] { return f.simulator.received().heartbeats > 0; }, 1500));
    CHECK(std::string(f.state().mavlink_peer) ==
          "127.0.0.1:" + std::to_string(f.simulator.port()));
}

TEST_CASE("session: the link goes down when heartbeats stop") {
    Fixture f({}, [](imago_config& cfg) { cfg.link_timeout_ms = 300; });
    REQUIRE(waitFor([&] { return f.events.has(IMAGO_EVENT_LINK_UP); }, 2000));
    f.simulator.stop();
    CHECK(waitFor([&] { return f.events.has(IMAGO_EVENT_LINK_DOWN); }, 1000));
    CHECK_FALSE(f.state().link_up);
}

TEST_CASE("C API rejects invalid input without crashing") {
    imago_config cfg;
    imago_config_init(&cfg);
    cfg.payload_ip = "not-an-ip";
    CHECK(imago_session_create(&cfg, nullptr, nullptr, nullptr) == nullptr);
    CHECK(imago_session_create(nullptr, nullptr, nullptr, nullptr) == nullptr);
    CHECK(imago_camera_take_photo(nullptr) == 0);
    imago_gimbal_set_rate(nullptr, 1, 1);
    imago_state st;
    imago_session_get_state(nullptr, &st);
    CHECK_FALSE(st.link_up);

    cfg.payload_ip = "127.0.0.1";
    imago_session* s = imago_session_create(&cfg, nullptr, nullptr, nullptr);
    REQUIRE(s != nullptr);
    CHECK(imago_camera_take_photo(s) == 0); // not started
    CHECK(imago_video_start(s, "rtsp://127.0.0.1/x") == IMAGO_ERR_STATE);
    imago_session_destroy(s);
}
