#pragma once

#include <imago/imago_core.h>

#include <QImage>
#include <QString>
#include <QVideoFrame>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

class QVideoSink;
struct AVBufferRef;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;

/// Decoding of the payload's video with FFmpeg's libavcodec, shown through a Qt video sink
/// (VideoOutput in QML).
///
/// The core hands each compressed frame over on its video thread; it is copied into a queue and
/// decoded on this class's own thread, so a slow picture never holds up packet reception. The
/// GPU decodes when it can (D3D11VA or DXVA2 on Windows, VA-API, CUDA or VDPAU on Linux), with
/// the software decoder as the fallback, in low-delay mode: one picture out for each frame in.
/// Every decoded picture goes to the sink at once; when several reach it between two screen
/// refreshes, only the newest is shown.
///
/// With playout smoothing the core asks for some frames to be shown a little later
/// (display_delay_ns); those wait on a presenter thread until their time and are never skipped.
class VideoDecoder {
  public:
    struct Counters {
        uint32_t decodeFailures = 0; // frames the decoder rejected or could not take
        uint32_t skipped = 0;        // pictures replaced before they reached the screen
        uint32_t rendered = 0;
    };

    VideoDecoder();
    ~VideoDecoder();

    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    /// Where pictures go. Main thread; the sink must outlive the decoder or be replaced first.
    void setSink(QVideoSink* sink);

    /// The session that receives the timings; set to nullptr before that session is destroyed.
    void setSession(imago_session* session);

    /// Core video thread.
    void onFrame(const imago_video_frame& frame);

    /// Drops the decoder, every queued frame and the counters, and blanks the picture. Call
    /// while no session delivers frames.
    void reset();

    /// Render thread, after a frame was swapped to the screen: completes the timing of the
    /// newest picture handed to the sink since the previous swap.
    void framePresented();

    Counters counters() const;
    /// The picture on screen now, converted to RGB (a still, or a check of the decoder).
    QImage snapshot();
    /// "D3D11VA", "VAAPI", "Software"... or empty before the first picture.
    QString decoderName() const;

    /// Picture size in pixels, from the decode thread whenever it changes.
    std::function<void(int width, int height)> onVideoSize;

  private:
    struct Packet {
        std::vector<uint8_t> data; // Annex B, parameter sets in front of keyframes
        imago_codec codec = IMAGO_CODEC_UNKNOWN;
        bool keyframe = false;
        bool parameterSetsChanged = false;
        int64_t receiveNs = 0;
        int64_t displayAtNs = 0; // 0 = at once
        uint32_t frameNumber = 0;
        uint64_t generation = 0;
    };

    struct Picture {
        QVideoFrame frame;
        int64_t receiveNs = 0;
        int64_t decodedNs = 0;
        int64_t displayAtNs = 0;
        uint64_t generation = 0;
    };

    void decodeLoop();
    void decode(Packet& packet);
    bool open(imago_codec codec);
    void close();
    void receivePictures();
    void picture(AVFrame* frame);
    void deliver(Picture picture);
    void presentLoop();
    void show(const Picture& picture);
    void failed();
    friend struct VideoDecoderAccess;

    // Core thread -> decode thread
    std::mutex queueMu_;
    std::condition_variable queueCv_;
    std::deque<Packet> queue_;
    bool resetRequested_ = false;
    bool stopping_ = false;
    std::thread decodeThread_;

    // Decode thread only
    AVCodecContext* context_ = nullptr;
    AVBufferRef* hwDevice_ = nullptr;
    int hwDeviceType_ = 0; // AVHWDeviceType of hwDevice_
    int hwPixelFormat_ = -1;
    bool hardwareFailed_ = false; // fell back to software for this stream
    imago_codec codec_ = IMAGO_CODEC_UNKNOWN;
    AVPacket* packet_ = nullptr;
    AVFrame* frame_ = nullptr;
    bool waitingForKeyframe_ = true;
    int64_t waitingSinceNs_ = 0;
    uint32_t consecutiveErrors_ = 0;
    uint64_t currentGeneration_ = 0;
    int width_ = 0;
    int height_ = 0;
    struct Timing {
        int64_t pts = -1;
        int64_t receiveNs = 0;
        int64_t displayAtNs = 0;
    };
    std::array<Timing, 64> timings_{};

    // Decode thread -> presenter thread (smoothing only)
    std::mutex presentMu_;
    std::condition_variable presentCv_;
    std::deque<Picture> scheduled_;
    bool presenterStopping_ = false;
    std::thread presentThread_;

    // Presenting -> render thread
    std::mutex sinkMu_;
    QVideoSink* sink_ = nullptr;
    /// Bumped by reset(): pictures of an older stream are dropped.
    std::atomic<uint64_t> generation_{0};
    uint32_t handedSinceSwap_ = 0;
    int64_t pendingReceiveNs_ = 0;
    int64_t pendingDecodedNs_ = 0;

    std::mutex sessionMu_;
    imago_session* session_ = nullptr;

    mutable std::mutex nameMu_;
    QString decoderName_;

    std::atomic<uint32_t> decodeFailures_{0};
    std::atomic<uint32_t> skipped_{0};
    std::atomic<uint32_t> rendered_{0};
};
