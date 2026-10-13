#pragma once

#include "imago/imago_core.h"
#include "rtp/depacketizer.h"
#include "rtsp/rtsp_client.h"
#include "util/stats.h"

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

    VideoReceiver(imago_rtsp_transport transport, imago_nal_format format, FrameCallback onFrame,
                  EventCallback onEvent);
    ~VideoReceiver();

    // Starts the thread, or switches the running session to a new URL.
    void start(const std::string& url);
    void stop();
    bool running() const { return thread_.joinable(); }
    std::string url() const;

    // Fills the video fields of the state.
    void snapshot(int64_t nowNs, imago_state& out) const;

  private:
    enum class SessionEnd { Stopped, Restart, NoPackets, Stalled, Failed };

    void run();
    SessionEnd runSession(const std::string& url, bool tcp, std::string& reason);
    void handleRtp(const uint8_t* data, size_t size, uint8_t payloadType);
    void deliverFrame(const AssembledFrame& frame);
    void publishStats(int64_t nowNs);
    bool shouldExit() const;

    const imago_rtsp_transport transport_;
    const imago_nal_format format_;
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
    } published_;
};

} // namespace imago
