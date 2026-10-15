#include "video_decoder.h"

#include "jni_util.h"

#include <android/log.h>
#include <media/NdkMediaFormat.h>

#include <cstring>
#include <vector>

namespace imago::android {

namespace {

constexpr const char* kTag = "imago-video";

/// A decoder that has not seen a key frame shows garbage or fails, so it starts on one. Some
/// encoders refresh without key frames, so after this long it starts anyway.
constexpr int64_t kKeyframeWaitNs = 2'000'000'000;
/// How long the video thread waits for a free input buffer before dropping the frame.
constexpr int64_t kInputTimeoutUs = 50'000;
constexpr int64_t kOutputTimeoutUs = 20'000;
/// Errors in a row after which the decoder is rebuilt at the next key frame.
constexpr uint32_t kMaxConsecutiveErrors = 10;

void appendNal(std::vector<uint8_t>& out, const uint8_t* nal, size_t size) {
    static constexpr uint8_t kStartCode[] = {0, 0, 0, 1};
    out.insert(out.end(), kStartCode, kStartCode + 4);
    out.insert(out.end(), nal, nal + size);
}

/// Decoder options for real-time video. Each is ignored by decoders that do not know it.
void setLowLatencyOptions(AMediaFormat* format) {
    AMediaFormat_setInt32(format, "low-latency", 1);  // Android 11+
    AMediaFormat_setInt32(format, "priority", 0);     // real time
    // Qualcomm: low-latency mode, and pictures in decode order (the payload sends no B-frames).
    AMediaFormat_setInt32(format, "vendor.qti-ext-dec-low-latency.enable", 1);
    AMediaFormat_setInt32(format, "vendor.qti-ext-dec-picture-order.enable", 1);
    // MediaTek
    AMediaFormat_setInt32(format, "vendor.rtc-ext-dec-low-latency.enable", 1);
    // HiSilicon (Huawei)
    AMediaFormat_setInt32(format, "vendor.hisi-ext-low-latency-video-dec.video-scene-for-low-latency-req", 1);
    AMediaFormat_setInt32(format, "vendor.hisi-ext-low-latency-video-dec.video-scene-for-low-latency-rdy", -1);
}

} // namespace

VideoDecoder::VideoDecoder(jobject listener) : listener_(listener) {
    JNIEnv* env = currentEnv();
    jclass cls = env->GetObjectClass(listener_);
    onVideoSize_ = env->GetMethodID(cls, "onVideoSize", "(II)V");
    env->DeleteLocalRef(cls);
}

VideoDecoder::~VideoDecoder() {
    {
        std::lock_guard lock(mu_);
        teardown();
        if (window_ != nullptr) {
            ANativeWindow_release(window_);
            window_ = nullptr;
        }
    }
    if (JNIEnv* env = currentEnv()) {
        env->DeleteGlobalRef(listener_);
    }
}

void VideoDecoder::setSurface(ANativeWindow* window) {
    std::lock_guard lock(mu_);
    if (window == window_) {
        if (window != nullptr) {
            ANativeWindow_release(window); // already holding one reference
        }
        return;
    }
    if (window != nullptr && codec_ != nullptr &&
        AMediaCodec_setOutputSurface(codec_, window) == AMEDIA_OK) {
        // Same decoder, new surface: no need to wait for a key frame.
    } else {
        teardown();
    }
    if (window_ != nullptr) {
        ANativeWindow_release(window_);
    }
    window_ = window;
    waitingSinceNs_ = 0;
}

void VideoDecoder::setSession(imago_session* session) {
    std::lock_guard lock(sessionMu_);
    session_ = session;
}

void VideoDecoder::reset() {
    std::lock_guard lock(mu_);
    teardown();
    waitingSinceNs_ = 0;
    decodeFailures_ = 0;
    skipped_ = 0;
    rendered_ = 0;
}

VideoDecoder::Counters VideoDecoder::counters() const {
    return Counters{decodeFailures_.load(), skipped_.load(), rendered_.load()};
}

// MARK: Input (core video thread)

void VideoDecoder::onFrame(const imago_video_frame& frame) {
    std::lock_guard lock(mu_);
    if (window_ == nullptr || frame.data == nullptr || frame.size == 0) {
        return; // nothing to show it on
    }
    if (codec_ != nullptr && (frame.parameter_sets_changed || frame.codec != codecType_)) {
        teardown();
    }
    if (codec_ == nullptr) {
        if (!frame.keyframe) {
            if (waitingSinceNs_ == 0) {
                waitingSinceNs_ = frame.receive_time_ns;
            }
            if (frame.receive_time_ns - waitingSinceNs_ < kKeyframeWaitNs) {
                return;
            }
        }
        if (!configure(frame)) {
            decodeFailures_.fetch_add(1);
            return;
        }
        waitingSinceNs_ = 0;
    }

    const ssize_t index = AMediaCodec_dequeueInputBuffer(codec_, kInputTimeoutUs);
    if (index < 0) {
        // The decoder is still busy with earlier frames: this one is lost (artifacts until
        // the next key frame), which is better than falling behind live.
        skipped_.fetch_add(1);
        return;
    }
    size_t capacity = 0;
    uint8_t* buffer = AMediaCodec_getInputBuffer(codec_, static_cast<size_t>(index), &capacity);
    const bool fits = buffer != nullptr && frame.size <= capacity;
    if (fits) {
        std::memcpy(buffer, frame.data, frame.size);
    }
    // The receive time travels through the decoder as the presentation time, so the output
    // thread can report the whole app-side latency.
    const auto pts = static_cast<uint64_t>(frame.receive_time_ns / 1000);
    if (frame.display_delay_ns > 0) {
        schedule(static_cast<int64_t>(pts), frame.receive_time_ns + frame.display_delay_ns);
    }
    const media_status_t status = AMediaCodec_queueInputBuffer(
        codec_, static_cast<size_t>(index), 0, fits ? frame.size : 0, pts, 0);
    if (!fits || status != AMEDIA_OK) {
        decodeFailures_.fetch_add(1);
        if (++consecutiveErrors_ >= kMaxConsecutiveErrors) {
            __android_log_print(ANDROID_LOG_WARN, kTag, "decoder failing, rebuilding (%d)", status);
            teardown();
        }
        return;
    }
    consecutiveErrors_ = 0;
}

bool VideoDecoder::configure(const imago_video_frame& frame) {
    const bool hevc = frame.codec == IMAGO_CODEC_H265;
    if (frame.sps == nullptr || frame.pps == nullptr || frame.sps_size == 0 || frame.pps_size == 0 ||
        (hevc && (frame.vps == nullptr || frame.vps_size == 0))) {
        return false;
    }
    const char* mime = hevc ? "video/hevc" : "video/avc";

    // Parameter sets in Annex B, as MediaCodec wants them: H.264 SPS and PPS apart, H.265 all
    // three together.
    std::vector<uint8_t> csd0;
    std::vector<uint8_t> csd1;
    if (hevc) {
        appendNal(csd0, frame.vps, frame.vps_size);
        appendNal(csd0, frame.sps, frame.sps_size);
        appendNal(csd0, frame.pps, frame.pps_size);
    } else {
        appendNal(csd0, frame.sps, frame.sps_size);
        appendNal(csd1, frame.pps, frame.pps_size);
    }

    // Some older decoders refuse options they do not know: then try once without them.
    for (const bool lowLatency : {true, false}) {
        AMediaCodec* codec = AMediaCodec_createDecoderByType(mime);
        if (codec == nullptr) {
            __android_log_print(ANDROID_LOG_ERROR, kTag, "no decoder for %s", mime);
            return false;
        }
        AMediaFormat* format = AMediaFormat_new();
        AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, mime);
        // The real size comes from the SPS; this only sizes the first buffers.
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, 1920);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, 1080);
        AMediaFormat_setBuffer(format, "csd-0", csd0.data(), csd0.size());
        if (!csd1.empty()) {
            AMediaFormat_setBuffer(format, "csd-1", csd1.data(), csd1.size());
        }
        if (lowLatency) {
            setLowLatencyOptions(format);
        }
        media_status_t status = AMediaCodec_configure(codec, format, window_, nullptr, 0);
        AMediaFormat_delete(format);
        if (status == AMEDIA_OK) {
            status = AMediaCodec_start(codec);
        }
        if (status != AMEDIA_OK) {
            __android_log_print(ANDROID_LOG_WARN, kTag, "configure %s (low latency %d) failed: %d",
                                mime, lowLatency, status);
            AMediaCodec_delete(codec);
            continue;
        }
        codec_ = codec;
        codecType_ = frame.codec;
        consecutiveErrors_ = 0;
        outputRunning_ = true;
        output_ = std::thread(&VideoDecoder::outputLoop, this, codec);
        return true;
    }
    return false;
}

void VideoDecoder::teardown() {
    if (codec_ == nullptr) {
        return;
    }
    outputRunning_ = false;
    if (output_.joinable()) {
        output_.join();
    }
    AMediaCodec_stop(codec_);
    AMediaCodec_delete(codec_);
    codec_ = nullptr;
    codecType_ = IMAGO_CODEC_UNKNOWN;
}

// MARK: Output (decoder output thread)

void VideoDecoder::outputLoop(AMediaCodec* codec) {
    ready_.reserve(16);
    while (outputRunning_.load(std::memory_order_relaxed)) {
        // Wait for a picture, then take every other one that is ready too.
        ready_.clear();
        int64_t timeoutUs = kOutputTimeoutUs;
        for (;;) {
            AMediaCodecBufferInfo info{};
            const ssize_t index = AMediaCodec_dequeueOutputBuffer(codec, &info, timeoutUs);
            if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                outputFormatChanged(codec);
                continue;
            }
            if (index < 0) {
                break; // none (more) ready, or output buffers changed
            }
            if ((info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) != 0 || info.size == 0) {
                AMediaCodec_releaseOutputBuffer(codec, static_cast<size_t>(index), false);
                continue;
            }
            ready_.push_back(Output{static_cast<size_t>(index), info});
            timeoutUs = 0;
        }
        if (ready_.empty()) {
            continue;
        }

        // Of the pictures due now (a burst after a hiccup), only the newest is shown; each is
        // timestamped now, so the compositor shows it at the next refresh and never holds it
        // back. Pictures the core holds back for smoothing go out timestamped for their moment.
        const int64_t now = imago_now_ns();
        size_t newestDue = ready_.size();
        for (size_t k = 0; k < ready_.size(); ++k) {
            if (displayTime(ready_[k].info.presentationTimeUs) <= now) {
                newestDue = k;
            }
        }
        for (size_t k = 0; k < ready_.size(); ++k) {
            const Output& out = ready_[k];
            const int64_t at = displayTime(out.info.presentationTimeUs);
            if (at > now) {
                AMediaCodec_releaseOutputBufferAtTime(codec, out.index, at);
            } else if (k == newestDue) {
                AMediaCodec_releaseOutputBufferAtTime(codec, out.index, now);
            } else {
                AMediaCodec_releaseOutputBuffer(codec, out.index, false);
                skipped_.fetch_add(1);
                continue;
            }
            rendered_.fetch_add(1);
            report(out.info.presentationTimeUs * 1000, now);
        }
    }
}

void VideoDecoder::schedule(int64_t ptsUs, int64_t displayNs) {
    std::lock_guard lock(scheduleMu_);
    schedule_[scheduleNext_] = Scheduled{ptsUs, displayNs};
    scheduleNext_ = (scheduleNext_ + 1) % schedule_.size();
}

int64_t VideoDecoder::displayTime(int64_t ptsUs) {
    std::lock_guard lock(scheduleMu_);
    for (const Scheduled& s : schedule_) {
        if (s.ptsUs == ptsUs) {
            return s.displayNs;
        }
    }
    return 0;
}

void VideoDecoder::outputFormatChanged(AMediaCodec* codec) {
    AMediaFormat* format = AMediaCodec_getOutputFormat(codec);
    if (format == nullptr) {
        return;
    }
    int32_t width = 0;
    int32_t height = 0;
    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_WIDTH, &width);
    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_HEIGHT, &height);
    // The visible picture, when the decoder pads it (1080 lines coded as 1088).
    int32_t left = 0;
    int32_t right = 0;
    int32_t top = 0;
    int32_t bottom = 0;
    if (AMediaFormat_getInt32(format, "crop-left", &left) &&
        AMediaFormat_getInt32(format, "crop-right", &right) &&
        AMediaFormat_getInt32(format, "crop-top", &top) &&
        AMediaFormat_getInt32(format, "crop-bottom", &bottom)) {
        width = right - left + 1;
        height = bottom - top + 1;
    }
    AMediaFormat_delete(format);
    if (width <= 0 || height <= 0 || (width == width_ && height == height_)) {
        return;
    }
    width_ = width;
    height_ = height;
    if (JNIEnv* env = currentEnv(); env != nullptr && onVideoSize_ != nullptr) {
        env->CallVoidMethod(listener_, onVideoSize_, width, height);
        clearException(env);
    }
}

void VideoDecoder::report(int64_t receiveNs, int64_t decodedNs) {
    std::lock_guard lock(sessionMu_);
    if (session_ != nullptr) {
        // Presented is 0 (not measured): the compositor shows it at the next refresh.
        imago_report_frame_presented(session_, receiveNs, decodedNs, 0);
    }
}

} // namespace imago::android
