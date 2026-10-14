#include "session/session.h"

#include "platform/clock.h"
#include "platform/thread.h"
#include "rtsp/rtsp_url.h"
#include "util/text.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace imago {

namespace {

constexpr int64_t kHeartbeatPeriodNs = kNsPerSec;
constexpr int64_t kStreamInfoPeriodNs = 2 * kNsPerSec;
constexpr int64_t kStatusPeriodNs = 2 * kNsPerSec;
// Lost SET_MESSAGE_INTERVAL commands are not acknowledged, so the whole set is repeated.
constexpr int64_t kIntervalsPeriodNs = 15 * kNsPerSec;
// The zoom mode decides the zoom range; re-read now and then in case another app changes it.
constexpr int64_t kZoomModeRetryNs = 2 * kNsPerSec;
constexpr int64_t kZoomModePeriodNs = 10 * kNsPerSec;
// Zoom level from a dragged control: at most one command per period, the final level twice.
constexpr int64_t kZoomTargetPeriodNs = 150'000'000;
constexpr int64_t kZoomRepeatDelayNs = 400'000'000;
// Camera setting reads asked at once (a settings page) leave this far apart.
constexpr int64_t kParamReadSpacingNs = 40'000'000;
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
    {orus::DataIndex::IrTempMean, 1000},
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
    nextStreamInfoNs_ = now;
    nextStatusNs_ = now;
    nextIntervalsNs_ = now;
    running_.store(true, std::memory_order_release);
    networkThread_ = std::thread([this] { networkLoop(); });

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
    // Never leave the gimbal turning.
    if (link_.isOpen() && model_.linkUp()) {
        std::lock_guard lock(gimbalMutex_);
        for (int i = 0; i < kGimbalStopRepeats; ++i) {
            sendGimbalRate(0, 0, monotonicNs());
        }
    }
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
    for (const auto& r : commands_.clear()) {
        emitResult(r);
    }
}

// ----------------------------------------------------------------------------------------------
// Network thread

void Session::networkLoop() {
    configureCurrentThread("imago-mavlink", ThreadRole::Network);
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
        // Only the payload's acknowledgements addressed to us. 0 means unset or broadcast.
        if (!orus::isPayloadComponent(msg.compid) ||
            (ack.target_system != 0 && ack.target_system != config_.system_id) ||
            (ack.target_component != 0 && ack.target_component != config_.component_id)) {
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
            // fill (unverified); the answer comes as PARAM_EXT_VALUE.
            readCameraParam(std::string_view(ack.param_id, strnlen(ack.param_id, sizeof(ack.param_id))));
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
        emit(IMAGO_EVENT_COMMAND_RESULT, r.requestId, r.command, r.result, nullptr);
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

uint32_t Session::sendTracked(CommandManager::Message message, int retries) {
    if (!running()) {
        return 0;
    }
    const uint32_t id = commands_.add(message, monotonicNs(), retries);
    std::visit([this](const auto& msg) { link_.send(msg); }, message);
    return id;
}

void Session::sendGimbalRate(float pitchDps, float yawDps, int64_t nowNs) {
    if (gimbalProtocol_.load(std::memory_order_relaxed) == IMAGO_GIMBAL_PROTOCOL_V1) {
        // Streamed, not tracked: the next command supersedes a lost one.
        link_.send(orus::mountControl(model_.targets(), pitchDps, yawDps, true));
    } else {
        link_.send(orus::gimbalRate(model_.targets(), model_.gimbalFlags(), pitchDps, yawDps));
    }
    gimbalLastSentNs_ = nowNs;
}

void Session::setGimbalProtocol(imago_gimbal_protocol protocol) {
    if (protocol == IMAGO_GIMBAL_PROTOCOL_V1 || protocol == IMAGO_GIMBAL_PROTOCOL_V2) {
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
    return link_.send(orus::gimbalAngle(model_.targets(), model_.gimbalFlags(), pitchDeg, yawDeg))
               ? 1u
               : 0u;
}

uint32_t Session::setGimbalMode(imago_gimbal_mode mode) {
    if (mode < IMAGO_GIMBAL_MODE_OFF || mode > IMAGO_GIMBAL_MODE_RESET) {
        return 0;
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
    return sendTracked(orus::zoomContinuous(model_.targets(), direction));
}

float Session::zoomRangeMax() const {
    const float max = model_.zoomMax();
    return max > 0 ? max : orus::kZoomCombinedMax;
}

float Session::clampZoom(float level) const { return std::clamp(level, 1.0f, zoomRangeMax()); }

mavlink_command_long_t Session::zoomCommand(float level) const {
    return orus::zoomRange(model_.targets(), orus::zoomPercentForLevel(level, zoomRangeMax()));
}

uint32_t Session::zoomRange(float percent) {
    if (!std::isfinite(percent)) {
        return 0;
    }
    return sendTracked(orus::zoomRange(model_.targets(), percent));
}

uint32_t Session::zoomTo(float level) {
    if (!std::isfinite(level) || level < 1.0f) {
        return 0;
    }
    return sendTracked(zoomCommand(clampZoom(level)));
}

void Session::setZoomTarget(float level) {
    if (!running() || !std::isfinite(level)) {
        return;
    }
    level = clampZoom(level);
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
    return sendTracked(orus::setCameraParam(model_.targets(), id, value));
}

uint32_t Session::setTrackingMode(imago_tracking_mode mode) {
    if (mode < IMAGO_TRACKING_STOP || mode > IMAGO_TRACKING_EAGLE_EYE) {
        return 0;
    }
    return sendTracked(orus::trackingMode(model_.targets(), mode));
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
    return sendTracked(orus::trackingPosition(model_.targets(), px, py, pw, ph));
}

uint32_t Session::pointAt(float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return 0;
    }
    setTrackingMode(IMAGO_TRACKING_EAGLE_EYE);
    return sendTracked(orus::trackingPosition(
        model_.targets(), std::clamp(x, 0.0f, 1.0f) * orus::kFrameWidth,
        std::clamp(y, 0.0f, 1.0f) * orus::kFrameHeight, orus::kDefaultTrackBoxPx,
        orus::kDefaultTrackBoxPx));
}

uint32_t Session::streamSetting(uint32_t cameraId, uint32_t setting, uint32_t value) {
    return sendTracked(orus::streamSetting(model_.targets(), cameraId, setting, value));
}

} // namespace imago
