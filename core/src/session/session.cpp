#include "session/session.h"

#include "platform/clock.h"
#include "platform/thread.h"
#include "rtsp/rtsp_url.h"
#include "util/text.h"
#include "web/http_client.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace imago {

namespace {

constexpr int64_t kHeartbeatPeriodNs = kNsPerSec;
// The app's own measurements go into a recording this often, as NAMED_VALUE_FLOAT.
constexpr int64_t kStatsRecordPeriodNs = kNsPerSec;
// Wi-Fi keep-awake: a datagram this often (the rate at which a moving joystick kept an iPad's
// radio awake in the field, 2026-10-14).
constexpr int64_t kKeepAwakePeriodNs = 33 * kNsPerMs;
constexpr int64_t kStreamInfoPeriodNs = 2 * kNsPerSec;
constexpr int64_t kStatusPeriodNs = 2 * kNsPerSec;
constexpr uint32_t kManagerStatusPeriodMs = 500;
// Lost SET_MESSAGE_INTERVAL commands are not acknowledged, so the whole set is repeated.
constexpr int64_t kIntervalsPeriodNs = 15 * kNsPerSec;
// The zoom mode decides the zoom range; re-read now and then in case another app changes it.
constexpr int64_t kZoomModeRetryNs = 2 * kNsPerSec;
constexpr int64_t kZoomModePeriodNs = 10 * kNsPerSec;
// Zoom level from a dragged control: at most one command per period, the final level twice.
constexpr int64_t kZoomTargetPeriodNs = 150'000'000;
constexpr int64_t kZoomRepeatDelayNs = 400'000'000;
// After this session stops a continuous zoom, the lens may coast this long.
constexpr int64_t kZoomCoastNs = 1'500'000'000;
// A range zoom lands somewhere unknown (imago-cli uses it to measure that); not judged meanwhile.
constexpr int64_t kZoomRangeCoastNs = 6 * kNsPerSec;
// A zoom move larger than this ratio that this session did not ask for is reported...
constexpr float kExternalZoomRatio = 1.15f;
// ...at most once per this period.
constexpr int64_t kExternalZoomQuietNs = 3 * kNsPerSec;
// Camera setting reads asked at once (a settings page) leave this far apart.
constexpr int64_t kParamReadSpacingNs = 40'000'000;
// How long the payload's tracker gets to follow a tracking command it accepted before it
// counts as ignored (its status streams at 20 Hz), and how far from the point asked for the
// box may sit: a tracker snaps to the object near the tap, not to the tap itself.
constexpr int64_t kTrackingApplyNs = 1'500'000'000;
constexpr float kTrackingPointSlack = 0.08f;
// A gimbal that turned this much since a tracking command was steering to the new target.
constexpr float kTrackingSlewDeg = 2.0f;
constexpr int kGimbalStopRepeats = 3;
constexpr float kMaxGimbalRateDps = 180.0f;
constexpr int64_t kLatencyWindowNs = 2 * kNsPerSec;
constexpr uint16_t kListenDefaultPort = 14550;

// Rates requested from the payload manager when the link comes up (milliseconds).
struct DataRate {
    orus::DataIndex index;
    uint32_t intervalMs;
};
constexpr DataRate kDataRates[] = {
    {orus::DataIndex::EoZoom, 250},     {orus::DataIndex::IrZoom, 500},
    {orus::DataIndex::LrfRange, 200},   {orus::DataIndex::TrackX, 50},
    {orus::DataIndex::TrackY, 50},      {orus::DataIndex::TrackW, 50},
    {orus::DataIndex::TrackH, 50},      {orus::DataIndex::TrackStatus, 50},
    {orus::DataIndex::ViewMode, 1000},  {orus::DataIndex::GimbalMode, 1000},
    {orus::DataIndex::IrTempMax, 1000}, {orus::DataIndex::IrTempMin, 1000},
    {orus::DataIndex::IrTempMean, 1000}, {orus::DataIndex::IrType, 5000},
    {orus::DataIndex::IrPalette, 1000},  {orus::DataIndex::IrFfcMode, 1000},
    {orus::DataIndex::TargetLon, 500},   {orus::DataIndex::TargetLat, 500},
    {orus::DataIndex::TargetAlt, 500},
};
constexpr uint32_t kDetectionIntervalMs = 100;

} // namespace

Session::Session(const imago_config& config, imago_event_fn onEvent, imago_video_frame_fn onFrame,
                 void* user)
    : config_(config), onEvent_(onEvent), onFrame_(onFrame), user_(user),
      model_(static_cast<int64_t>(config.link_timeout_ms) * kNsPerMs),
      commands_(static_cast<int64_t>(config.command_timeout_ms) * kNsPerMs,
                config.command_retries) {
    payloadIp_ = config.payload_ip != nullptr ? config.payload_ip : "";
    rtspUrl_ = config.rtsp_url != nullptr ? config.rtsp_url : "";
    config_.payload_ip = nullptr;
    config_.rtsp_url = nullptr;

    if (!payloadIp_.empty()) {
        remote_ = net::Endpoint::parse(payloadIp_, config.payload_port).value_or(net::Endpoint{});
    }
    localPort_ = config.local_port != 0
                     ? config.local_port
                     : (payloadIp_.empty() ? kListenDefaultPort : config.payload_port);
    gimbalPeriodNs_ = kNsPerSec / std::max<uint32_t>(config.gimbal_rate_hz, 1);
    gimbalProtocol_ = config.gimbal_protocol;
    webHost_ = config.web_host != nullptr && config.web_host[0] != '\0' ? config.web_host : payloadIp_;
    webPort_ = config.web_port != 0 ? config.web_port : web::kOrusWebPort;
    config_.web_host = nullptr;
}

Session::~Session() { stop(); }

int Session::start() {
    std::lock_guard lifecycle(lifecycleMutex_);
    if (running()) {
        return 0;
    }
    if (!payloadIp_.empty() && !remote_.valid()) {
        return IMAGO_ERR_INVALID_ARG;
    }
    std::string error;
    if (!link_.open(remote_, localPort_, config_.system_id, config_.component_id, &error)) {
        emit(IMAGO_EVENT_LINK_DOWN, 0, 0, IMAGO_ERR_SOCKET, error.c_str());
        return IMAGO_ERR_SOCKET;
    }
    const int64_t now = monotonicNs();
    nextHeartbeatNs_ = now;
    nextKeepAwakeNs_ = now;
    nextStatsRecordNs_ = now + kStatsRecordPeriodNs;
    nextStreamInfoNs_ = now;
    nextStatusNs_ = now;
    nextIntervalsNs_ = now;
    running_.store(true, std::memory_order_release);
    networkThread_ = std::thread([this] { networkLoop(); });
    webStopping_.store(false);
    webThread_ = std::thread([this] { webLoop(); });
    if (!webHost_.empty()) {
        media_.start(webHost_, webPort_);
    }

    // A configured URL (e.g. the Herelink stream) does not depend on the MAVLink link.
    if (config_.auto_start_video && !rtspUrl_.empty()) {
        startVideo(nullptr);
    }
    return 0;
}

void Session::stop() {
    std::lock_guard lifecycle(lifecycleMutex_);
    if (!running()) {
        return;
    }
    // Never leave the gimbal turning. Through the autopilot the payload's own heartbeat says
    // nothing about that path, so then the stop goes out whenever the link is open.
    const bool viaManager =
        gimbalProtocol_.load(std::memory_order_relaxed) == IMAGO_GIMBAL_PROTOCOL_MANAGER;
    if (link_.isOpen() && (model_.linkUp() || viaManager)) {
        std::lock_guard lock(gimbalMutex_);
        for (int i = 0; i < kGimbalStopRepeats; ++i) {
            sendGimbalRate(0, 0, monotonicNs());
        }
    }
    // Give the gimbal manager back, so the pilot or a GCS is not left without it. Whatever the
    // last status said (it may be seconds old): -3 only releases if we hold it, and it is
    // repeated because nothing will retry it after this.
    if (link_.isOpen() && viaManager) {
        for (int i = 0; i < kGimbalStopRepeats; ++i) {
            link_.send(orus::gimbalManagerReleaseControl(model_.targets()));
        }
    }
    {
        std::lock_guard lock(webMutex_);
        webJobs_.clear();
    }
    webStopping_.store(true);
    webWake_.notify_all();
    if (webThread_.joinable()) {
        webThread_.join(); // a request in flight gives up within 100 ms, or its connect within 1.5 s
    }
    media_.stop();
    // Network thread first, so a late stream announcement cannot restart video.
    running_.store(false, std::memory_order_release);
    if (networkThread_.joinable()) {
        networkThread_.join();
    }
    stopVideo();
    link_.close();
    {
        std::lock_guard lock(paramReadMutex_);
        paramReads_.clear(); // a restarted session reads what its own screens ask for
    }
    stopRecording();
    for (const auto& r : commands_.clear()) {
        emitResult(r);
    }
}

// ----------------------------------------------------------------------------------------------
// Network thread

void Session::networkLoop() {
    configureCurrentThread("imago-mavlink", ThreadRole::Network);
    const TimerResolution timerResolution; // for as long as the session runs
    std::vector<PayloadModel::Notice> notices;
    std::vector<CommandManager::Message> resend;
    std::vector<CommandManager::Result> expired;

    while (running()) {
        // Wake often enough for the gimbal rate period while the gimbal is commanded.
        int timeoutMs = 20;
        {
            std::lock_guard lock(gimbalMutex_);
            if (gimbalPitchDps_ != 0 || gimbalYawDps_ != 0 || gimbalStopRepeats_ > 0) {
                timeoutMs = 5;
            }
        }
        if (config_.wifi_keep_awake) {
            const int64_t untilNs = nextKeepAwakeNs_ - monotonicNs();
            timeoutMs = static_cast<int>(std::clamp<int64_t>(untilNs / kNsPerMs, 1, timeoutMs));
        }
        const int handled = link_.poll(timeoutMs, [this](const mavlink_message_t& msg) {
            handleMessage(msg, monotonicNs());
        });
        if (handled < 0) {
            emit(IMAGO_EVENT_LINK_DOWN, 0, 0, IMAGO_ERR_SOCKET, "MAVLink socket error");
            break;
        }

        const int64_t now = monotonicNs();
        if (now >= nextHeartbeatNs_) {
            link_.send(orus::heartbeat());
            nextHeartbeatNs_ = now + kHeartbeatPeriodNs;
        }
        if (now >= nextStatsRecordNs_) {
            recordStats(now);
            nextStatsRecordNs_ = now + kStatsRecordPeriodNs;
        }
        if (config_.wifi_keep_awake && now >= nextKeepAwakeNs_) {
            link_.sendKeepAwake(config_.wifi_keep_awake_port);
            // Keeps the period despite late wake-ups, without catching up a backlog.
            nextKeepAwakeNs_ = std::max(nextKeepAwakeNs_ + kKeepAwakePeriodNs, now);
        }

        notices.clear();
        model_.tick(now, notices);
        for (const auto& n : notices) {
            if (n.kind == PayloadModel::Notice::Kind::LinkDown) {
                onLinkDown();
            }
        }

        resend.clear();
        expired.clear();
        commands_.tick(now, resend, expired);
        for (const auto& m : resend) {
            std::visit([this](const auto& msg) { link_.send(msg); }, m);
        }
        for (const auto& r : expired) {
            emitResult(r);
        }

        serviceGimbalRate(now);
        serviceZoomTarget(now);
        watchZoom(now);
        watchTracking(now);
        if (model_.linkUp()) {
            serviceParamReads(now);
            periodicRequests(now);
        }
    }
}

void Session::handleMessage(const mavlink_message_t& msg, int64_t nowNs) {
    switch (msg.msgid) {
    case MAVLINK_MSG_ID_COMMAND_ACK: {
        mavlink_command_ack_t ack;
        mavlink_msg_command_ack_decode(&msg, &ack);
        // Only the payload's and the autopilot's (gimbal manager) acknowledgements addressed
        // to us. 0 means unset or broadcast.
        // The autopilot's only for the gimbal manager's control command, so it cannot settle
        // a payload command that shares an id.
        const auto targets = model_.targets();
        const bool fromAutopilot =
            msg.sysid == targets.autopilotSystem && msg.compid == targets.autopilotComponent &&
            ack.command == MAV_CMD_DO_GIMBAL_MANAGER_CONFIGURE &&
            gimbalProtocol_.load(std::memory_order_relaxed) == IMAGO_GIMBAL_PROTOCOL_MANAGER;
        if (!orus::isPayloadComponent(msg.compid) && !fromAutopilot) {
            break;
        }
        if ((ack.target_system != 0 && ack.target_system != config_.system_id) ||
            (ack.target_component != 0 && ack.target_component != config_.component_id)) {
            // Someone else commands the payload (the autopilot, a GCS): worth knowing when the
            // camera does things nobody here asked for.
            const std::string from = std::to_string(ack.target_system) + "/" +
                                     std::to_string(ack.target_component);
            if (!fromAutopilot) {
                emit(IMAGO_EVENT_FOREIGN_COMMAND, 0, ack.command, ack.result, from.c_str());
            }
            break;
        }
        if (auto r = commands_.onCommandAck(ack, nowNs)) {
            emitResult(*r);
        }
        break;
    }
    case MAVLINK_MSG_ID_PARAM_EXT_ACK: {
        mavlink_param_ext_ack_t ack;
        mavlink_msg_param_ext_ack_decode(&msg, &ack);
        if (!orus::isPayloadComponent(msg.compid)) {
            break;
        }
        if (auto r = commands_.onParamExtAck(ack, nowNs)) {
            emitResult(*r);
        }
        if (ack.param_result == PARAM_ACK_ACCEPTED) {
            // Read it back rather than trust the ack's value field, which the Orus L may not
            // fill (unverified); the answer comes as PARAM_EXT_VALUE. Not the zoom levels a
            // dragged slider streams: the lens reports where it is anyway.
            const std::string_view id(ack.param_id, strnlen(ack.param_id, sizeof(ack.param_id)));
            if (id != orus::zoomLevelParam(IMAGO_ZOOM_MODE_COMBINED) &&
                id != orus::zoomLevelParam(IMAGO_ZOOM_MODE_SUPER_RESOLUTION)) {
                readCameraParam(id);
            }
        }
        break;
    }
    case MAVLINK_MSG_ID_PARAM_EXT_VALUE: {
        mavlink_param_ext_value_t value;
        mavlink_msg_param_ext_value_decode(&msg, &value);
        if (orus::isPayloadComponent(msg.compid)) {
            emitCameraParam(value.param_id, value.param_value);
        }
        break;
    }
    default:
        break;
    }

    std::vector<PayloadModel::Notice> notices;
    model_.handle(msg, nowNs, notices);
    for (const auto& n : notices) {
        switch (n.kind) {
        case PayloadModel::Notice::Kind::LinkUp:
            onLinkUp(nowNs);
            break;
        case PayloadModel::Notice::Kind::LinkDown:
            onLinkDown();
            break;
        case PayloadModel::Notice::Kind::StreamUri:
            onStreamUri(n.text, n.code);
            break;
        case PayloadModel::Notice::Kind::StatusText:
            emit(IMAGO_EVENT_STATUS_TEXT, 0, n.code, 0, n.text.c_str());
            break;
        }
    }
}

void Session::onLinkUp(int64_t nowNs) {
    emit(IMAGO_EVENT_LINK_UP, 0, 0, 0, nullptr);
    // The lens may have moved while the link was down: judge zoom moves from the first level
    // the payload reports now (the model forgets the old one on link down).
    zoomRefLevel_ = 0;
    const auto t = model_.targets();
    link_.send(orus::requestComponentInformation(t));
    link_.send(orus::requestStorageInformation(t));
    link_.send(orus::requestCaptureStatus(t));
    link_.send(orus::requestFieldOfView(t, 0));
    sendDataIntervals();
    nextIntervalsNs_ = nowNs + kIntervalsPeriodNs;
    nextStreamInfoNs_ = nowNs; // ask for the video URL right away
    nextStatusNs_ = nowNs + kStatusPeriodNs;
    nextZoomModeNs_ = nowNs;
}

void Session::onLinkDown() {
    clearTrackingExpectation(); // the tracker's state is unknown from here on
    emit(IMAGO_EVENT_LINK_DOWN, 0, 0, 0, nullptr);
    for (const auto& r : commands_.clear()) {
        emitResult(r);
    }
}

void Session::sendDataIntervals() {
    const auto t = model_.targets();
    for (const auto& rate : kDataRates) {
        link_.send(orus::setDataInterval(t, static_cast<uint32_t>(rate.index), rate.intervalMs));
    }
    link_.send(orus::setDataInterval(t, MAVLINK_MSG_ID_V2_EXTENSION, kDetectionIntervalMs));
}

void Session::periodicRequests(int64_t nowNs) {
    const auto t = model_.targets();
    if (rtspUrl_.empty() && !model_.hasStreamUri() && nowNs >= nextStreamInfoNs_) {
        link_.send(orus::requestVideoStreamInformation(t));
        nextStreamInfoNs_ = nowNs + kStreamInfoPeriodNs;
    }
    if (nowNs >= nextStatusNs_) {
        link_.send(orus::requestCaptureStatus(t));
        link_.send(orus::requestStorageInformation(t));
        if (gimbalProtocol_.load(std::memory_order_relaxed) == IMAGO_GIMBAL_PROTOCOL_MANAGER) {
            // Who holds the gimbal, twice a second. Repeated: the autopilot may come up later.
            link_.send(orus::requestGimbalManagerStatus(t, kManagerStatusPeriodMs));
        }
        nextStatusNs_ = nowNs + kStatusPeriodNs;
    }
    if (nowNs >= nextIntervalsNs_) {
        sendDataIntervals();
        nextIntervalsNs_ = nowNs + kIntervalsPeriodNs;
    }
    if (nowNs >= nextZoomModeNs_) {
        link_.send(orus::requestCameraParam(t, orus::param::kZoomMode));
        nextZoomModeNs_ = nowNs + (model_.zoomModeKnown() ? kZoomModePeriodNs : kZoomModeRetryNs);
    }
}

// ----------------------------------------------------------------------------------------------
// Events

void Session::emit(imago_event_type type, uint32_t requestId, uint32_t code, int32_t result,
                   const char* text) {
    if (onEvent_ == nullptr) {
        return;
    }
    imago_event event{type, requestId, code, result, text};
    onEvent_(&event, user_);
}

void Session::emitResult(const CommandManager::Result& r) {
    if (r.result != IMAGO_RESULT_TIMEOUT) {
        std::lock_guard lock(metricsMutex_);
        rttLastMs_ = r.roundTripMs;
        rttAvgMs_ = rttAvgMs_ == 0 ? r.roundTripMs : rttAvgMs_ * 0.8f + r.roundTripMs * 0.2f;
        commands_.setRoundTrip(static_cast<int64_t>(rttAvgMs_ * 1e6f));
    }
    if (r.isParam) {
        emit(IMAGO_EVENT_PARAM_RESULT, r.requestId, 0, r.result, r.paramId.c_str());
    } else {
        {
            std::lock_guard lock(trackingMutex_);
            if (tracking_.pending && r.requestId == tracking_.requestId) {
                // Rejected or unanswered: that result is the news; only an accepted command
                // the tracker then ignores is worth a second event. The clock starts at the
                // acceptance, so a retried command gets its full time.
                tracking_.accepted = r.result == IMAGO_RESULT_ACCEPTED;
                tracking_.pending = tracking_.accepted;
                tracking_.acceptedNs = monotonicNs();
            }
        }
        emit(IMAGO_EVENT_COMMAND_RESULT, r.requestId, r.command, r.result, nullptr);
    }
}

// What the app measures (video, latencies, link) is not on the wire, so a recording gets it
// once a second as NAMED_VALUE_FLOAT from this component: one file holds everything, and
// any MAVLink tool plots it. Never sent.
void Session::recordStats(int64_t nowNs) {
    std::shared_ptr<TlogWriter> recorder;
    {
        std::lock_guard lock(recordingMutex_);
        recorder = recorder_;
    }
    if (!recorder || compactRecording_.load()) {
        return;
    }
    imago_state st;
    getState(st);
    const auto ms = static_cast<uint32_t>(nowNs / kNsPerMs);
    const struct {
        char name[11]; // the message copies 10 bytes: shorter names are zero padded here
        float value;
    } values[] = {
        {"vid_kbps", st.video_bitrate_kbps},
        {"vid_fps", st.video_fps},
        {"vid_gap95", st.video_frame_gap_p95_ms},
        {"vid_gapmax", st.video_frame_gap_max_ms},
        {"vid_lag_ms", st.video_lag_ms},
        {"vid_jitter", st.video_jitter_ms},
        {"vid_lost", static_cast<float>(st.video_packets_lost)},
        {"vid_decode", st.latency_decode_ms},
        {"vid_shown", st.latency_present_ms},
        {"cmd_rtt_ms", st.command_rtt_ms_avg},
        {"mav_gaps", static_cast<float>(st.mavlink_rx_dropped)},
        {"gb_pan_deg", st.gimbal_pan_valid ? st.gimbal_pan_deg : NAN},
    };
    uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
    for (const auto& v : values) {
        mavlink_message_t message;
        // Its own sequence counter: the sent stream's must not show gaps for these.
        mavlink_msg_named_value_float_pack_status(config_.system_id, config_.component_id,
                                                  &statsStatus_, &message, ms, v.name, v.value);
        const uint16_t length = mavlink_msg_to_send_buffer(buffer, &message);
        recorder->record(buffer, length);
    }
}

// Called before the command goes out (sendTracked's hook), so its answer cannot overtake it
// and the "before" snapshot predates it.
void Session::expectTracking(bool stop, uint32_t requestId, float x, float y) {
    TrackingExpectation e;
    e.pending = true;
    e.stop = stop;
    e.requestId = requestId;
    e.x = x;
    e.y = y;
    e.stateBefore = model_.trackingState();
    const int64_t now = monotonicNs();
    e.boxBefore = model_.trackingBoxCentre(now, e.boxXBefore, e.boxYBefore);
    imago_state st;
    model_.snapshot(now, st);
    e.gimbalBefore = st.gimbal_yaw_valid;
    e.pitchBefore = st.gimbal_pitch_deg;
    e.yawBefore = st.gimbal_yaw_deg;
    std::lock_guard lock(trackingMutex_);
    tracking_ = e;
}

void Session::clearTrackingExpectation() {
    std::lock_guard lock(trackingMutex_);
    tracking_.pending = false;
}

void Session::watchTracking(int64_t nowNs) {
    TrackingExpectation e;
    {
        std::lock_guard lock(trackingMutex_);
        if (!tracking_.pending || !tracking_.accepted ||
            nowNs - tracking_.acceptedNs < kTrackingApplyNs) {
            return; // nothing, not answered yet (its timeout reports that), or still early
        }
        e = tracking_;
        tracking_.pending = false;
    }
    if (!model_.linkUp()) {
        return;
    }
    const int32_t state = model_.trackingState();
    bool followed = false;
    if (e.stop) {
        followed = state != IMAGO_TRACKING_TRACKED;
    } else if (state == IMAGO_TRACKING_TRACKED) {
        // A tracker that just started obeyed. One that was already tracking obeyed when its box
        // sits near the point asked for, or when the gimbal turned: with a point far from the
        // centre the gimbal brings the new target in, so 1.5 s on the box is already on its way
        // to the centre, which is where the old target sat (field report 2026-10-16: every far
        // point raised the warning).
        float x = 0, y = 0;
        const bool box = model_.trackingBoxCentre(nowNs, x, y);
        imago_state st;
        model_.snapshot(nowNs, st);
        const bool turned = e.gimbalBefore && st.gimbal_yaw_valid &&
                            (std::fabs(st.gimbal_pitch_deg - e.pitchBefore) >= kTrackingSlewDeg ||
                             std::fabs(std::remainder(st.gimbal_yaw_deg - e.yawBefore, 360.0f)) >=
                                 kTrackingSlewDeg);
        followed = e.stateBefore != IMAGO_TRACKING_TRACKED || turned ||
                   (box && std::fabs(x - e.x) <= kTrackingPointSlack &&
                    std::fabs(y - e.y) <= kTrackingPointSlack);
    }
    if (!followed) {
        emit(IMAGO_EVENT_TRACKING_IGNORED, e.requestId, static_cast<uint32_t>(state), 0,
             e.stop ? "stop" : "track");
    }
}

// ----------------------------------------------------------------------------------------------
// Video

void Session::ensureVideoReceiver() {
    if (video_) {
        return;
    }
    video_ = std::make_unique<VideoReceiver>(
        config_.rtsp_transport, config_.nal_format,
        [this](const imago_video_frame& frame) {
            if (onFrame_ != nullptr) {
                onFrame_(&frame, user_);
            }
        },
        [this](imago_event_type type, const std::string& text, uint32_t code) {
            emit(type, 0, code, 0, text.c_str());
        },
        VideoReceiver::LagGuard{config_.video_resync_on_lag,
                                static_cast<int64_t>(config_.video_max_lag_ms) * kNsPerMs});
    video_->setSmoothing(static_cast<int64_t>(config_.video_smoothing_ms) * kNsPerMs);
}

int Session::startVideo(const char* url) {
    if (!running()) {
        return IMAGO_ERR_STATE;
    }
    std::lock_guard lock(videoMutex_);
    std::string target = url != nullptr && url[0] != '\0' ? std::string(url) : rtspUrl_;
    if (target.empty()) {
        target = announcedUrl_;
    }
    if (target.empty() || !RtspUrl::parse(target)) {
        return IMAGO_ERR_INVALID_ARG;
    }
    ensureVideoReceiver();
    video_->start(target);
    return 0;
}

void Session::stopVideo() {
    std::unique_ptr<VideoReceiver> receiver;
    {
        std::lock_guard lock(videoMutex_);
        receiver = std::move(video_);
    }
    if (receiver) {
        receiver->stop(); // joins the video thread outside the lock
    }
}

void Session::onStreamUri(const std::string& uri, uint32_t streamId) {
    emit(IMAGO_EVENT_STREAM_URI, 0, streamId, 0, uri.c_str());
    std::string url = uri;
    if (config_.rtsp_use_payload_host && !payloadIp_.empty()) {
        if (auto parsed = RtspUrl::parse(uri)) {
            url = parsed->withHost(payloadIp_).toString();
        }
    }
    bool start = false;
    {
        std::lock_guard lock(videoMutex_);
        if (announcedUrl_.empty()) {
            announcedUrl_ = url;
            start = config_.auto_start_video && rtspUrl_.empty() && !video_;
        }
    }
    if (start) {
        startVideo(nullptr);
    }
}

void Session::reportPresented(int64_t receiveNs, int64_t decodedNs, int64_t presentedNs) {
    std::lock_guard lock(metricsMutex_);
    const int64_t now = monotonicNs();
    if (decodedNs > receiveNs) {
        decodeMs_.add(now, static_cast<float>(decodedNs - receiveNs) / 1e6f);
    }
    if (presentedNs > receiveNs) {
        presentMs_.add(now, static_cast<float>(presentedNs - receiveNs) / 1e6f);
    }
}

// ----------------------------------------------------------------------------------------------
// State

void Session::getState(imago_state& out) {
    std::memset(&out, 0, sizeof(out));
    const int64_t now = monotonicNs();
    model_.snapshot(now, out);
    if (out.gimbal_control == IMAGO_GIMBAL_CONTROL_OTHER &&
        out.gimbal_control_system == config_.system_id &&
        out.gimbal_control_component == config_.component_id) {
        out.gimbal_control = IMAGO_GIMBAL_CONTROL_THIS_APP;
    }
    out.mavlink_rx_messages = link_.messagesReceived();
    out.mavlink_tx_messages = link_.messagesSent();
    out.mavlink_rx_dropped = link_.messagesDropped();
    out.commands_pending = static_cast<uint32_t>(commands_.pending());
    const auto peer = link_.peer();
    if (peer.valid()) {
        text::copyTo(out.mavlink_peer, peer.toString());
    }
    {
        std::lock_guard lock(metricsMutex_);
        out.command_rtt_ms_last = rttLastMs_;
        out.command_rtt_ms_avg = rttAvgMs_;
        out.latency_decode_ms = decodeMs_.percentile(now, kLatencyWindowNs, 0.95f);
        out.latency_present_ms = presentMs_.percentile(now, kLatencyWindowNs, 0.95f);
    }
    std::lock_guard lock(videoMutex_);
    if (video_) {
        video_->snapshot(now, out);
    }
}

// ----------------------------------------------------------------------------------------------
// Commands

uint32_t Session::sendTracked(CommandManager::Message message, int retries,
                              const std::function<void(uint32_t)>& registered) {
    if (!running()) {
        return 0;
    }
    const uint32_t id = commands_.add(message, monotonicNs(), retries);
    if (registered) {
        registered(id);
    }
    std::visit([this](const auto& msg) { link_.send(msg); }, message);
    return id;
}

void Session::sendGimbalRate(float pitchDps, float yawDps, int64_t nowNs) {
    // Streamed, not tracked: the next command supersedes a lost one.
    switch (gimbalProtocol_.load(std::memory_order_relaxed)) {
    case IMAGO_GIMBAL_PROTOCOL_V1:
        link_.send(orus::mountControl(model_.targets(), pitchDps, yawDps, true));
        break;
    case IMAGO_GIMBAL_PROTOCOL_MANAGER:
        link_.send(orus::gimbalManagerRate(model_.targets(), model_.gimbalFlags(), pitchDps, yawDps));
        break;
    default:
        link_.send(orus::gimbalRate(model_.targets(), model_.gimbalFlags(), pitchDps, yawDps));
        break;
    }
    gimbalLastSentNs_ = nowNs;
}

void Session::setGimbalProtocol(imago_gimbal_protocol protocol) {
    if (protocol == IMAGO_GIMBAL_PROTOCOL_V1 || protocol == IMAGO_GIMBAL_PROTOCOL_V2 ||
        protocol == IMAGO_GIMBAL_PROTOCOL_MANAGER) {
        gimbalProtocol_.store(protocol, std::memory_order_relaxed);
    }
}

void Session::setGimbalRate(float pitchDps, float yawDps) {
    if (!running()) {
        return;
    }
    pitchDps = std::isfinite(pitchDps) ? std::clamp(pitchDps, -kMaxGimbalRateDps, kMaxGimbalRateDps) : 0;
    yawDps = std::isfinite(yawDps) ? std::clamp(yawDps, -kMaxGimbalRateDps, kMaxGimbalRateDps) : 0;
    const int64_t now = monotonicNs();
    std::lock_guard lock(gimbalMutex_);
    const bool wasMoving = gimbalPitchDps_ != 0 || gimbalYawDps_ != 0;
    const bool moving = pitchDps != 0 || yawDps != 0;
    gimbalPitchDps_ = pitchDps;
    gimbalYawDps_ = yawDps;
    if (!moving) {
        if (wasMoving) {
            // Stop now on the caller's thread, then repeat from the network thread.
            sendGimbalRate(0, 0, now);
            gimbalStopRepeats_ = kGimbalStopRepeats - 1;
        }
        return;
    }
    gimbalStopRepeats_ = 0;
    // Start immediately; while moving, send at most twice per period from here.
    if (!wasMoving || now - gimbalLastSentNs_ >= gimbalPeriodNs_ / 2) {
        sendGimbalRate(pitchDps, yawDps, now);
    }
}

void Session::serviceGimbalRate(int64_t nowNs) {
    std::lock_guard lock(gimbalMutex_);
    if (nowNs - gimbalLastSentNs_ < gimbalPeriodNs_) {
        return;
    }
    if (gimbalPitchDps_ != 0 || gimbalYawDps_ != 0) {
        sendGimbalRate(gimbalPitchDps_, gimbalYawDps_, nowNs);
    } else if (gimbalStopRepeats_ > 0) {
        sendGimbalRate(0, 0, nowNs);
        --gimbalStopRepeats_;
    }
}

uint32_t Session::setGimbalAngle(float pitchDeg, float yawDeg) {
    if (!running()) {
        return 0;
    }
    if (gimbalProtocol_.load(std::memory_order_relaxed) == IMAGO_GIMBAL_PROTOCOL_V1) {
        return sendTracked(orus::mountControl(model_.targets(), pitchDeg, yawDeg, false));
    }
    if (gimbalProtocol_.load(std::memory_order_relaxed) == IMAGO_GIMBAL_PROTOCOL_MANAGER) {
        return link_.send(orus::gimbalManagerAngle(model_.targets(), model_.gimbalFlags(), pitchDeg,
                                                   yawDeg))
                   ? 1u
                   : 0u;
    }
    return link_.send(orus::gimbalAngle(model_.targets(), model_.gimbalFlags(), pitchDeg, yawDeg))
               ? 1u
               : 0u;
}

uint32_t Session::takeGimbalControl(bool take) {
    if (!running()) {
        return 0;
    }
    const auto t = model_.targets();
    return sendTracked(take ? orus::gimbalManagerTakeControl(t, config_.system_id, config_.component_id)
                            : orus::gimbalManagerReleaseControl(t));
}

uint32_t Session::setGimbalMode(imago_gimbal_mode mode) {
    if (mode < IMAGO_GIMBAL_MODE_OFF || mode > IMAGO_GIMBAL_MODE_RESET) {
        return 0;
    }
    if (mode == IMAGO_GIMBAL_MODE_RESET &&
        gimbalProtocol_.load(std::memory_order_relaxed) == IMAGO_GIMBAL_PROTOCOL_MANAGER) {
        // Through the manager, so only one controller commands the gimbal: the payload's own
        // reset (GB_MODE 4) while the autopilot kept re-sending "hold" sent an Orus L into a
        // full-travel pitch oscillation (flight 2026-10-15). Yaw 0 against the nose, whatever
        // frame the gimbal is in.
        const uint32_t flags = (model_.gimbalFlags() & ~GIMBAL_MANAGER_FLAGS_YAW_LOCK) |
                               GIMBAL_MANAGER_FLAGS_YAW_IN_VEHICLE_FRAME;
        return link_.send(orus::gimbalManagerAngle(model_.targets(), flags, 0, 0)) ? 1u : 0u;
    }
    return setCameraParam(orus::param::kGimbalMode, static_cast<uint32_t>(mode));
}

uint32_t Session::setCameraMode(imago_camera_mode mode) {
    const uint32_t id = sendTracked(orus::commandLong(
        model_.targets().cameraSystem, model_.targets().cameraComponent, MAV_CMD_SET_CAMERA_MODE,
        {0, static_cast<float>(mode), 0, 0, 0, 0, 0}));
    if (id != 0) {
        model_.setCameraMode(mode);
    }
    return id;
}

uint32_t Session::takePhoto() {
    if (model_.cameraMode() != IMAGO_CAMERA_MODE_PHOTO) {
        setCameraMode(IMAGO_CAMERA_MODE_PHOTO);
    }
    // Never resent: a lost ACK must not produce extra photos.
    return sendTracked(orus::takePhoto(model_.targets()), 0);
}

uint32_t Session::setRecording(bool recording) {
    if (recording) {
        if (model_.cameraMode() != IMAGO_CAMERA_MODE_VIDEO) {
            setCameraMode(IMAGO_CAMERA_MODE_VIDEO);
        }
        return sendTracked(orus::startRecording(model_.targets()), 0);
    }
    // Stopping is safe to repeat and must not be lost.
    return sendTracked(orus::stopRecording(model_.targets()));
}

uint32_t Session::zoom(int direction) {
    {
        // A continuous zoom takes over from a dragged level.
        std::lock_guard lock(zoomMutex_);
        zoomTarget_ = 0;
        zoomDirty_ = false;
        zoomRepeat_ = false;
    }
    noteOwnZoom(direction, 0, direction == 0 ? kZoomCoastNs : 0);
    return sendTracked(orus::zoomContinuous(model_.targets(), direction));
}

float Session::zoomRangeMax() const {
    const float max = model_.zoomMax();
    return max > 0 ? max : orus::kZoomCombinedMax;
}

float Session::clampZoom(float level) const { return std::clamp(level, 1.0f, zoomRangeMax()); }

int32_t Session::zoomMode() const {
    // The range tells the mode apart; unknown counts as combined, the camera's default.
    return zoomRangeMax() <= IMAGO_EO_ZOOM_OPTICAL_MAX ? IMAGO_ZOOM_MODE_SUPER_RESOLUTION
                                                       : IMAGO_ZOOM_MODE_COMBINED;
}

float Session::snapZoom(float level) const {
    const auto levels = orus::zoomLevels(zoomMode());
    return levels[orus::nearestZoomIndex(levels, clampZoom(level))];
}

mavlink_param_ext_set_t Session::zoomCommand(float level) const {
    // The camera's own level setting: the range zoom did not land on intermediate levels on a
    // real Orus L (field test 2026-10-14).
    const int32_t mode = zoomMode();
    const auto index = orus::nearestZoomIndex(orus::zoomLevels(mode), level);
    return orus::setCameraParam(model_.targets(), orus::zoomLevelParam(mode),
                                static_cast<uint32_t>(index));
}

uint32_t Session::zoomRange(float percent) {
    if (!std::isfinite(percent)) {
        return 0;
    }
    noteOwnZoom(0, 0, kZoomRangeCoastNs); // where it lands is what is being measured
    return sendTracked(orus::zoomRange(model_.targets(), percent));
}

uint32_t Session::zoomTo(float level) {
    if (!std::isfinite(level) || level < 1.0f) {
        return 0;
    }
    level = snapZoom(level);
    noteOwnZoom(0, level, 0);
    return sendTracked(zoomCommand(level));
}

void Session::setZoomTarget(float level) {
    if (!running() || !std::isfinite(level)) {
        return;
    }
    level = snapZoom(level);
    noteOwnZoom(0, level, 0);
    const int64_t now = monotonicNs();
    std::lock_guard lock(zoomMutex_);
    if (level == zoomTarget_) {
        return;
    }
    zoomTarget_ = level;
    zoomRepeat_ = true;
    // Send now on the caller's thread when allowed, so the first move has no added delay.
    if (now - zoomLastSentNs_ >= kZoomTargetPeriodNs) {
        link_.send(zoomCommand(level));
        zoomLastSentNs_ = now;
        zoomDirty_ = false;
    } else {
        zoomDirty_ = true;
    }
}

void Session::serviceZoomTarget(int64_t nowNs) {
    std::lock_guard lock(zoomMutex_);
    if (zoomDirty_) {
        if (nowNs - zoomLastSentNs_ >= kZoomTargetPeriodNs) {
            link_.send(zoomCommand(zoomTarget_));
            zoomLastSentNs_ = nowNs;
            zoomDirty_ = false;
        }
    } else if (zoomRepeat_ && nowNs - zoomLastSentNs_ >= kZoomRepeatDelayNs) {
        // Once more in case the last one was lost: the level is absolute, so a repeat is harmless.
        link_.send(zoomCommand(zoomTarget_));
        zoomLastSentNs_ = nowNs;
        zoomRepeat_ = false;
    }
}

uint32_t Session::focus(int direction) {
    return sendTracked(orus::focusContinuous(model_.targets(), direction));
}

uint32_t Session::autoFocus() { return sendTracked(orus::autoFocus(model_.targets())); }

void Session::noteOwnZoom(int direction, float target, int64_t coastNs) {
    ownZoomDirection_.store(direction, std::memory_order_relaxed);
    ownZoomTarget_.store(target, std::memory_order_relaxed);
    ownZoomGraceUntilNs_.store(coastNs > 0 ? monotonicNs() + coastNs : 0, std::memory_order_relaxed);
    ownZoomSerial_.fetch_add(1, std::memory_order_release);
}

void Session::watchZoom(int64_t nowNs) {
    const float level = model_.zoomLevel();
    if (!(level > 0)) {
        return;
    }
    const uint32_t serial = ownZoomSerial_.load(std::memory_order_acquire);
    const int direction = ownZoomDirection_.load(std::memory_order_relaxed);
    const float target = ownZoomTarget_.load(std::memory_order_relaxed);
    const bool fresh = serial != watchedZoomSerial_ || zoomRefLevel_ <= 0;
    if (serial != watchedZoomSerial_) {
        watchedZoomSerial_ = serial;
        zoomIntentLost_ = false;
    }
    if (fresh || nowNs < ownZoomGraceUntilNs_.load(std::memory_order_relaxed) ||
        nowNs < externalZoomQuietUntilNs_) {
        zoomRefLevel_ = level;
        zoomRefDistance_ = target > 0 ? std::fabs(std::log(level / target)) : 0;
        return;
    }
    const float tolerance = std::log(kExternalZoomRatio);
    bool unexpected = false;
    if (zoomIntentLost_ || (direction == 0 && target <= 0)) {
        // Holding still: any real move is someone else's.
        unexpected = std::fabs(std::log(level / zoomRefLevel_)) > tolerance;
    } else if (direction != 0) {
        // Our continuous zoom: further along is ours, a way back is not.
        if ((direction > 0) == (level > zoomRefLevel_)) {
            zoomRefLevel_ = level;
        } else {
            unexpected = std::fabs(std::log(level / zoomRefLevel_)) > tolerance;
        }
    } else {
        // Heading to our level: closer is ours, a way away from it is not.
        const float distance = std::fabs(std::log(level / target));
        if (distance <= zoomRefDistance_) {
            zoomRefDistance_ = distance;
            zoomRefLevel_ = level;
        } else {
            unexpected = distance > zoomRefDistance_ + tolerance;
        }
    }
    if (!unexpected) {
        return;
    }
    char text[48];
    std::snprintf(text, sizeof(text), "%.1fx -> %.1fx", static_cast<double>(zoomRefLevel_),
                  static_cast<double>(level));
    emit(IMAGO_EVENT_ZOOM_CHANGED_EXTERNALLY, 0, static_cast<uint32_t>(std::lround(level * 10)),
         0, text);
    zoomIntentLost_ = true; // our level no longer stands; judge from where it is now
    zoomRefLevel_ = level;
    externalZoomQuietUntilNs_ = nowNs + kExternalZoomQuietNs;
}

int Session::startRecording(const std::string& path) {
    auto recorder = std::make_shared<TlogWriter>();
    if (path.empty() || !recorder->open(path, nullptr)) {
        return IMAGO_ERR_INVALID_ARG;
    }
    std::shared_ptr<TlogWriter> previous;
    {
        std::lock_guard lock(recordingMutex_);
        previous = std::exchange(recorder_, recorder);
        link_.setRecorder(std::move(recorder));
    }
    if (previous) {
        previous->close();
    }
    return 0;
}

void Session::setRecordingDetail(bool compact) {
    compactRecording_.store(compact);
    link_.setRecordingFilter(compact ? compactFilter_ : nullptr);
}

int Session::stopRecording() {
    std::shared_ptr<TlogWriter> recorder;
    {
        std::lock_guard lock(recordingMutex_);
        recorder.swap(recorder_);
        link_.setRecorder(nullptr);
    }
    if (!recorder) {
        return 0;
    }
    recorder->close(); // a send racing with this sees a closed writer and skips it
    return recorder->complete() ? 0 : IMAGO_ERR_SOCKET;
}

int Session::readCameraParam(std::string_view id) {
    if (!running()) {
        return IMAGO_ERR_STATE;
    }
    if (id.empty() || id.size() > 16) {
        return IMAGO_ERR_INVALID_ARG;
    }
    std::lock_guard lock(paramReadMutex_);
    if (std::find(paramReads_.begin(), paramReads_.end(), id) == paramReads_.end()) {
        paramReads_.emplace_back(id);
    }
    return 0;
}

void Session::serviceParamReads(int64_t nowNs) {
    std::string id;
    {
        std::lock_guard lock(paramReadMutex_);
        if (paramReads_.empty() || nowNs < nextParamReadNs_) {
            return;
        }
        id = std::move(paramReads_.front());
        paramReads_.pop_front();
    }
    nextParamReadNs_ = nowNs + kParamReadSpacingNs;
    link_.send(orus::requestCameraParam(model_.targets(), id));
}

void Session::emitCameraParam(const char (&id)[16], const char (&value)[128]) {
    const std::string name(id, strnlen(id, sizeof(id)));
    emit(IMAGO_EVENT_CAMERA_PARAM, 0, orus::paramExtUint(value), 0, name.c_str());
}

uint32_t Session::setCameraParam(std::string_view id, uint32_t value) {
    if (id.empty() || id.size() > 16) {
        return 0;
    }
    if (id == orus::param::kZoomMode) {
        noteOwnZoom(0, 0, kZoomRangeCoastNs); // a new mode can move the lens (300x -> 25x)
    }
    return sendTracked(orus::setCameraParam(model_.targets(), id, value));
}

uint32_t Session::setTrackingMode(imago_tracking_mode mode) {
    if (mode < IMAGO_TRACKING_STOP || mode > IMAGO_TRACKING_EAGLE_EYE) {
        return 0;
    }
    if (mode != IMAGO_TRACKING_STOP) {
        clearTrackingExpectation(); // a new intent; trackAt() sets its own right after
        return sendTracked(orus::trackingMode(model_.targets(), mode));
    }
    return sendTracked(orus::trackingMode(model_.targets(), mode), -1,
                       [this](uint32_t id) { expectTracking(true, id, 0, 0); });
}

uint32_t Session::trackAt(float x, float y, float w, float h) {
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return 0;
    }
    const float px = std::clamp(x, 0.0f, 1.0f) * orus::kFrameWidth;
    const float py = std::clamp(y, 0.0f, 1.0f) * orus::kFrameHeight;
    const float pw = w > 0 ? std::min(w, 1.0f) * orus::kFrameWidth : orus::kDefaultTrackBoxPx;
    const float ph = h > 0 ? std::min(h, 1.0f) * orus::kFrameHeight : orus::kDefaultTrackBoxPx;
    setTrackingMode(IMAGO_TRACKING_ACTIVE);
    const float nx = px / orus::kFrameWidth, ny = py / orus::kFrameHeight;
    return sendTracked(orus::trackingPosition(model_.targets(), px, py, pw, ph), -1,
                       [this, nx, ny](uint32_t id) { expectTracking(false, id, nx, ny); });
}

uint32_t Session::pointAt(float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return 0;
    }
    setTrackingMode(IMAGO_TRACKING_EAGLE_EYE); // also drops any pending tracking expectation
    return sendTracked(orus::trackingPosition(
        model_.targets(), std::clamp(x, 0.0f, 1.0f) * orus::kFrameWidth,
        std::clamp(y, 0.0f, 1.0f) * orus::kFrameHeight, orus::kDefaultTrackBoxPx,
        orus::kDefaultTrackBoxPx));
}

uint32_t Session::irFfc() { return sendTracked(orus::irFfcTrigger(model_.targets())); }

uint32_t Session::setIrFfcMode(bool automatic) {
    return sendTracked(orus::irFfcMode(model_.targets(), automatic));
}

uint32_t Session::setIsotherm(bool enabled, float lowC, float highC) {
    namespace p = orus::param;
    if (!enabled) {
        return setCameraParam(p::kIsothermMode, 0);
    }
    if (!std::isfinite(lowC) || !std::isfinite(highC)) {
        return 0;
    }
    // Whole degrees: the camera takes the thresholds as unsigned integers, in Celsius from 0.
    const auto low = static_cast<uint32_t>(std::lround(std::clamp(lowC, 0.0f, IMAGO_IR_GAIN_LOW_MAX_C)));
    const auto high = static_cast<uint32_t>(std::lround(std::clamp(highC, 0.0f, IMAGO_IR_GAIN_LOW_MAX_C)));
    if (low >= high || !running()) {
        return 0;
    }
    // Each setting is acknowledged and resent on its own, so none is written twice with
    // different values: a late resend can never undo a later one. Region 1 is never switched
    // off on the way, and the isotherm is turned on last, over a finished band.
    setCameraParam(p::kIsothermUnit, orus::kIsothermUnitCelsius);
    if (static_cast<float>(high) > IMAGO_IR_GAIN_HIGH_MAX_C) {
        setCameraParam(p::kIrGain, IMAGO_IR_GAIN_LOW);
    }
    for (int region = 0; region < orus::kIsothermRegions; ++region) {
        if (region != 1) {
            setCameraParam(orus::isothermParam(region, "CLR_M"), orus::kIsothermColourOff);
        }
    }
    setCameraParam(orus::isothermParam(0, "TMP"), low);
    setCameraParam(orus::isothermParam(1, "TMP"), high);
    setCameraParam(orus::isothermParam(1, "CLR_MN"), orus::kIsothermColourBlack);
    setCameraParam(orus::isothermParam(1, "CLR_MX"), orus::kIsothermColourRed);
    setCameraParam(orus::isothermParam(1, "CLR_M"), orus::kIsothermColourLinear);
    return setCameraParam(p::kIsothermMode, 1);
}

uint32_t Session::streamSetting(uint32_t cameraId, uint32_t setting, uint32_t value) {
    return sendTracked(orus::streamSetting(model_.targets(), cameraId, setting, value));
}

// ----------------------------------------------------------------------------------------------
// Web settings

namespace {

// The page answers in well under a second through Herelink; a restart may never answer.
constexpr int kWebTimeoutMs = 5000;
constexpr int kWebConnectTimeoutMs = 1500;

bool webOk(int status) { return status >= 200 && status < 400; }

} // namespace

uint32_t Session::queueWeb(WebJob job) {
    if (!running() || webHost_.empty()) {
        return 0;
    }
    job.id = commands_.reserveId();
    {
        std::lock_guard lock(webMutex_);
        webJobs_.push_back(job);
    }
    webWake_.notify_one();
    return job.id;
}

uint32_t Session::webReadIrSettings() { return queueWeb({WebJob::Kind::Read}); }

uint32_t Session::webSetIsotherm(bool enabled, int thresholdC, int measurementRange) {
    if (thresholdC < 0 || thresholdC > 550 || measurementRange < 0 || measurementRange > 1) {
        return 0;
    }
    WebJob job{WebJob::Kind::Isotherm};
    job.enabled = enabled;
    job.thresholdC = thresholdC;
    job.measurementRange = measurementRange;
    return queueWeb(job);
}

uint32_t Session::webRestart(bool fullReboot) {
    WebJob job{WebJob::Kind::Restart};
    job.fullReboot = fullReboot;
    return queueWeb(job);
}

imago_web_ir_settings Session::webIrSettings() {
    std::lock_guard lock(webMutex_);
    return webSettings_;
}

void Session::webLoop() {
    configureCurrentThread("imago-web", ThreadRole::Background);
    std::optional<net::Endpoint> server;
    while (true) {
        WebJob job;
        {
            std::unique_lock lock(webMutex_);
            webWake_.wait(lock, [&] { return webStopping_.load() || !webJobs_.empty(); });
            if (webStopping_.load()) {
                return;
            }
            job = webJobs_.front();
            webJobs_.pop_front();
        }
        if (!server) {
            server = net::Endpoint::resolve(webHost_, webPort_); // blocking: only here
        }
        if (!server) {
            emit(IMAGO_EVENT_WEB_RESULT, job.id, 0, IMAGO_RESULT_FAILED,
                 ("unknown web host " + webHost_).c_str());
            continue;
        }
        runWebJob(job, *server);
    }
}

bool Session::readIrPage(const net::Endpoint& server, int& status, std::string& error) {
    const auto page = web::httpRequest(server, "GET", web::kIrCameraPath, "", kWebTimeoutMs,
                                       kWebConnectTimeoutMs, &webStopping_);
    status = page.status;
    if (page.status != 200) {
        error = page.status == 0 ? page.error : "the page answered " + std::to_string(page.status);
        return false;
    }
    const auto values = web::formValues(page.body);
    const auto status_ = values.find("IsoTherms_Status");
    const auto threshold = values.find("Threshold");
    if (status_ == values.end() || threshold == values.end()) {
        error = "the page has no isotherm settings";
        return false;
    }
    imago_web_ir_settings s{};
    s.valid = true;
    s.isotherm_enabled = status_->second == "1";
    s.isotherm_threshold_c = std::atoi(threshold->second.c_str());
    const auto range = values.find("Measurement_Range");
    s.measurement_range = range != values.end() ? std::atoi(range->second.c_str()) : 0;
    std::lock_guard lock(webMutex_);
    webSettings_ = s;
    return true;
}

void Session::runWebJob(const WebJob& job, const net::Endpoint& server) {
    int status = 0;
    std::string error;
    bool ok = false;
    switch (job.kind) {
    case WebJob::Kind::Read:
        ok = readIrPage(server, status, error);
        break;
    case WebJob::Kind::Isotherm: {
        const auto saved = web::httpRequest(
            server, "POST", web::kIrCameraPath,
            web::isothermForm(job.enabled, job.thresholdC, job.measurementRange), kWebTimeoutMs,
            kWebConnectTimeoutMs, &webStopping_);
        status = saved.status;
        if (!webOk(saved.status)) {
            error = saved.status == 0 ? saved.error : "the page answered " + std::to_string(saved.status);
            break;
        }
        int readStatus = 0;
        ok = readIrPage(server, readStatus, error); // confirms what the page now holds
        break;
    }
    case WebJob::Kind::Restart: {
        const auto answer = web::httpRequest(server, "POST", web::kSystemsPath,
                                             web::restartForm(job.fullReboot), kWebTimeoutMs,
                                             kWebConnectTimeoutMs, &webStopping_);
        status = answer.status;
        ok = webOk(answer.status);
        if (!ok) {
            error = answer.status == 0 ? answer.error : "the page answered " + std::to_string(answer.status);
        }
        break;
    }
    }
    if (webStopping_.load()) {
        return;
    }
    const int32_t result = ok ? IMAGO_RESULT_ACCEPTED
                              : (status == 0 ? IMAGO_RESULT_TIMEOUT : IMAGO_RESULT_FAILED);
    emit(IMAGO_EVENT_WEB_RESULT, job.id, static_cast<uint32_t>(status), result,
         ok ? "" : error.c_str());
}

// ----------------------------------------------------------------------------------------------
// Media files

uint32_t Session::mediaRefresh() {
    if (!running() || webHost_.empty()) {
        return 0;
    }
    const uint32_t id = commands_.reserveId();
    media_.refresh(id);
    return id;
}

uint32_t Session::mediaDownload(const std::string& name, const std::string& path) {
    if (!running() || webHost_.empty() || name.empty() || path.empty()) {
        return 0;
    }
    const uint32_t id = commands_.reserveId();
    media_.download(id, name, path);
    return id;
}

uint32_t Session::mediaDelete(const std::string& name) {
    if (!running() || webHost_.empty() || name.empty()) {
        return 0;
    }
    const uint32_t id = commands_.reserveId();
    media_.remove(id, name);
    return id;
}

uint32_t Session::mediaSetStorage(uint32_t index) {
    if (!running() || webHost_.empty()) {
        return 0;
    }
    const uint32_t id = commands_.reserveId();
    return media_.setStorage(id, index) ? id : 0;
}

} // namespace imago
