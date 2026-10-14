// Integration: RTSP/RTP video through the C API against the simulator's RTSP server.

#include "imago/imago_core.h"
#include "integration_helpers.h"
#include "orus_simulator.h"
#include "rtsp_server.h"
#include "test_helpers.h"

#include <doctest.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace imago;
using namespace imago::test;

namespace {

struct FrameLog {
    std::mutex mutex;
    std::vector<std::vector<uint8_t>> frames;
    std::vector<bool> keyframes;
    std::vector<uint8_t> sps, pps;
    int corrupt = 0;
    int64_t maxDeliveryUs = 0;

    static void callback(const imago_video_frame* f, void* user) {
        auto* self = static_cast<FrameLog*>(user);
        const int64_t delayUs = (imago_now_ns() - f->receive_time_ns) / 1000;
        std::lock_guard lock(self->mutex);
        self->frames.emplace_back(f->data, f->data + f->size);
        self->keyframes.push_back(f->keyframe);
        self->sps.assign(f->sps, f->sps + f->sps_size);
        self->pps.assign(f->pps, f->pps + f->pps_size);
        self->corrupt += f->corrupt ? 1 : 0;
        self->maxDeliveryUs = std::max(self->maxDeliveryUs, delayUs);
    }

    size_t count() {
        std::lock_guard lock(mutex);
        return frames.size();
    }
};

// Bridges the C callbacks of one session to an event log and a frame log.
struct Callbacks {
    EventLog events;
    FrameLog frames;
    static void onEvent(const imago_event* e, void* user) {
        EventLog::callback(e, &static_cast<Callbacks*>(user)->events);
    }
    static void onFrame(const imago_video_frame* f, void* user) {
        FrameLog::callback(f, &static_cast<Callbacks*>(user)->frames);
    }
};

imago_session* startSession(Callbacks& cb, const std::string& rtspUrl,
                            imago_rtsp_transport transport, uint16_t mavlinkPort = 0,
                            uint32_t maxLagMs = 0) {
    imago_config cfg;
    imago_config_init(&cfg);
    if (maxLagMs != 0) {
        cfg.video_max_lag_ms = maxLagMs;
    }
    cfg.payload_ip = "127.0.0.1";
    cfg.payload_port = mavlinkPort != 0 ? mavlinkPort : freeUdpPort();
    cfg.local_port = freeUdpPort();
    cfg.rtsp_url = rtspUrl.empty() ? nullptr : rtspUrl.c_str();
    cfg.rtsp_transport = transport;
    imago_session* s = imago_session_create(&cfg, &Callbacks::onEvent, &Callbacks::onFrame, &cb);
    REQUIRE(s != nullptr);
    REQUIRE(imago_session_start(s) == 0);
    return s;
}

std::vector<uint8_t> expectedIdr() {
    const auto frames = sim::RtspServer::syntheticFrames(1, 30);
    return avcc(frames[0].back()); // SPS/PPS are stripped from the access unit
}

} // namespace

TEST_CASE("video over RTP/UDP: frames arrive intact, starting at a keyframe") {
    sim::RtspServer server;
    sim::RtspServer::Options options;
    options.port = 0;
    std::string error;
    REQUIRE(server.start(options, &error));

    Callbacks cb;
    imago_session* s = startSession(cb, server.url(), IMAGO_RTSP_UDP);
    REQUIRE(waitFor([&] { return cb.frames.count() >= 40; }, 4000));
    {
        std::lock_guard lock(cb.frames.mutex);
        CHECK(cb.frames.keyframes.front());
        CHECK(cb.frames.frames.front() == expectedIdr());
        CHECK(cb.frames.sps == sim::RtspServer::syntheticSps());
        CHECK(cb.frames.pps == sim::RtspServer::syntheticPps());
        CHECK(cb.frames.corrupt == 0);
        // The core hands a frame over within a millisecond of its last packet.
        CHECK(cb.frames.maxDeliveryUs < 1000);
    }
    CHECK(waitFor([&] {
        imago_state st;
        imago_session_get_state(s, &st);
        return st.video_fps > 20.0f;
    }, 2000));
    imago_state st;
    imago_session_get_state(s, &st);
    CHECK(st.video_active);
    CHECK(st.video_transport == IMAGO_RTSP_UDP);
    CHECK(st.video_codec == IMAGO_CODEC_H264);
    CHECK(st.video_packets_lost == 0);
    CHECK(cb.events.has(IMAGO_EVENT_VIDEO_STARTED));
    imago_session_destroy(s);
    CHECK(cb.events.has(IMAGO_EVENT_VIDEO_STOPPED));
}

TEST_CASE("video over RTSP interleaved TCP") {
    sim::RtspServer server;
    sim::RtspServer::Options options;
    options.port = 0;
    std::string error;
    REQUIRE(server.start(options, &error));

    Callbacks cb;
    imago_session* s = startSession(cb, server.url(), IMAGO_RTSP_TCP);
    REQUIRE(waitFor([&] { return cb.frames.count() >= 20; }, 4000));
    imago_state st;
    imago_session_get_state(s, &st);
    CHECK(st.video_transport == IMAGO_RTSP_TCP);
    {
        std::lock_guard lock(cb.frames.mutex);
        CHECK(cb.frames.frames.front() == expectedIdr());
        CHECK(cb.frames.corrupt == 0);
    }
    imago_session_destroy(s);
}

TEST_CASE("the simulator streams to several clients at once") {
    // A device on the Wi-Fi must get video while the iOS Simulator is still watching.
    sim::RtspServer server;
    sim::RtspServer::Options options;
    options.port = 0;
    std::string error;
    REQUIRE(server.start(options, &error));

    Callbacks first;
    imago_session* a = startSession(first, server.url(), IMAGO_RTSP_UDP);
    REQUIRE(waitFor([&] { return first.frames.count() >= 10; }, 4000));

    Callbacks second;
    imago_session* b = startSession(second, server.url(), IMAGO_RTSP_TCP);
    CHECK(waitFor([&] { return second.frames.count() >= 10; }, 4000));
    const size_t firstBefore = first.frames.count();
    CHECK(waitFor([&] { return first.frames.count() >= firstBefore + 10; }, 2000));
    imago_session_destroy(b);
    imago_session_destroy(a);
}

TEST_CASE("video in AUTO mode falls back to TCP when UDP never arrives") {
    sim::RtspServer server;
    sim::RtspServer::Options options;
    options.port = 0;
    options.sendOverUdp = false;
    std::string error;
    REQUIRE(server.start(options, &error));

    Callbacks cb;
    imago_session* s = startSession(cb, server.url(), IMAGO_RTSP_AUTO);
    REQUIRE(waitFor([&] { return cb.frames.count() >= 10; }, 6000));
    imago_state st;
    imago_session_get_state(s, &st);
    CHECK(st.video_transport == IMAGO_RTSP_TCP);
    imago_session_destroy(s);
}

TEST_CASE("video reconnects after the server restarts") {
    sim::RtspServer::Options options;
    options.port = 0;
    std::string error;
    auto server = std::make_unique<sim::RtspServer>();
    REQUIRE(server->start(options, &error));
    const uint16_t port = server->port();
    const std::string url = server->url();

    Callbacks cb;
    imago_session* s = startSession(cb, url, IMAGO_RTSP_UDP);
    REQUIRE(waitFor([&] { return cb.frames.count() >= 10; }, 4000));

    server.reset(); // stream stops
    options.port = port;
    server = std::make_unique<sim::RtspServer>();
    REQUIRE(server->start(options, &error));
    const size_t before = cb.frames.count();
    CHECK(waitFor([&] { return cb.frames.count() >= before + 10; }, 6000));
    CHECK(server->sessionsStarted() >= 1);
    imago_session_destroy(s);
}

TEST_CASE("video lag guard: a lasting queue restarts the session, a hiccup does not") {
    sim::RtspServer server;
    sim::RtspServer::Options options;
    options.port = 0;
    options.lagMs = 500;
    options.lagEverySec = 1;
    std::string error;

    SUBCASE("queue that never drains") {
        REQUIRE(server.start(options, &error));
        Callbacks cb;
        imago_session* s = startSession(cb, server.url(), IMAGO_RTSP_UDP, 0, 300);
        // Hiccup at 1 s, 2 s of late frames, then the restart.
        REQUIRE(waitFor([&] { return cb.events.has(IMAGO_EVENT_VIDEO_RESYNC); }, 6000));
        CHECK(waitFor([&] { return server.sessionsStarted() >= 2; }, 3000));
        imago_state st;
        imago_session_get_state(s, &st);
        CHECK(st.video_resyncs >= 1);
        for (const auto& e : cb.events.events()) {
            if (e.type == IMAGO_EVENT_VIDEO_RESYNC) {
                CHECK(e.code >= 300);
                CHECK(e.code < 1500);
            }
        }
        // The new session starts with no lag.
        const size_t before = cb.frames.count();
        REQUIRE(waitFor([&] { return cb.frames.count() >= before + 15; }, 3000));
        imago_session_get_state(s, &st);
        CHECK(st.video_lag_ms < 100.0f);
        imago_session_destroy(s);
    }
    SUBCASE("hiccup followed by a burst") {
        options.lagCatchUp = true;
        REQUIRE(server.start(options, &error));
        Callbacks cb;
        imago_session* s = startSession(cb, server.url(), IMAGO_RTSP_UDP, 0, 300);
        std::this_thread::sleep_for(std::chrono::milliseconds(4500));
        CHECK_FALSE(cb.events.has(IMAGO_EVENT_VIDEO_RESYNC));
        CHECK(server.sessionsStarted() == 1);
        imago_state st;
        imago_session_get_state(s, &st);
        CHECK(st.video_lag_ms < 100.0f);
        imago_session_destroy(s);
    }
}

TEST_CASE("video starts from the URL the payload announces") {
    sim::RtspServer server;
    sim::RtspServer::Options rtspOptions;
    rtspOptions.port = 0;
    std::string error;
    REQUIRE(server.start(rtspOptions, &error));

    sim::OrusSimulator simulator;
    sim::OrusSimulator::Options simOptions;
    simOptions.port = 0;
    // Announce a host the client cannot reach; rtsp_use_payload_host replaces it.
    simOptions.streamUri = "rtsp://10.255.255.1:" + std::to_string(server.port()) + "/live";
    REQUIRE(simulator.start(simOptions, &error));

    Callbacks cb;
    imago_session* s = startSession(cb, "", IMAGO_RTSP_UDP, simulator.port());
    REQUIRE(waitFor([&] { return cb.frames.count() >= 10; }, 5000));
    CHECK(cb.events.has(IMAGO_EVENT_STREAM_URI));
    imago_session_destroy(s);
    simulator.stop();
}
