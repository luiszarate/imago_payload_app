#include "video/VideoDecoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/log.h>
#include <libavutil/pixdesc.h>
}

#include <QAbstractVideoBuffer>
#include <QVideoFrameFormat>
#include <QVideoSink>
#include <QtGlobal>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace {

/// Frames waiting to be decoded before the decoder is considered behind: it then skips to the
/// next keyframe rather than show ever older pictures.
constexpr size_t kMaxQueued = 30;
/// A stream that sends no keyframe this long is decoded anyway (with artifacts until one comes).
constexpr int64_t kKeyframeWaitNs = 2'000'000'000;
/// Errors in a row before the decoder is rebuilt (in software, if the GPU was decoding).
constexpr uint32_t kMaxConsecutiveErrors = 10;

constexpr uint8_t kStartCode[4] = {0, 0, 0, 1};

void appendNal(std::vector<uint8_t>& out, const uint8_t* nal, size_t size) {
    if (nal == nullptr || size == 0) {
        return;
    }
    out.insert(out.end(), kStartCode, kStartCode + 4);
    out.insert(out.end(), nal, nal + size);
}

/// GPU decoders to try, best first.
constexpr AVHWDeviceType kHardware[] = {
#if defined(_WIN32)
    AV_HWDEVICE_TYPE_D3D11VA,
    AV_HWDEVICE_TYPE_DXVA2,
    AV_HWDEVICE_TYPE_CUDA,
#elif defined(__APPLE__)
    AV_HWDEVICE_TYPE_VIDEOTOOLBOX,
#else
    AV_HWDEVICE_TYPE_VAAPI,
    AV_HWDEVICE_TYPE_CUDA,
    AV_HWDEVICE_TYPE_VDPAU,
#endif
};

bool hardwareAllowed() { return qEnvironmentVariable("IMAGO_HWDEC") != QLatin1String("0"); }

QVideoFrameFormat::PixelFormat qtFormat(int format) {
    switch (format) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
        return QVideoFrameFormat::Format_YUV420P;
    case AV_PIX_FMT_NV12:
        return QVideoFrameFormat::Format_NV12;
    case AV_PIX_FMT_P010LE:
        return QVideoFrameFormat::Format_P010;
    case AV_PIX_FMT_YUV420P10LE:
        return QVideoFrameFormat::Format_YUV420P10;
    default:
        return QVideoFrameFormat::Format_Invalid;
    }
}

/// A decoded picture handed to Qt without copying: the planes stay in the AVFrame, which is
/// freed when Qt is done with the frame.
class FrameBuffer : public QAbstractVideoBuffer {
  public:
    FrameBuffer(AVFrame* frame, const QVideoFrameFormat& format) : frame_(frame), format_(format) {}
    ~FrameBuffer() override { av_frame_free(&frame_); }

    MapData map(QVideoFrame::MapMode) override {
        MapData data;
        const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame_->format));
        const int planes = format_.planeCount();
        data.planeCount = planes;
        for (int i = 0; i < planes && i < 4; ++i) {
            const int height = i == 0 || desc == nullptr ? frame_->height
                                                         : AV_CEIL_RSHIFT(frame_->height, desc->log2_chroma_h);
            data.bytesPerLine[i] = frame_->linesize[i];
            data.data[i] = frame_->data[i];
            data.dataSize[i] = frame_->linesize[i] * height;
        }
        return data;
    }

    QVideoFrameFormat format() const override { return format_; }

  private:
    AVFrame* frame_;
    QVideoFrameFormat format_;
};

} // namespace

/// FFmpeg asks which output format to use once it has parsed the stream: the GPU's, when it
/// offers it for this stream (codec, profile and size), else software.
struct VideoDecoderAccess {
    static AVPixelFormat getFormat(AVCodecContext* context, const AVPixelFormat* formats) {
        auto* self = static_cast<VideoDecoder*>(context->opaque);
        for (const AVPixelFormat* f = formats; *f != AV_PIX_FMT_NONE; ++f) {
            if (*f == self->hwPixelFormat_) {
                return *f;
            }
        }
        self->hardwareFailed_ = true;
        return avcodec_default_get_format(context, formats);
    }
};

VideoDecoder::VideoDecoder() {
    av_log_set_level(qEnvironmentVariableIsSet("IMAGO_FFMPEG_LOG") ? AV_LOG_WARNING : AV_LOG_QUIET);
    packet_ = av_packet_alloc();
    frame_ = av_frame_alloc();
    decodeThread_ = std::thread([this] { decodeLoop(); });
    presentThread_ = std::thread([this] { presentLoop(); });
}

VideoDecoder::~VideoDecoder() {
    {
        std::lock_guard lock(queueMu_);
        stopping_ = true;
    }
    queueCv_.notify_all();
    decodeThread_.join();
    {
        std::lock_guard lock(presentMu_);
        presenterStopping_ = true;
    }
    presentCv_.notify_all();
    presentThread_.join();
    close();
    av_buffer_unref(&hwDevice_);
    av_packet_free(&packet_);
    av_frame_free(&frame_);
}

void VideoDecoder::setSink(QVideoSink* sink) {
    std::lock_guard lock(sinkMu_);
    sink_ = sink;
}

void VideoDecoder::setSession(imago_session* session) {
    std::lock_guard lock(sessionMu_);
    session_ = session;
}

void VideoDecoder::onFrame(const imago_video_frame& frame) {
    Packet packet;
    packet.codec = frame.codec;
    packet.keyframe = frame.keyframe;
    packet.parameterSetsChanged = frame.parameter_sets_changed;
    packet.receiveNs = frame.receive_time_ns;
    packet.displayAtNs = frame.display_delay_ns > 0 ? frame.receive_time_ns + frame.display_delay_ns : 0;
    packet.frameNumber = frame.frame_number;
    packet.generation = generation_.load();
    packet.data.reserve(frame.size + frame.vps_size + frame.sps_size + frame.pps_size + 16 +
                        AV_INPUT_BUFFER_PADDING_SIZE);
    // The core removes parameter sets from the frames; the decoder finds them in band, in front
    // of every keyframe, so it can start (or restart) on any of them.
    if (frame.keyframe || frame.parameter_sets_changed) {
        appendNal(packet.data, frame.vps, frame.vps_size);
        appendNal(packet.data, frame.sps, frame.sps_size);
        appendNal(packet.data, frame.pps, frame.pps_size);
    }
    packet.data.insert(packet.data.end(), frame.data, frame.data + frame.size);
    {
        std::lock_guard lock(queueMu_);
        if (queue_.size() >= kMaxQueued) {
            // The decoder cannot keep up: drop the backlog and restart at the next keyframe.
            skipped_.fetch_add(static_cast<uint32_t>(queue_.size()));
            queue_.clear();
            queue_.push_back(Packet{}); // no codec: the marker for "skip to a keyframe"
        }
        queue_.push_back(std::move(packet));
    }
    queueCv_.notify_one();
}

void VideoDecoder::reset() {
    {
        std::lock_guard lock(queueMu_);
        queue_.clear();
        resetRequested_ = true;
    }
    queueCv_.notify_one();
    {
        std::lock_guard lock(presentMu_);
        scheduled_.clear();
    }
    {
        // Pictures of the old stream still being decoded or scheduled are not shown.
        std::lock_guard lock(sinkMu_);
        generation_.fetch_add(1);
        if (sink_ != nullptr) {
            sink_->setVideoFrame(QVideoFrame());
        }
        handedSinceSwap_ = 0;
    }
    decodeFailures_ = 0;
    skipped_ = 0;
    rendered_ = 0;
}

VideoDecoder::Counters VideoDecoder::counters() const {
    return Counters{decodeFailures_.load(), skipped_.load(), rendered_.load()};
}

QImage VideoDecoder::snapshot() {
    QVideoFrame frame;
    {
        std::lock_guard lock(sinkMu_);
        if (sink_ != nullptr) {
            frame = sink_->videoFrame();
        }
    }
    return frame.isValid() ? frame.toImage() : QImage();
}

QString VideoDecoder::decoderName() const {
    std::lock_guard lock(nameMu_);
    return decoderName_;
}

// MARK: Decode thread

void VideoDecoder::decodeLoop() {
    while (true) {
        Packet packet;
        bool doReset = false;
        {
            std::unique_lock lock(queueMu_);
            queueCv_.wait(lock, [this] { return stopping_ || resetRequested_ || !queue_.empty(); });
            if (stopping_) {
                return;
            }
            if (resetRequested_) {
                resetRequested_ = false;
                doReset = true;
            } else {
                packet = std::move(queue_.front());
                queue_.pop_front();
            }
        }
        if (doReset) {
            close();
            codec_ = IMAGO_CODEC_UNKNOWN;
            hardwareFailed_ = false;
            width_ = 0;
            height_ = 0;
            continue;
        }
        if (packet.codec == IMAGO_CODEC_UNKNOWN) {
            // Backlog dropped: the next picture must be a keyframe.
            waitingForKeyframe_ = true;
            waitingSinceNs_ = imago_now_ns();
            if (context_ != nullptr) {
                avcodec_flush_buffers(context_);
            }
            continue;
        }
        currentGeneration_ = packet.generation;
        decode(packet);
    }
}

void VideoDecoder::decode(Packet& packet) {
    if (packet.codec != codec_ || packet.parameterSetsChanged || context_ == nullptr) {
        // A new stream (or new parameter sets after a restart): a fresh decoder, from a keyframe.
        close();
        if (!open(packet.codec)) {
            failed();
            return;
        }
        codec_ = packet.codec;
        waitingForKeyframe_ = true;
        waitingSinceNs_ = imago_now_ns();
    }
    if (waitingForKeyframe_) {
        if (!packet.keyframe && imago_now_ns() - waitingSinceNs_ < kKeyframeWaitNs) {
            return;
        }
        waitingForKeyframe_ = false;
    }

    timings_[packet.frameNumber % timings_.size()] =
        Timing{packet.frameNumber, packet.receiveNs, packet.displayAtNs};

    if (av_new_packet(packet_, static_cast<int>(packet.data.size())) < 0) {
        failed();
        return;
    }
    std::memcpy(packet_->data, packet.data.data(), packet.data.size());
    packet_->pts = packet.frameNumber;
    packet_->dts = packet.frameNumber;
    if (packet.keyframe) {
        packet_->flags |= AV_PKT_FLAG_KEY;
    }
    const int sent = avcodec_send_packet(context_, packet_);
    av_packet_unref(packet_);
    if (sent < 0 && sent != AVERROR(EAGAIN)) {
        failed();
    }
    receivePictures();
}

void VideoDecoder::receivePictures() {
    while (context_ != nullptr) {
        const int result = avcodec_receive_frame(context_, frame_);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
            return;
        }
        if (result < 0) {
            failed();
            return;
        }
        picture(frame_);
        av_frame_unref(frame_);
    }
}

void VideoDecoder::picture(AVFrame* decoded) {
    AVFrame* out = av_frame_alloc();
    if (out == nullptr) {
        failed();
        return;
    }
    if (decoded->format == hwPixelFormat_ && decoded->hw_frames_ctx != nullptr) {
        // Copied back from the GPU, as NV12 (or P010 for 10-bit), for Qt to upload.
        if (av_hwframe_transfer_data(out, decoded, 0) < 0) {
            av_frame_free(&out);
            failed();
            return;
        }
        av_frame_copy_props(out, decoded);
    } else if (av_frame_ref(out, decoded) < 0) {
        av_frame_free(&out);
        failed();
        return;
    }
    consecutiveErrors_ = 0;

    const QVideoFrameFormat::PixelFormat pixelFormat = qtFormat(out->format);
    if (pixelFormat == QVideoFrameFormat::Format_Invalid) {
        av_frame_free(&out);
        failed();
        return;
    }
    QVideoFrameFormat format(QSize(out->width, out->height), pixelFormat);
    const bool full = out->color_range == AVCOL_RANGE_JPEG || decoded->format == AV_PIX_FMT_YUVJ420P;
    format.setColorRange(full ? QVideoFrameFormat::ColorRange_Full : QVideoFrameFormat::ColorRange_Video);
    switch (out->colorspace) {
    case AVCOL_SPC_BT709:
        format.setColorSpace(QVideoFrameFormat::ColorSpace_BT709);
        break;
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
        format.setColorSpace(QVideoFrameFormat::ColorSpace_BT601);
        break;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
        format.setColorSpace(QVideoFrameFormat::ColorSpace_BT2020);
        break;
    default:
        // Unspecified: HD streams are BT.709 in practice.
        format.setColorSpace(out->height >= 720 ? QVideoFrameFormat::ColorSpace_BT709
                                                : QVideoFrameFormat::ColorSpace_BT601);
        break;
    }

    if (out->width != width_ || out->height != height_) {
        width_ = out->width;
        height_ = out->height;
        if (onVideoSize) {
            onVideoSize(width_, height_);
        }
    }

    Picture p;
    p.generation = currentGeneration_;
    p.decodedNs = imago_now_ns();
    const Timing& timing = timings_[static_cast<uint64_t>(decoded->pts) % timings_.size()];
    if (timing.pts == decoded->pts) {
        p.receiveNs = timing.receiveNs;
        p.displayAtNs = timing.displayAtNs;
    }
    p.frame = QVideoFrame(std::make_unique<FrameBuffer>(out, format));
    deliver(std::move(p));
}

bool VideoDecoder::open(imago_codec codec) {
    const AVCodec* decoder = avcodec_find_decoder(codec == IMAGO_CODEC_H265 ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264);
    if (decoder == nullptr || codec == IMAGO_CODEC_UNKNOWN) {
        return false;
    }
    context_ = avcodec_alloc_context3(decoder);
    if (context_ == nullptr) {
        return false;
    }
    // One picture out for each frame in: no reordering delay, no frame threads.
    context_->flags |= AV_CODEC_FLAG_LOW_DELAY;
    context_->flags2 |= AV_CODEC_FLAG2_FAST;
    context_->thread_type = FF_THREAD_SLICE;
    context_->thread_count = 0;
    context_->opaque = this;

    QString name = QStringLiteral("Software");
    hwPixelFormat_ = AV_PIX_FMT_NONE;
    if (hardwareAllowed() && !hardwareFailed_) {
        for (const AVHWDeviceType type : kHardware) {
            AVPixelFormat pixelFormat = AV_PIX_FMT_NONE;
            for (int i = 0;; ++i) {
                const AVCodecHWConfig* config = avcodec_get_hw_config(decoder, i);
                if (config == nullptr) {
                    break;
                }
                if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) != 0 &&
                    config->device_type == type) {
                    pixelFormat = config->pix_fmt;
                    break;
                }
            }
            if (pixelFormat == AV_PIX_FMT_NONE) {
                continue;
            }
            // The device is kept across streams: opening one takes tens of milliseconds.
            if (hwDevice_ == nullptr || hwDeviceType_ != type) {
                av_buffer_unref(&hwDevice_);
                if (av_hwdevice_ctx_create(&hwDevice_, type, nullptr, nullptr, 0) < 0) {
                    hwDevice_ = nullptr;
                    continue;
                }
                hwDeviceType_ = type;
            }
            context_->hw_device_ctx = av_buffer_ref(hwDevice_);
            context_->get_format = VideoDecoderAccess::getFormat;
            hwPixelFormat_ = pixelFormat;
            name = QString::fromLatin1(av_hwdevice_get_type_name(type)).toUpper();
            break;
        }
    }
    if (avcodec_open2(context_, decoder, nullptr) < 0) {
        avcodec_free_context(&context_);
        return false;
    }
    std::lock_guard lock(nameMu_);
    decoderName_ = name;
    return true;
}

void VideoDecoder::close() {
    if (context_ != nullptr) {
        avcodec_free_context(&context_);
    }
    consecutiveErrors_ = 0;
}

void VideoDecoder::failed() {
    decodeFailures_.fetch_add(1);
    if (++consecutiveErrors_ < kMaxConsecutiveErrors || context_ == nullptr) {
        return;
    }
    // Something is wrong with this decoder: rebuild it, in software if the GPU was decoding.
    if (hwPixelFormat_ != AV_PIX_FMT_NONE) {
        hardwareFailed_ = true;
    }
    close();
    codec_ = IMAGO_CODEC_UNKNOWN;
}

// MARK: Presenting

void VideoDecoder::deliver(Picture picture) {
    {
        std::lock_guard lock(presentMu_);
        // Held back for smoothing, or behind pictures that are: in order, on the presenter.
        if (!scheduled_.empty() || picture.displayAtNs > imago_now_ns() + 1'000'000) {
            if (picture.displayAtNs == 0) {
                picture.displayAtNs = imago_now_ns();
            }
            scheduled_.push_back(std::move(picture));
            presentCv_.notify_one();
            return;
        }
    }
    show(picture);
}

void VideoDecoder::presentLoop() {
    std::unique_lock lock(presentMu_);
    while (!presenterStopping_) {
        if (scheduled_.empty()) {
            presentCv_.wait(lock);
            continue;
        }
        const int64_t dueNs = scheduled_.front().displayAtNs;
        const int64_t nowNs = imago_now_ns();
        if (dueNs > nowNs) {
            presentCv_.wait_for(lock, std::chrono::nanoseconds(dueNs - nowNs));
            continue;
        }
        Picture next = std::move(scheduled_.front());
        scheduled_.pop_front();
        lock.unlock();
        show(next);
        lock.lock();
    }
}

void VideoDecoder::show(const Picture& picture) {
    std::lock_guard lock(sinkMu_);
    if (sink_ == nullptr || picture.generation != generation_.load()) {
        return;
    }
    sink_->setVideoFrame(picture.frame);
    ++handedSinceSwap_;
    pendingReceiveNs_ = picture.receiveNs;
    pendingDecodedNs_ = picture.decodedNs;
}

void VideoDecoder::framePresented() {
    int64_t receiveNs = 0;
    int64_t decodedNs = 0;
    {
        std::lock_guard lock(sinkMu_);
        if (handedSinceSwap_ == 0) {
            return;
        }
        skipped_.fetch_add(handedSinceSwap_ - 1);
        rendered_.fetch_add(1);
        handedSinceSwap_ = 0;
        receiveNs = pendingReceiveNs_;
        decodedNs = pendingDecodedNs_;
    }
    if (receiveNs == 0) {
        return;
    }
    std::lock_guard lock(sessionMu_);
    if (session_ != nullptr) {
        imago_report_frame_presented(session_, receiveNs, decodedNs, imago_now_ns());
    }
}
