#include "video/video_receiver.h"

#include "platform/clock.h"
#include "platform/socket.h"
#include "platform/thread.h"
#include "rtp/rtp_packet.h"
#include "rtsp/sdp.h"

#include <algorithm>
#include <random>

namespace imago {

namespace {

constexpr int kConnectTimeoutMs = 2000;
constexpr int kRequestTimeoutMs = 3000;
constexpr int kPollMs = 20;
// Smoothing measures lateness against the fastest frame in this window.
constexpr int64_t kSmoothingBaselineNs = 2 * kNsPerSec;
// Frame arrival gaps are reported over this window.
constexpr int64_t kGapWindowNs = 2 * kNsPerSec;
constexpr int64_t kFirstPacketTimeoutNs = 3 * kNsPerSec;
constexpr int64_t kStallTimeoutNs = 2 * kNsPerSec;
constexpr int64_t kStatsWindowNs = 2 * kNsPerSec;
constexpr int64_t kStatsPeriodNs = 250 * kNsPerMs;
constexpr int kRtpReceiveBuffer = 4 * 1024 * 1024;
// Lag guard: how long every frame must be late before a restart, and the minimum time between
// restarts (when the link cannot carry the stream at all, restarting often would only add gaps).
constexpr int64_t kLagPersistNs = 2 * kNsPerSec;
constexpr int64_t kResyncCooldownNs = 10 * kNsPerSec;
constexpr int64_t kLagWindowNs = 1 * kNsPerSec;

// Binds an even RTP port and the next odd RTCP port, as RTSP expects.
bool openPortPair(net::UdpSocket& rtp, net::UdpSocket& rtcp) {
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> pick(25000, 29999);
    for (int attempt = 0; attempt < 32; ++attempt) {
        const auto port = static_cast<uint16_t>(pick(rng) * 2);
        if (rtp.open(port, kRtpReceiveBuffer, net::TrafficClass::Video, nullptr) &&
            rtcp.open(static_cast<uint16_t>(port + 1), 64 * 1024, net::TrafficClass::Video,
                      nullptr)) {
            return true;
        }
        rtp.close();
        rtcp.close();
    }
    return false;
}

} // namespace

VideoReceiver::VideoReceiver(imago_rtsp_transport transport, imago_nal_format format,
                             FrameCallback onFrame, EventCallback onEvent, LagGuard lagGuard)
    : transport_(transport), format_(format), lagGuard_(lagGuard), onFrame_(std::move(onFrame)),
      onEvent_(std::move(onEvent)) {}

VideoReceiver::~VideoReceiver() { stop(); }

void VideoReceiver::start(const std::string& url) {
    {
        std::lock_guard lock(mutex_);
        url_ = url;
    }
    if (thread_.joinable()) {
        restartRequested_ = true;
        wake_.notify_all();
        return;
    }
    stopRequested_ = false;
    restartRequested_ = false;
    thread_ = std::thread([this] { run(); });
}

void VideoReceiver::stop() {
    if (!thread_.joinable()) {
        return;
    }
    stopRequested_ = true;
    wake_.notify_all();
    thread_.join();
    std::lock_guard lock(mutex_);
    published_.active = false;
}

std::string VideoReceiver::url() const {
    std::lock_guard lock(mutex_);
    return url_;
}

bool VideoReceiver::shouldExit() const { return stopRequested_ || restartRequested_; }

void VideoReceiver::run() {
    configureCurrentThread("imago-video", ThreadRole::Video);
    bool udpFailed = transport_ == IMAGO_RTSP_TCP;
    int failures = 0;
    while (!stopRequested_) {
        restartRequested_ = false;
        const std::string url = this->url();
        const bool tcp = transport_ == IMAGO_RTSP_TCP || (transport_ == IMAGO_RTSP_AUTO && udpFailed);
        std::string reason;
        const SessionEnd end = runSession(url, tcp, reason);
        {
            std::lock_guard lock(mutex_);
            published_.active = false;
        }
        if (end == SessionEnd::Stopped || stopRequested_) {
            break;
        }
        if (end == SessionEnd::Restart || end == SessionEnd::Lagging) {
            failures = 0;
            continue;
        }
        if (end == SessionEnd::NoPackets && !tcp && transport_ == IMAGO_RTSP_AUTO) {
            // UDP is likely blocked between the server and us: switch to TCP right away.
            udpFailed = true;
            onEvent_(IMAGO_EVENT_VIDEO_ERROR, "no RTP over UDP, switching to TCP", 0);
            continue;
        }
        onEvent_(IMAGO_EVENT_VIDEO_ERROR, reason, 0);
        // Back off 250 ms, 500 ms, 1 s, then 2 s between attempts.
        const int delayMs = std::min(250 << std::min(failures, 3), 2000);
        ++failures;
        std::unique_lock lock(mutex_);
        wake_.wait_for(lock, std::chrono::milliseconds(delayMs),
                       [this] { return stopRequested_.load() || restartRequested_.load(); });
    }
}

VideoReceiver::SessionEnd VideoReceiver::runSession(const std::string& urlText, bool tcp,
                                                    std::string& reason) {
    const auto url = RtspUrl::parse(urlText);
    if (!url) {
        reason = "invalid RTSP URL: " + urlText;
        return SessionEnd::Failed;
    }
    RtspClient client;
    if (!client.connect(*url, kConnectTimeoutMs, &reason)) {
        return SessionEnd::Failed;
    }
    const std::string requestUrl = url->toString();

    // DESCRIBE: codec and parameter sets.
    auto describe = client.request("DESCRIBE", requestUrl, {{"Accept", "application/sdp"}},
                                   kRequestTimeoutMs, &reason);
    if (!describe) {
        return SessionEnd::Failed;
    }
    if (describe->status != 200) {
        reason = "DESCRIBE: " + std::to_string(describe->status) + " " + describe->reason;
        return SessionEnd::Failed;
    }
    const auto sdp = parseSdpVideo(describe->body);
    if (!sdp) {
        reason = "no H.264 or H.265 video in SDP";
        return SessionEnd::Failed;
    }
    std::string baseUrl = requestUrl;
    if (auto contentBase = describe->header("Content-Base")) {
        baseUrl = std::string(*contentBase);
    }
    const std::string controlUrl = resolveControlUrl(baseUrl, sdp->control);

    depacketizer_ = std::make_unique<Depacketizer>(sdp->codec, format_);
    depacketizer_->setParameterSets(sdp->parameterSets);
    const uint8_t payloadType = sdp->payloadType;

    // SETUP
    net::UdpSocket rtp;
    net::UdpSocket rtcp;
    std::string transportRequest;
    if (tcp) {
        transportRequest = "RTP/AVP/TCP;unicast;interleaved=0-1";
    } else {
        if (!openPortPair(rtp, rtcp)) {
            reason = "cannot bind RTP ports";
            return SessionEnd::Failed;
        }
        const uint16_t port = rtp.localPort();
        transportRequest = "RTP/AVP;unicast;client_port=" + std::to_string(port) + "-" +
                           std::to_string(port + 1);
    }
    auto setup = client.request("SETUP", controlUrl, {{"Transport", transportRequest}},
                                kRequestTimeoutMs, &reason);
    if (!setup) {
        return SessionEnd::Failed;
    }
    if (setup->status != 200) {
        reason = "SETUP: " + std::to_string(setup->status) + " " + setup->reason;
        return SessionEnd::Failed;
    }
    int sessionTimeoutSec = 60;
    if (auto session = setup->header("Session")) {
        const auto parsed = parseSessionHeader(*session);
        client.setSession(parsed.id);
        sessionTimeoutSec = parsed.timeoutSec;
    }
    RtspTransportHeader transport;
    if (auto header = setup->header("Transport")) {
        transport = parseTransportHeader(*header);
    }
    const bool interleaved = tcp || transport.tcp;
    const uint8_t rtpChannel = transport.interleavedRtp;
    if (interleaved) {
        rtp.close();
        rtcp.close();
    }

    auto onInterleaved = [&](uint8_t channel, const uint8_t* data, size_t size) {
        if (channel == rtpChannel) {
            handleRtp(data, size, payloadType);
        }
    };

    // PLAY
    auto play = client.request("PLAY", baseUrl, {{"Range", "npt=0.000-"}}, kRequestTimeoutMs,
                               &reason, onInterleaved);
    if (!play) {
        return SessionEnd::Failed;
    }
    if (play->status != 200) {
        reason = "PLAY: " + std::to_string(play->status) + " " + play->reason;
        return SessionEnd::Failed;
    }

    const int64_t playNs = monotonicNs();
    lastPacketNs_ = 0;
    lag_.reset();
    lagMs_ = WindowedPercentile{};
    gapMs_ = WindowedPercentile{};
    lastFrameNs_ = 0;
    lagSinceNs_ = 0;
    lagResyncMs_ = 0;
    {
        std::lock_guard lock(mutex_);
        published_ = Published{};
        published_.active = true;
        published_.codec = sdp->codec;
        published_.transport = interleaved ? IMAGO_RTSP_TCP : IMAGO_RTSP_UDP;
    }
    onEvent_(IMAGO_EVENT_VIDEO_STARTED, urlText,
             static_cast<uint32_t>(interleaved ? IMAGO_RTSP_TCP : IMAGO_RTSP_UDP));

    // Keepalive at half the session timeout, at most every 25 s.
    const int64_t keepaliveNs =
        std::clamp<int64_t>(static_cast<int64_t>(sessionTimeoutSec) * kNsPerSec / 2,
                            5 * kNsPerSec, 25 * kNsPerSec);
    int64_t nextKeepaliveNs = playNs + keepaliveNs;

    SessionEnd end = SessionEnd::Stopped;
    uint8_t datagram[65536];
    while (true) {
        if (shouldExit()) {
            end = stopRequested_ ? SessionEnd::Stopped : SessionEnd::Restart;
            break;
        }
        const net::SocketHandle fds[3] = {client.fd(), rtp.fd(), rtcp.fd()};
        bool readable[3] = {};
        if (net::waitReadable(fds, kPollMs, readable) < 0) {
            reason = "poll failed";
            end = SessionEnd::Failed;
            break;
        }
        if (readable[1]) {
            while (true) {
                const long size = rtp.receive(datagram, sizeof(datagram), nullptr);
                if (size <= 0) {
                    break;
                }
                handleRtp(datagram, static_cast<size_t>(size), payloadType);
            }
        }
        if (readable[2]) {
            while (rtcp.receive(datagram, sizeof(datagram), nullptr) > 0) {
                // Sender reports are not needed: timing comes from local arrival times.
            }
        }
        if (readable[0] && !client.pump(onInterleaved)) {
            reason = "RTSP connection closed";
            end = SessionEnd::Failed;
            break;
        }

        if (lagResyncMs_ != 0) {
            // Drop the queue wherever it is: a new session starts from the live picture.
            onEvent_(IMAGO_EVENT_VIDEO_RESYNC, "video " + std::to_string(lagResyncMs_) + " ms behind",
                     lagResyncMs_);
            lastResyncNs_ = monotonicNs();
            {
                std::lock_guard lock(mutex_);
                ++resyncs_;
            }
            end = SessionEnd::Lagging;
            break;
        }

        const int64_t nowNs = monotonicNs();
        if (lastPacketNs_ == 0 && nowNs - playNs > kFirstPacketTimeoutNs) {
            reason = "no video packets after PLAY";
            end = SessionEnd::NoPackets;
            break;
        }
        if (lastPacketNs_ != 0 && nowNs - lastPacketNs_ > kStallTimeoutNs) {
            reason = "video stalled";
            end = SessionEnd::Stalled;
            break;
        }
        if (nowNs >= nextKeepaliveNs) {
            client.sendAsync("GET_PARAMETER", baseUrl);
            nextKeepaliveNs = nowNs + keepaliveNs;
        }
        if (nowNs - lastStatsNs_ >= kStatsPeriodNs) {
            publishStats(nowNs);
        }
    }

    // Best effort: tell the server to stop sending. Do not wait for the answer.
    client.sendAsync("TEARDOWN", baseUrl);
    client.close();
    depacketizer_.reset();
    onEvent_(IMAGO_EVENT_VIDEO_STOPPED, urlText, 0);
    return end;
}

void VideoReceiver::handleRtp(const uint8_t* data, size_t size, uint8_t payloadType) {
    const auto packet = parseRtp(data, size);
    if (!packet || packet->payloadType != payloadType || !depacketizer_) {
        return;
    }
    const int64_t nowNs = monotonicNs();
    lastPacketNs_ = nowNs;
    packetMeter_.add(nowNs, size);
    depacketizer_->push(*packet, nowNs,
                        [this](const AssembledFrame& frame) { deliverFrame(frame); });
}

void VideoReceiver::deliverFrame(const AssembledFrame& frame) {
    const ParameterSets& params = depacketizer_->parameterSets();
    imago_video_frame out{};
    out.codec = depacketizer_->codec();
    out.data = frame.data;
    out.size = frame.size;
    out.vps = params.vps.empty() ? nullptr : params.vps.data();
    out.vps_size = params.vps.size();
    out.sps = params.sps.data();
    out.sps_size = params.sps.size();
    out.pps = params.pps.data();
    out.pps_size = params.pps.size();
    out.parameter_sets_changed = frame.parameterSetsChanged;
    out.keyframe = frame.keyframe;
    out.corrupt = frame.corrupt;
    out.rtp_timestamp = frame.rtpTimestamp;
    out.frame_number = frame.frameNumber;
    out.receive_time_ns = frame.lastPacketNs;

    // Measured before the platform callback: this is the core's share of the latency only.
    const int64_t nowNs = monotonicNs();
    frameMeter_.add(nowNs, frame.size);
    transferMs_.add(nowNs, static_cast<float>(frame.lastPacketNs - frame.firstPacketNs) / 1e6f);
    assemblyMs_.add(nowNs, static_cast<float>(nowNs - frame.lastPacketNs) / 1e6f);
    if (lastFrameNs_ != 0) {
        gapMs_.add(nowNs, static_cast<float>(frame.lastPacketNs - lastFrameNs_) / 1e6f);
    }
    lastFrameNs_ = frame.lastPacketNs;
    // Smoothing: a frame that came early in a burst waits for its turn, within the delay; one
    // later than the delay is shown at once, so the delay never grows.
    const int64_t lagNs = trackLag(frame);
    if (smoothingNs_ > 0) {
        // Late against the fastest frame of the last 2 s, not the tracker's slow-drifting
        // baseline: one freak early frame must not switch smoothing off for minutes.
        const auto fastestMs = lagMs_.percentile(frame.firstPacketNs, kSmoothingBaselineNs, 0.0f);
        const int64_t lateNs = std::max<int64_t>(0, lagNs - static_cast<int64_t>(fastestMs * 1e6f));
        // From the first packet, like the lag, so frames keep their order on screen.
        const int64_t transferNs = frame.lastPacketNs - frame.firstPacketNs;
        out.display_delay_ns = std::max<int64_t>(0, smoothingNs_ - lateNs - transferNs);
    }

    if (onFrame_) {
        onFrame_(out);
    }
}

int64_t VideoReceiver::trackLag(const AssembledFrame& frame) {
    const int64_t lagNs = lag_.update(frame.rtpTimestamp, frame.firstPacketNs);
    lagMs_.add(frame.firstPacketNs, static_cast<float>(lagNs) / 1e6f);
    if (lagNs < lagGuard_.maxLagNs) {
        lagSinceNs_ = 0;
        return lagNs;
    }
    if (lagSinceNs_ == 0) {
        lagSinceNs_ = frame.firstPacketNs;
    }
    // Late frames for a while, not just a burst after a hiccup: something upstream is queueing.
    const bool cooledDown = lastResyncNs_ == 0 || frame.firstPacketNs - lastResyncNs_ >= kResyncCooldownNs;
    if (lagGuard_.enabled && cooledDown && frame.firstPacketNs - lagSinceNs_ >= kLagPersistNs) {
        lagResyncMs_ = static_cast<uint32_t>(lagNs / kNsPerMs);
    }
    return lagNs;
}

void VideoReceiver::publishStats(int64_t nowNs) {
    lastStatsNs_ = nowNs;
    packetMeter_.update(nowNs);
    frameMeter_.update(nowNs);
    std::lock_guard lock(mutex_);
    published_.fps = frameMeter_.eventsPerSecond();
    published_.kbps = packetMeter_.kilobitsPerSecond();
    if (depacketizer_) {
        published_.stats = depacketizer_->stats();
    }
    published_.transferP95 = transferMs_.percentile(nowNs, kStatsWindowNs, 0.95f);
    published_.gapP95 = gapMs_.percentile(nowNs, kGapWindowNs, 0.95f);
    published_.gapMax = gapMs_.percentile(nowNs, kGapWindowNs, 1.0f);
    // A frozen stream has no new gaps to measure: report the one still open.
    if (lastFrameNs_ != 0) {
        const auto openMs = static_cast<float>(nowNs - lastFrameNs_) / 1e6f;
        published_.gapP95 = std::max(published_.gapP95, openMs);
        published_.gapMax = std::max(published_.gapMax, openMs);
    }
    published_.assemblyP95 = assemblyMs_.percentile(nowNs, kStatsWindowNs, 0.95f);
    // The lowest lag over the last second: bursts after a hiccup do not count, a queue does.
    published_.lagMs = lagMs_.percentile(nowNs, kLagWindowNs, 0.0f);
}

void VideoReceiver::snapshot(int64_t nowNs, imago_state& out) const {
    (void)nowNs;
    std::lock_guard lock(mutex_);
    out.video_active = published_.active;
    out.video_codec = published_.codec;
    out.video_transport = published_.transport;
    out.video_fps = published_.active ? published_.fps : 0;
    out.video_bitrate_kbps = published_.active ? published_.kbps : 0;
    out.video_packets_received = published_.stats.packetsReceived;
    out.video_packets_lost = published_.stats.packetsLost;
    out.video_packets_late = published_.stats.packetsLate;
    out.video_frames_delivered = published_.stats.framesDelivered;
    out.video_frames_corrupt = published_.stats.framesCorrupt;
    out.video_jitter_ms = published_.stats.jitterMs;
    out.video_frame_transfer_ms = published_.transferP95;
    out.video_frame_gap_p95_ms = published_.active ? published_.gapP95 : 0;
    out.video_frame_gap_max_ms = published_.active ? published_.gapMax : 0;
    out.latency_assembly_ms = published_.assemblyP95;
    out.video_lag_ms = published_.active ? published_.lagMs : 0;
    out.video_resyncs = resyncs_;
}

} // namespace imago
