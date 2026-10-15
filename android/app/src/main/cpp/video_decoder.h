#pragma once

#include <imago/imago_core.h>

#include <android/native_window.h>
#include <jni.h>
#include <media/NdkMediaCodec.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace imago::android {

/// Hardware decoding and display of the payload's video, entirely in native code.
///
/// The core hands each compressed frame over on its video thread; it goes straight into a
/// MediaCodec input buffer there, with the low-latency options each chip vendor offers. An
/// output thread renders every decoded picture to the SurfaceView's surface the moment it
/// appears, timestamped "now", and when several are waiting only the newest is shown. There is
/// no queue and no Java on the frame path.
///
/// With playout smoothing the core asks for some frames to be shown a little later
/// (display_delay_ns); those go to the compositor timestamped for that moment and are never
/// skipped.
class VideoDecoder {
public:
    struct Counters {
        uint32_t decodeFailures = 0; // frames the decoder rejected or could not take
        uint32_t skipped = 0;        // pictures dropped so the screen shows the newest
        uint32_t rendered = 0;
    };

    /// listener: global reference to the Kotlin VideoRenderer, told about picture size changes.
    explicit VideoDecoder(jobject listener);
    ~VideoDecoder();

    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    /// Takes over one reference of `window`; nullptr detaches the surface (the decoder stops).
    void setSurface(ANativeWindow* window);

    /// The session that receives decode timings; set to nullptr before that session is destroyed.
    void setSession(imago_session* session);

    /// Core video thread.
    void onFrame(const imago_video_frame& frame);

    /// Drops the decoder and the counters. Call while no session delivers frames.
    void reset();

    Counters counters() const;

private:
    bool configure(const imago_video_frame& frame); // mu_ held
    void teardown();                                // mu_ held
    void outputLoop(AMediaCodec* codec);
    void outputFormatChanged(AMediaCodec* codec);
    void report(int64_t receiveNs, int64_t decodedNs);
    void schedule(int64_t ptsUs, int64_t displayNs);
    /// When the picture with this presentation time should be shown; 0 = at once.
    int64_t displayTime(int64_t ptsUs);

    std::mutex mu_;
    ANativeWindow* window_ = nullptr;
    AMediaCodec* codec_ = nullptr;
    imago_codec codecType_ = IMAGO_CODEC_UNKNOWN;
    int64_t waitingSinceNs_ = 0;
    uint32_t consecutiveErrors_ = 0;
    std::thread output_;
    std::atomic<bool> outputRunning_{false};

    /// Display times of held-back frames, by presentation time (core thread -> output thread).
    struct Scheduled {
        int64_t ptsUs = -1;
        int64_t displayNs = 0;
    };
    std::mutex scheduleMu_;
    std::array<Scheduled, 64> schedule_{};
    size_t scheduleNext_ = 0;

    struct Output {
        size_t index;
        AMediaCodecBufferInfo info;
    };
    std::vector<Output> ready_; // output thread

    std::mutex sessionMu_;
    imago_session* session_ = nullptr;

    std::atomic<uint32_t> decodeFailures_{0};
    std::atomic<uint32_t> skipped_{0};
    std::atomic<uint32_t> rendered_{0};

    jobject listener_;
    jmethodID onVideoSize_ = nullptr;
    int width_ = 0; // output thread
    int height_ = 0;
};

} // namespace imago::android
