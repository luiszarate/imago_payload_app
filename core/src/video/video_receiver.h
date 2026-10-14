#pragma once

#include "imago/imago_core.h"
#include "rtp/depacketizer.h"
#include "rtsp/rtsp_client.h"
#include "util/stats.h"
#include "video/lag_tracker.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace imago {

// Owns the video thread: RTSP session, RTP reception, frame assembly and delivery.
// Reconnects on its own with a short backoff until stopped.
class VideoReceiver {
  public:
    using FrameCallback = std::function<void(const imago_video_frame&)>;
    using EventCallback =
        std::function<void(imago_event_type type, const std::string& text, uint32_t code)>;

    // Restarts the session when every frame for a while arrives at least maxLagNs late.
    struct LagGuard {
        bool enabled = true;
        int64_t maxLagNs = 500'000'000;
    };

    VideoReceiver(imago_rtsp_transport transport, imago_nal_format format, FrameCallback onFrame,
                  EventCallback onEvent, LagGuard lagGuard);
    ~VideoReceiver();

    // Over UDP, sends a 1-byte datagram every ~33 ms to the video host's discard port so the
    // device's Wi-Fi stays awake (a dozing radio gets packets in bursts). Never to the RTSP
    // server's own ports. Set before start(); port 0 = 9.
    // Playout smoothing (imago_video_frame.display_delay_ns). Set before start(); 0 = none.
    void setSmoothing(int64_t delayNs) { smoothingNs_ = delayNs > 0 ? delayNs : 0; }

    void setWifiKeepAwake(bool enabled, uint16_t port) {
        wifiKeepAwake_ = enabled;
        wifiKeepAwakePort_ = port != 0 ? port : 9;
    }

    // Starts the thread, or switches the running session to a new URL.
    void start(const std::string& url);
    void stop();
    bool running() const { return thread_.joinable(); }
    std::string url() const;

    // Fills the video fields of the state.
    void snapshot(int64_t nowNs, imago_state& out) const;

  private:
    int64_t smoothingNs_ = 0;
    bool wifiKeepAwake_ = false;
    uint16_t wifiKeepAwakePort_ = 9;
    enum class SessionEnd { Stopped, Restart, NoPackets, Stalled, Lagging, Failed };

    void run();
    SessionEnd runSession(const std::string& url, bool tcp, std::string& reason);
    void handleRtp(const uint8_t* data, size_t size, uint8_t payloadType);
    void deliverFrame(const AssembledFrame& frame);
    // Returns how much later than the fastest recent frame this one arrived.
    int64_t trackLag(const AssembledFrame& frame);
    void publishStats(int64_t nowNs);
    bool shouldExit() const;

    const imago_rtsp_transport transport_;
    const imago_nal_format format_;
    const LagGuard lagGuard_;
    FrameCallback onFrame_;
    EventCallback onEvent_;

    std::thread thread_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> restartRequested_{false};
    mutable std::mutex mutex_; // guards url_ and the published stats
    std::condition_variable wake_;
    std::string url_;

    // Video thread only.
    std::unique_ptr<Depacketizer> depacketizer_;
    int64_t lastPacketNs_ = 0;
    RateMeter packetMeter_;
    RateMeter frameMeter_;
    WindowedPercentile transferMs_;
    WindowedPercentile assemblyMs_;
    WindowedPercentile lagMs_;
    WindowedPercentile gapMs_;   // between consecutive frames' arrival
    int64_t lastFrameNs_ = 0;
    LagTracker lag_;
    int64_t lagSinceNs_ = 0;    // first packet of the first frame in the current late run
    uint32_t lagResyncMs_ = 0;  // set when the guard decides to restart
    int64_t lastResyncNs_ = 0;
    int64_t lastStatsNs_ = 0;

    // Published for snapshot().
    struct Published {
        bool active = false;
        imago_codec codec = IMAGO_CODEC_UNKNOWN;
        int32_t transport = IMAGO_RTSP_AUTO;
        float fps = 0;
        float kbps = 0;
        DepacketizerStats stats;
        float transferP95 = 0;
        float assemblyP95 = 0;
        float lagMs = 0;
        float gapP95 = 0;
        float gapMax = 0;
    } published_;
    uint32_t resyncs_ = 0; // guarded by mutex_, kept across sessions
};

} // namespace imago
