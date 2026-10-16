// Integration: the whole core (C API) against the Orus L simulator over real UDP sockets.

#include "imago/imago_core.h"
#include "integration_helpers.h"
#include "orus_simulator.h"
#include "payload/orus_protocol.h"

#include <doctest.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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
        CHECK((m.flags & GIMBAL_DEVICE_FLAGS_YAW_LOCK) != 0); // mode preserved from the status
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

TEST_CASE("session: gimbal rate through DO_MOUNT_CONTROL (protocol V1)") {
    Fixture f({}, [](imago_config& cfg) { cfg.gimbal_protocol = IMAGO_GIMBAL_PROTOCOL_V1; });
    REQUIRE(waitFor([&] { return f.state().gimbal_valid; }, 2000));
    imago_gimbal_set_rate(f.session, 20.0f, 0.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    imago_gimbal_set_rate(f.session, 0.0f, 0.0f);
    REQUIRE(waitFor([&] { return f.countCommands(MAV_CMD_DO_MOUNT_CONTROL) >= 10; }, 500));
    CHECK(f.simulator.received().gimbal.empty()); // nothing sent with the V2 message
    CHECK(waitFor([&] { return f.state().gimbal_pitch_deg > 5.0f; }, 500));

    // Switching at runtime goes back to GIMBAL_DEVICE_SET_ATTITUDE.
    imago_gimbal_set_protocol(f.session, IMAGO_GIMBAL_PROTOCOL_V2);
    imago_gimbal_set_rate(f.session, 0.0f, 10.0f);
    CHECK(waitFor([&] { return !f.simulator.received().gimbal.empty(); }, 200));
    imago_gimbal_set_rate(f.session, 0.0f, 0.0f);
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
    REQUIRE(f.waitResult(photo, result, 2500)); // single attempt: waits at least 1.5 s
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
    // The box arrives one value at a time (x, y, w, h), so wait for the last of them.
    REQUIRE(waitFor([&] { return f.state().tracking_box_valid && f.state().tracking_box.h > 0; }, 1000));
    const auto st = f.state();
    CHECK(st.tracking_mode == IMAGO_TRACKING_ACTIVE);
    CHECK(st.tracking_state == IMAGO_TRACKING_TRACKED);
    CHECK(st.tracking_box.w == doctest::Approx(0.1f));
    CHECK(st.tracking_box.x == doctest::Approx(0.45f));

    // A new point while tracking: the box moves there.
    imago_tracking_start_at(f.session, 0.2f, 0.3f, 0.1f, 0.1f);
    REQUIRE(waitFor([&] { return f.state().tracking_box.x == doctest::Approx(0.15f); }, 1000));
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    CHECK_FALSE(f.events.has(IMAGO_EVENT_TRACKING_IGNORED)); // the point was followed

    imago_tracking_stop(f.session);
    CHECK(waitFor([&] { return f.state().tracking_state == IMAGO_TRACKING_IDLE; }, 1000));
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    CHECK_FALSE(f.events.has(IMAGO_EVENT_TRACKING_IGNORED)); // and so was the stop

    imago_tracker_set_function(f.session, IMAGO_TRACKER_OBJECT_DETECTION);
    REQUIRE(waitFor([&] { return f.state().detection_count == 2; }, 1500));
    CHECK(f.state().detections[0].class_id == 2);
    imago_tracker_set_function(f.session, IMAGO_TRACKER_OBJECT_TRACKING);
    CHECK(waitFor([&] { return f.state().detection_count == 0; }, 2500)); // stale boxes expire
}

TEST_CASE("session: a tracking command the payload accepts but ignores is reported") {
    // Field test 2026-10-15: for minutes every stop and every new point got MAV_RESULT_ACCEPTED
    // while the tracker stayed where it was. The command's own result says "accepted", so the
    // core watches the tracker's status and raises IMAGO_EVENT_TRACKING_IGNORED.
    Fixture f;
    REQUIRE(waitFor([&] { return f.events.has(IMAGO_EVENT_LINK_UP); }, 2000));
    auto ignored = [&](const char* text) {
        int n = 0;
        for (const auto& e : f.events.events()) {
            if (e.type == IMAGO_EVENT_TRACKING_IGNORED && e.text == text) ++n;
        }
        return n;
    };

    // Tracking an object, then the tracker stops listening: a new point and a stop are
    // accepted and ignored, each reported once.
    imago_tracking_start_at(f.session, 0.5f, 0.5f, 0.1f, 0.1f);
    REQUIRE(waitFor([&] { return f.state().tracking_box_valid && f.state().tracking_box.h > 0; }, 1000));
    f.simulator.setIgnoreTracking(true);
    int32_t result = 99;
    const uint32_t point = imago_tracking_start_at(f.session, 0.2f, 0.3f, 0.1f, 0.1f);
    REQUIRE(f.waitResult(point, result));
    CHECK(result == IMAGO_RESULT_ACCEPTED);
    REQUIRE(waitFor([&] { return ignored("track") == 1; }, 2500));
    CHECK(f.state().tracking_state == IMAGO_TRACKING_TRACKED);

    const uint32_t stop = imago_tracking_stop(f.session);
    REQUIRE(f.waitResult(stop, result));
    CHECK(result == IMAGO_RESULT_ACCEPTED);
    REQUIRE(waitFor([&] { return ignored("stop") == 1; }, 2500));
    CHECK(ignored("track") == 1);

    // Once it listens again, a stop is followed and nothing more is reported.
    f.simulator.setIgnoreTracking(false);
    imago_tracking_stop(f.session);
    REQUIRE(waitFor([&] { return f.state().tracking_state == IMAGO_TRACKING_IDLE; }, 1000));
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    CHECK(ignored("stop") == 1);
}

TEST_CASE("session: the target position the payload works out") {
    Fixture f;
    REQUIRE(waitFor([&] { return f.state().target_valid; }, 3000));
    const auto st = f.state();
    // The simulator puts the target 412.5 m along the camera's bearing (30°) from 22.16984,
    // -101.05302; float resolution is about 1 m.
    CHECK(st.target_lat_deg == doctest::Approx(22.16984 + 412.5 * std::cos(30 / 57.29578) / 111320)
                                   .epsilon(1e-7));
    CHECK(st.target_lon_deg == doctest::Approx(-101.05302 + 412.5 * std::sin(30 / 57.29578) /
                                                                (111320 * std::cos(22.16984 / 57.29578)))
                                   .epsilon(1e-7));
    CHECK(st.target_alt_m == doctest::Approx(1981.0f));
}

TEST_CASE("session: a far tracking point the gimbal brings to the centre is not reported") {
    // Field report 2026-10-16: every point far from the centre raised the "accepted but not
    // tracking" warning, because by the time it was checked the gimbal had already turned and
    // the box sat in the centre again, far from the point asked for.
    sim::OrusSimulator::Options options;
    options.centreTarget = true;
    Fixture f(options);
    REQUIRE(waitFor([&] { return f.events.has(IMAGO_EVENT_LINK_UP); }, 2000));
    REQUIRE(waitFor([&] { return f.state().gimbal_yaw_valid; }, 2000));

    imago_tracking_start_at(f.session, 0.5f, 0.5f, 0.1f, 0.1f);
    REQUIRE(waitFor([&] { return f.state().tracking_state == IMAGO_TRACKING_TRACKED; }, 1000));
    const float yawBefore = f.state().gimbal_yaw_deg;
    int32_t result = 99;
    const uint32_t point = imago_tracking_start_at(f.session, 0.1f, 0.2f, 0.1f, 0.1f);
    REQUIRE(f.waitResult(point, result));
    CHECK(result == IMAGO_RESULT_ACCEPTED);
    REQUIRE(waitFor([&] { return std::fabs(f.state().gimbal_yaw_deg - yawBefore) > 5; }, 1000));
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    CHECK(f.state().tracking_box.x == doctest::Approx(0.45f));
    CHECK_FALSE(f.events.has(IMAGO_EVENT_TRACKING_IGNORED));

    // The same retarget with a tracker that does not listen is still reported.
    f.simulator.setIgnoreTracking(true);
    imago_tracking_start_at(f.session, 0.1f, 0.2f, 0.1f, 0.1f);
    CHECK(waitFor([&] { return f.events.has(IMAGO_EVENT_TRACKING_IGNORED); }, 2500));
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

TEST_CASE("session: Wi-Fi keep-awake pokes the MAVLink peer's discard port, off by default") {
    // The peer is the ground unit on the Wi-Fi hop with Herelink forwarding (listen mode), so
    // the datagram wakes the radio without crossing the air link; it starts once the peer is
    // known and does not depend on video.
    net::UdpSocket sink;
    REQUIRE(sink.open(0, 64 * 1024, net::TrafficClass::BestEffort, nullptr));
    auto drain = [&] {
        int n = 0;
        uint8_t byte[16];
        while (sink.receive(byte, sizeof(byte), nullptr) > 0) ++n;
        return n;
    };
    auto listening = [&](bool keepAwake) {
        const uint16_t listenPort = freeUdpPort();
        sim::OrusSimulator::Options options;
        options.pushTo = net::Endpoint::parse("127.0.0.1", listenPort);
        return std::make_unique<Fixture>(options, [&](imago_config& cfg) {
            cfg.payload_ip = nullptr;
            cfg.local_port = listenPort;
            cfg.wifi_keep_awake = keepAwake;
            cfg.wifi_keep_awake_port = sink.localPort();
        });
    };

    auto off = listening(false);
    REQUIRE(waitFor([&] { return off->events.has(IMAGO_EVENT_LINK_UP); }, 2000));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(drain() == 0);
    off.reset();

    auto on = listening(true);
    REQUIRE(waitFor([&] { return on->events.has(IMAGO_EVENT_LINK_UP); }, 2000));
    drain();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    const int pokes = drain();
    CHECK(pokes >= 20); // ~30 a second
    CHECK(pokes <= 40);
    // MAVLink itself is unaffected (the first heartbeat waits for the peer to be known).
    CHECK(waitFor([&] { return on->simulator.received().heartbeats > 0; }, 1500));
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

namespace {

// The latest value the session reported for a camera setting, if any.
std::optional<uint32_t> cameraParam(const EventLog& log, const std::string& id) {
    std::optional<uint32_t> value;
    for (const auto& e : log.events()) {
        if (e.type == IMAGO_EVENT_CAMERA_PARAM && e.text == id) {
            value = e.code;
        }
    }
    return value;
}

} // namespace

TEST_CASE("session: camera settings are read on request and reported with every change") {
    Fixture f;
    REQUIRE(waitFor([&] { return f.events.has(IMAGO_EVENT_LINK_UP); }, 2000));

    // Several reads at once are queued and paced, and each value is reported.
    CHECK(imago_camera_read_param(f.session, "C_T_PALETTE") == 0);
    CHECK(imago_camera_read_param(f.session, "LRF_MODE") == 0);
    CHECK(imago_camera_read_param(f.session, "RC_MODE") == 0);
    CHECK(waitFor([&] { return cameraParam(f.events, "C_T_PALETTE") == 0u; }, 1000));
    CHECK(waitFor([&] { return cameraParam(f.events, "LRF_MODE") == 3u; }, 1000));
    CHECK(waitFor([&] { return cameraParam(f.events, "RC_MODE") == 0u; }, 1000));

    // A setting the camera accepts is reported with its new value.
    int32_t result = 99;
    const uint32_t id = imago_camera_set_param(f.session, "C_T_PALETTE", 4);
    REQUIRE(f.waitResult(id, result));
    CHECK(result == IMAGO_RESULT_ACCEPTED);
    CHECK(waitFor([&] { return cameraParam(f.events, "C_T_PALETTE") == 4u; }, 1000));

    CHECK(imago_camera_read_param(f.session, "") == IMAGO_ERR_INVALID_ARG);
    CHECK(imago_camera_read_param(f.session, "MUCH_TOO_LONG_PARAM_ID") == IMAGO_ERR_INVALID_ARG);
}

TEST_CASE("session: IR state, flat-field correction and palettes") {
    Fixture f;
    REQUIRE(waitFor([&] { return f.events.has(IMAGO_EVENT_LINK_UP); }, 2000));
    CHECK(waitFor([&] { return f.state().ir_palette == 0 && f.state().ir_ffc_mode == 1; }, 2000));
    CHECK(waitFor([&] { return f.state().ir_camera_type == 2; }, 2000));

    int32_t result = 99;
    uint32_t id = imago_camera_set_param(f.session, "C_T_PALETTE", 4);
    REQUIRE(f.waitResult(id, result));
    CHECK(waitFor([&] { return f.state().ir_palette == 4; }, 2000));

    id = imago_ir_set_ffc_mode(f.session, false);
    REQUIRE(f.waitResult(id, result));
    CHECK(result == IMAGO_RESULT_ACCEPTED);
    CHECK(waitFor([&] { return f.state().ir_ffc_mode == 0; }, 2000));

    id = imago_ir_ffc(f.session);
    REQUIRE(f.waitResult(id, result));
    CHECK(result == IMAGO_RESULT_ACCEPTED);
    CHECK(f.simulator.ffcCount() == 1);

    // The G1 names, those of our Orus L's thermal core (2026-10-16).
    CHECK(std::string(imago_ir_palette_name(0, 4)) == "Medical");
    CHECK(std::string(imago_ir_palette_name(0, 9)) == "Black hot");
    CHECK(std::string(imago_ir_palette_name(-1, 0)) == "White hot");
    CHECK(std::string(imago_ir_palette_name(0, IMAGO_IR_PALETTE_COUNT)).empty());
    uint32_t colours[8] = {};
    CHECK(imago_ir_palette_colors(0, 9, colours, 8) == 2);
    CHECK(colours[0] == 0xFFFFFFu);
    CHECK(colours[1] == 0x000000u);
    CHECK(imago_ir_palette_colors(0, 99, colours, 8) == 0);
}

TEST_CASE("session: isotherm band as settings, low gain above 150 C") {
    Fixture f;
    REQUIRE(waitFor([&] { return f.events.has(IMAGO_EVENT_LINK_UP); }, 2000));
    CHECK(imago_ir_set_isotherm(f.session, true, 40, 35) == 0);

    int32_t result = 99;
    uint32_t id = imago_ir_set_isotherm(f.session, true, 35.4f, 40.0f);
    REQUIRE(f.waitResult(id, result));
    CHECK(result == IMAGO_RESULT_ACCEPTED);
    CHECK(f.simulator.param("C_T_ISO_M") == 1);
    CHECK(f.simulator.param("C_T_ISO_U") == 1);
    CHECK(f.simulator.param("C_T_RG0_TMP") == 35);
    CHECK(f.simulator.param("C_T_RG1_TMP") == 40);
    CHECK(f.simulator.param("C_T_RG1_CLR_M") == 2);
    CHECK(f.simulator.param("C_T_RG1_CLR_MX") == 9);
    CHECK(f.simulator.param("C_T_RG0_CLR_M") == 0);
    CHECK(f.simulator.param("C_T_G") == 0); // within high gain

    // The isotherm setting goes out last, over a finished band.
    const auto params = f.simulator.received().params;
    REQUIRE(!params.empty());
    CHECK(std::string(params.back().param_id, strnlen(params.back().param_id, 16)) == "C_T_ISO_M");

    id = imago_ir_set_isotherm(f.session, true, 100, 400);
    REQUIRE(f.waitResult(id, result));
    CHECK(f.simulator.param("C_T_G") == 1);
    CHECK(f.simulator.param("C_T_RG1_TMP") == 400);

    id = imago_ir_set_isotherm(f.session, false, 0, 0);
    REQUIRE(f.waitResult(id, result));
    CHECK(f.simulator.param("C_T_ISO_M") == 0);
}

TEST_CASE("session: a camera without isotherm settings answers no read and rejects them") {
    sim::OrusSimulator::Options options;
    options.isotherm = false;
    Fixture f(options);
    REQUIRE(waitFor([&] { return f.events.has(IMAGO_EVENT_LINK_UP); }, 2000));
    CHECK(imago_camera_read_param(f.session, "C_T_ISO_M") == 0);
    CHECK(imago_camera_read_param(f.session, "C_T_PALETTE") == 0);
    CHECK(waitFor([&] { return cameraParam(f.events, "C_T_PALETTE") == 0u; }, 1000));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK(!cameraParam(f.events, "C_T_ISO_M").has_value());

    int32_t result = 0;
    const uint32_t id = imago_ir_set_isotherm(f.session, true, 35, 40);
    REQUIRE(f.waitResult(id, result));
    CHECK(result != IMAGO_RESULT_ACCEPTED);
}

namespace {

// Levels the simulator was asked for through the zoom level settings, in order.
std::vector<float> zoomLevelsSent(const sim::OrusSimulator& simulator) {
    std::vector<float> levels;
    for (const auto& p : simulator.received().params) {
        const std::string id(p.param_id, strnlen(p.param_id, sizeof(p.param_id)));
        uint32_t index = 0;
        std::memcpy(&index, p.param_value, sizeof(index));
        const int32_t mode = id == "C_V_ZM_SR_LV"   ? IMAGO_ZOOM_MODE_SUPER_RESOLUTION
                             : id == "C_V_ZM_CB_LV" ? IMAGO_ZOOM_MODE_COMBINED
                                                    : IMAGO_ZOOM_MODE_UNKNOWN;
        const auto table = orus::zoomLevels(mode);
        if (mode != IMAGO_ZOOM_MODE_UNKNOWN && index < table.size()) {
            levels.push_back(table[index]);
        }
    }
    return levels;
}

} // namespace

TEST_CASE("session: a dragged zoom level snaps to the camera's levels, is rate limited and clamped") {
    Fixture f;
    // The zoom mode is read on link up.
    REQUIRE(waitFor([&] { return f.state().eo_zoom_mode == IMAGO_ZOOM_MODE_COMBINED; }, 3000));
    CHECK(f.state().eo_zoom_max == 300.0f);
    f.simulator.clearReceived();

    // A drag of about 400 ms with a new level every 10 ms. The first level leaves at once.
    // The clock starts with it: the wait below also counts toward the send window.
    const int64_t start = imago_now_ns();
    imago_camera_zoom_target(f.session, 1.0f);
    CHECK(waitFor([&] { return !zoomLevelsSent(f.simulator).empty(); }, 100));
    for (int i = 1; i <= 38; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        imago_camera_zoom_target(f.session, 1.0f + static_cast<float>(i) * 0.25f);
    }
    // Sleeps run long on loaded machines (macOS CI), so the bound follows the real duration.
    const int64_t dragMs = (imago_now_ns() - start) / 1'000'000;
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    auto levels = zoomLevelsSent(f.simulator);
    CHECK(levels.size() >= 3);
    // About one per 150 ms, plus the first, the final and its repeat.
    CHECK(levels.size() <= static_cast<size_t>(dragMs / 150 + 3));
    REQUIRE(levels.size() >= 2);
    CHECK(levels.back() == 10.0f); // 10.5x snaps to the camera's 10x
    CHECK(levels[levels.size() - 2] == 10.0f); // the repeat
    // The simulated lens gets there.
    CHECK(waitFor([&] { return std::fabs(f.state().eo_zoom_level - 10.0f) < 0.05f; }, 4000));

    // Super resolution has its own table and stops at 25x.
    int32_t result = 99;
    const uint32_t id = imago_camera_set_param(f.session, "C_V_ZM_MODE", 2);
    REQUIRE(f.waitResult(id, result));
    CHECK(waitFor([&] { return f.state().eo_zoom_max == 25.0f; }, 1000));
    f.simulator.clearReceived();
    imago_camera_zoom_target(f.session, 100.0f);
    REQUIRE(waitFor([&] { return !zoomLevelsSent(f.simulator).empty(); }, 500));
    CHECK(zoomLevelsSent(f.simulator).front() == 25.0f);

    // A continuous zoom cancels the pending repeat.
    imago_camera_zoom(f.session, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    CHECK(zoomLevelsSent(f.simulator).size() == 1);
}

namespace {

size_t countEvents(const EventLog& log, imago_event_type type) {
    size_t n = 0;
    for (const auto& e : log.events()) {
        if (e.type == type) ++n;
    }
    return n;
}

} // namespace

TEST_CASE("session: zoom from another controller is reported") {
    Fixture f;
    REQUIRE(waitFor([&] { return f.state().eo_zoom_mode == IMAGO_ZOOM_MODE_COMBINED; }, 3000));

    // This session's own zoom is not reported.
    imago_camera_zoom_target(f.session, 4.0f);
    CHECK(waitFor([&] { return std::fabs(f.state().eo_zoom_level - 4.0f) < 0.05f; }, 3000));
    CHECK(countEvents(f.events, IMAGO_EVENT_ZOOM_CHANGED_EXTERNALLY) == 0);
    CHECK(countEvents(f.events, IMAGO_EVENT_FOREIGN_COMMAND) == 0);

    // The autopilot zooms out; the payload acknowledges it to the autopilot.
    f.simulator.zoomFromElsewhere(1.0f, 1, MAV_COMP_ID_AUTOPILOT1);
    CHECK(waitFor([&] {
        for (const auto& e : f.events.events()) {
            if (e.type == IMAGO_EVENT_FOREIGN_COMMAND && e.code == MAV_CMD_SET_CAMERA_ZOOM &&
                e.text.find("1/1") != std::string::npos) {
                return true;
            }
        }
        return false;
    }, 1000));
    // Once this session's own zoom is a few seconds old, a lens moving on its own is reported.
    CHECK(waitFor([&] { return countEvents(f.events, IMAGO_EVENT_ZOOM_CHANGED_EXTERNALLY) > 0; },
                  8000));
}

TEST_CASE("session: every MAVLink message in and out is recorded to a tlog") {
    const std::string path =
        (std::filesystem::temp_directory_path() / "imago_session_test.tlog").string();
    std::filesystem::remove(path);
    Fixture f;
    REQUIRE(imago_session_start_recording(f.session, path.c_str()) == 0);
    REQUIRE(waitFor([&] { return f.events.has(IMAGO_EVENT_LINK_UP); }, 2000));
    std::this_thread::sleep_for(std::chrono::milliseconds(2000)); // heartbeats both ways, stats
    imago_session_stop_recording(f.session);

    std::ifstream in(path, std::ios::binary);
    REQUIRE(in);
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), {});
    const auto nowUs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    size_t offset = 0;
    int records = 0;
    int sent = 0;
    int received = 0;
    int stats = 0; // the app's own NAMED_VALUE_FLOAT, once a second
    while (offset + 8 + 12 <= data.size()) {
        uint64_t us = 0;
        for (int i = 0; i < 8; ++i) us = (us << 8) | data[offset + i];
        CHECK(us + 60'000'000 > nowUs); // big-endian µs since 1970, recent
        CHECK(us < nowUs + 1'000'000);
        const uint8_t* p = &data[offset + 8];
        REQUIRE(p[0] == 0xFD); // MAVLink 2
        const size_t length = 10 + p[1] + 2 + ((p[2] & 0x01) != 0 ? 13 : 0);
        const uint8_t compid = p[6];
        const uint32_t msgid = p[7] | p[8] << 8 | p[9] << 16;
        if (compid == MAV_COMP_ID_ONBOARD_COMPUTER3 && msgid == MAVLINK_MSG_ID_NAMED_VALUE_FLOAT) {
            ++stats;
        } else {
            (compid == MAV_COMP_ID_ONBOARD_COMPUTER3 ? sent : received)++;
        }
        offset += 8 + length;
        ++records;
    }
    CHECK(offset == data.size());
    CHECK(records > 5);
    CHECK(sent > 0);
    CHECK(received > 0);
    CHECK(stats >= 12); // one batch at least (12 values)
    CHECK(f.simulator.received().heartbeats > 0);
    CHECK(f.simulator.received().namedValues == 0); // recorded, never sent
    in.close(); // Windows does not remove a file that is still open
    std::filesystem::remove(path);

    CHECK(imago_session_start_recording(f.session, "/no/such/dir/x.tlog") == IMAGO_ERR_INVALID_ARG);
}

namespace {

struct TlogRecord {
    uint8_t compid;
    uint32_t msgid;
};

std::vector<TlogRecord> readTlog(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), {});
    std::vector<TlogRecord> records;
    size_t offset = 0;
    while (offset + 8 + 12 <= data.size()) {
        const uint8_t* p = &data[offset + 8];
        const size_t length = 10 + p[1] + 2 + ((p[2] & 0x01) != 0 ? 13 : 0);
        records.push_back({p[6], static_cast<uint32_t>(p[7] | p[8] << 8 | p[9] << 16)});
        offset += 8 + length;
    }
    return records;
}

} // namespace

TEST_CASE("session: a compact recording keeps the payload's side, streams once a second") {
    const std::string path =
        (std::filesystem::temp_directory_path() / "imago_session_compact.tlog").string();
    std::filesystem::remove(path);
    sim::OrusSimulator::Options options;
    options.autopilot = true; // someone else on the link, as through Herelink
    Fixture f(options);
    imago_session_set_recording_detail(f.session, IMAGO_RECORDING_COMPACT);
    REQUIRE(waitFor([&] { return f.events.has(IMAGO_EVENT_LINK_UP); }, 2000));
    REQUIRE(imago_session_start_recording(f.session, path.c_str()) == 0);
    imago_gimbal_set_rate(f.session, 0, 10); // setpoints at the joystick's rate
    const uint32_t photo = imago_camera_take_photo(f.session);
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    imago_gimbal_set_rate(f.session, 0, 0);
    int32_t result = 0;
    REQUIRE(f.waitResult(photo, result));
    imago_session_stop_recording(f.session);

    int gimbalStatus = 0, setpoints = 0, stats = 0, autopilot = 0, commands = 0, acks = 0;
    for (const auto& r : readTlog(path)) {
        if (r.compid == MAV_COMP_ID_ONBOARD_COMPUTER3) {
            setpoints += r.msgid == MAVLINK_MSG_ID_GIMBAL_DEVICE_SET_ATTITUDE;
            stats += r.msgid == MAVLINK_MSG_ID_NAMED_VALUE_FLOAT;
            commands += r.msgid == MAVLINK_MSG_ID_COMMAND_LONG;
        } else {
            autopilot += r.compid == MAV_COMP_ID_AUTOPILOT1;
            gimbalStatus += r.msgid == MAVLINK_MSG_ID_GIMBAL_DEVICE_ATTITUDE_STATUS;
            acks += r.msgid == MAVLINK_MSG_ID_COMMAND_ACK;
        }
    }
    CHECK(stats == 0);
    CHECK(autopilot == 0);
    CHECK(gimbalStatus >= 2);
    CHECK(gimbalStatus <= 5); // a few seconds, once a second
    CHECK(setpoints >= 2);
    CHECK(setpoints <= 5);
    CHECK(commands >= 1); // the photo, kept in full
    CHECK(acks >= 1);
    std::filesystem::remove(path);
}

TEST_CASE("session: gimbal through the autopilot's gimbal manager, once in control") {
    sim::OrusSimulator::Options simOptions;
    simOptions.autopilot = true; // a PX4-like gimbal manager in front of the gimbal
    Fixture f(simOptions, [](imago_config& c) { c.gimbal_protocol = IMAGO_GIMBAL_PROTOCOL_MANAGER; });
    REQUIRE(waitFor([&] { return f.state().link_up; }, 2000));

    // The autopilot holds control at first, as PX4 does: our rates are ignored.
    REQUIRE(waitFor([&] { return f.state().gimbal_control == IMAGO_GIMBAL_CONTROL_OTHER; }, 2000));
    CHECK(f.state().gimbal_control_system == 1);
    CHECK(f.state().gimbal_control_component == MAV_COMP_ID_AUTOPILOT1);
    const float yaw0 = f.state().gimbal_yaw_deg;
    imago_gimbal_set_rate(f.session, 0, 30);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    imago_gimbal_set_rate(f.session, 0, 0);
    CHECK(std::fabs(f.state().gimbal_yaw_deg - yaw0) < 1.0f);

    // Take control: the manager now follows our rates. Nothing goes to the gimbal directly.
    f.simulator.clearReceived();
    int32_t result = 99;
    const uint32_t id = imago_gimbal_take_control(f.session, true);
    REQUIRE(f.waitResult(id, result));
    CHECK(result == IMAGO_RESULT_ACCEPTED);
    REQUIRE(waitFor([&] { return f.state().gimbal_control == IMAGO_GIMBAL_CONTROL_THIS_APP; }, 2000));
    imago_gimbal_set_rate(f.session, 0, 30);
    CHECK(waitFor([&] { return f.state().gimbal_yaw_deg - yaw0 > 5.0f; }, 2000));
    imago_gimbal_set_rate(f.session, 0, 0);
    CHECK(f.simulator.received().gimbal.empty());
    CHECK(f.simulator.received().managerSetpoints > 0);

    // Recentre goes through the manager too (an angle setpoint), never as the payload's
    // GB_MODE reset, so the two never fight.
    f.simulator.clearReceived();
    CHECK(imago_gimbal_set_mode(f.session, IMAGO_GIMBAL_MODE_RESET) == 1);
    CHECK(waitFor([&] { return f.simulator.received().managerSetpoints > 0; }, 1000));
    CHECK(waitFor([&] { return std::fabs(f.state().gimbal_yaw_deg) < 1.0f; }, 3000));
    for (const auto& p : f.simulator.received().params) {
        CHECK(std::string(p.param_id, strnlen(p.param_id, sizeof(p.param_id))) != "GB_MODE");
    }

    // Release it.
    const uint32_t releaseId = imago_gimbal_take_control(f.session, false);
    REQUIRE(f.waitResult(releaseId, result));
    CHECK(waitFor([&] { return f.state().gimbal_control == IMAGO_GIMBAL_CONTROL_NOBODY; }, 2000));
}

TEST_CASE("session: stopping gives the gimbal manager back, even right after taking it") {
    sim::OrusSimulator::Options simOptions;
    simOptions.autopilot = true;
    Fixture f(simOptions, [](imago_config& c) { c.gimbal_protocol = IMAGO_GIMBAL_PROTOCOL_MANAGER; });
    REQUIRE(waitFor([&] { return f.state().link_up; }, 2000));
    int32_t result = 99;
    REQUIRE(f.waitResult(imago_gimbal_take_control(f.session, true), result));
    // Stop before the next status could say we hold it: the release goes out anyway.
    f.simulator.clearReceived();
    imago_session_stop(f.session);
    CHECK(waitFor([&] {
        int releases = 0;
        for (const auto& c : f.simulator.received().commands) {
            if (c.command == MAV_CMD_DO_GIMBAL_MANAGER_CONFIGURE && c.param1 == -3.0f) ++releases;
        }
        return releases >= 1;
    }, 1000));
}
