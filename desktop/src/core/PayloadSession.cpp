#include "core/PayloadSession.h"

#include "video/VideoDecoder.h"

#include <algorithm>

PayloadSession::PayloadSession(const ConnectionProfile& profile, VideoDecoder* video, EventHandler onEvent)
    : profile_(profile), video_(video), onEvent_(std::move(onEvent)) {}

std::unique_ptr<PayloadSession> PayloadSession::create(const ConnectionProfile& profile, VideoDecoder* video,
                                                       EventHandler onEvent) {
    std::unique_ptr<PayloadSession> session(new PayloadSession(profile, video, std::move(onEvent)));
    const QByteArray ip = profile.mavlinkMode == ConnectionProfile::MavlinkMode::Direct
                              ? profile.payloadIP.trimmed().toUtf8()
                              : QByteArray();
    const QByteArray url = profile.videoURL.trimmed().toUtf8();
    // The web settings page lives at the payload's address, also when MAVLink is forwarded.
    const QByteArray webHost = profile.payloadIP.trimmed().toUtf8();

    imago_config config;
    imago_config_init(&config);
    config.payload_ip = ip.isEmpty() ? nullptr : ip.constData();
    config.payload_port = static_cast<uint16_t>(profile.mavlinkPort);
    config.local_port = static_cast<uint16_t>(profile.localPort);
    config.rtsp_url = url.isEmpty() ? nullptr : url.constData();
    config.web_host = webHost.isEmpty() ? nullptr : webHost.constData();
    config.rtsp_use_payload_host = true;
    config.auto_start_video = true;
    config.rtsp_transport = static_cast<imago_rtsp_transport>(profile.transportValue());
    // FFmpeg reads start codes.
    config.nal_format = IMAGO_NAL_ANNEXB;
    config.gimbal_protocol = static_cast<imago_gimbal_protocol>(profile.gimbalProtocolValue());
    config.wifi_keep_awake = profile.keepWifiAwake;
    config.video_smoothing_ms = static_cast<uint32_t>(std::clamp(profile.videoSmoothingMs, 0, 500));
    config.video_resync_on_lag = profile.maxVideoLagMs > 0;
    if (profile.maxVideoLagMs > 0) {
        config.video_max_lag_ms = static_cast<uint32_t>(profile.maxVideoLagMs);
    }
    // The core copies the strings.
    session->session_ = imago_session_create(&config, &PayloadSession::eventCallback,
                                             &PayloadSession::frameCallback, session.get());
    if (session->session_ == nullptr) {
        return nullptr;
    }
    if (video != nullptr) {
        video->setSession(session->session_);
    }
    return session;
}

PayloadSession::~PayloadSession() {
    if (video_ != nullptr) {
        video_->setSession(nullptr);
    }
    if (session_ != nullptr) {
        imago_session_destroy(session_); // stops first
    }
}

bool PayloadSession::start() { return imago_session_start(session_) == 0; }

void PayloadSession::stop() {
    if (video_ != nullptr) {
        video_->setSession(nullptr);
    }
    imago_session_stop(session_);
}

void PayloadSession::eventCallback(const imago_event* event, void* user) {
    auto* self = static_cast<PayloadSession*>(user);
    if (!self->onEvent_) {
        return;
    }
    PayloadEvent e;
    e.type = event->type;
    e.requestId = event->request_id;
    e.code = event->code;
    e.result = event->result;
    // A payload's STATUSTEXT is not always valid UTF-8; invalid bytes become U+FFFD.
    if (event->text != nullptr) {
        e.text = QString::fromUtf8(event->text);
    }
    self->onEvent_(e);
}

void PayloadSession::frameCallback(const imago_video_frame* frame, void* user) {
    auto* self = static_cast<PayloadSession*>(user);
    if (self->video_ != nullptr) {
        self->video_->onFrame(*frame);
    }
}

imago_state PayloadSession::state() const {
    imago_state s;
    imago_session_get_state(session_, &s);
    return s;
}

void PayloadSession::stopVideo() { imago_video_stop(session_); }

bool PayloadSession::startVideo() { return imago_video_start(session_, nullptr) == 0; }

void PayloadSession::setGimbalRate(float pitch, float yaw) { imago_gimbal_set_rate(session_, pitch, yaw); }

uint32_t PayloadSession::setGimbalMode(int mode) {
    return imago_gimbal_set_mode(session_, static_cast<imago_gimbal_mode>(mode));
}

uint32_t PayloadSession::takeGimbalControl(bool take) { return imago_gimbal_take_control(session_, take); }

uint32_t PayloadSession::setCameraMode(int mode) {
    return imago_camera_set_mode(session_, static_cast<imago_camera_mode>(mode));
}

uint32_t PayloadSession::takePhoto() { return imago_camera_take_photo(session_); }

uint32_t PayloadSession::setRecording(bool on) { return imago_camera_set_recording(session_, on); }

uint32_t PayloadSession::zoom(int direction) { return imago_camera_zoom(session_, direction); }

void PayloadSession::zoomTarget(float level) { imago_camera_zoom_target(session_, level); }

uint32_t PayloadSession::setView(int source) {
    return imago_camera_set_view(session_, static_cast<imago_view_source>(source));
}

uint32_t PayloadSession::setCameraParam(const QString& id, int value) {
    return imago_camera_set_param(session_, id.toLatin1().constData(), static_cast<uint32_t>(value));
}

void PayloadSession::readCameraParam(const QString& id) {
    imago_camera_read_param(session_, id.toLatin1().constData());
}

uint32_t PayloadSession::setIsotherm(bool enabled, int lowC, int highC) {
    return imago_ir_set_isotherm(session_, enabled, static_cast<float>(lowC), static_cast<float>(highC));
}

uint32_t PayloadSession::setStreamBitrate(int cameraId, int bitsPerSecond) {
    return imago_stream_set_bitrate(session_, static_cast<uint32_t>(cameraId),
                                    static_cast<uint32_t>(bitsPerSecond));
}

bool PayloadSession::startRecording(const QString& path) {
    return imago_session_start_recording(session_, path.toUtf8().constData()) == 0;
}

bool PayloadSession::stopRecording() { return imago_session_stop_recording(session_) == 0; }

void PayloadSession::setRecordingDetail(bool compact) {
    imago_session_set_recording_detail(session_, compact ? IMAGO_RECORDING_COMPACT : IMAGO_RECORDING_FULL);
}

uint32_t PayloadSession::readWebIRSettings() { return imago_web_read_ir_settings(session_); }

uint32_t PayloadSession::setWebIsotherm(bool enabled, int thresholdC, int measurementRange) {
    return imago_web_set_isotherm(session_, enabled, thresholdC, measurementRange);
}

uint32_t PayloadSession::restartPayloadService() { return imago_web_restart(session_, false); }

imago_web_ir_settings PayloadSession::webIRSettings() const {
    imago_web_ir_settings settings{};
    imago_web_get_ir_settings(session_, &settings);
    return settings;
}

imago_media_state PayloadSession::mediaState() const {
    imago_media_state state{};
    imago_media_get_state(session_, &state);
    return state;
}

uint32_t PayloadSession::refreshMedia() { return imago_media_refresh(session_); }

void PayloadSession::setMediaActive(bool active) { imago_media_set_active(session_, active); }

std::vector<imago_media_file> PayloadSession::mediaFiles() const {
    // The list may grow between the two calls: ask again with the new count.
    std::vector<imago_media_file> files(mediaState().file_count + 16);
    for (;;) {
        const uint32_t count = imago_media_get_files(session_, files.data(), static_cast<uint32_t>(files.size()));
        if (count <= files.size()) {
            files.resize(count);
            return files;
        }
        files.resize(count + 16);
    }
}

std::vector<imago_media_flight> PayloadSession::mediaFlights() const {
    std::vector<imago_media_flight> flights(mediaState().flight_count + 4);
    for (;;) {
        const uint32_t count = imago_media_get_flights(session_, flights.data(), static_cast<uint32_t>(flights.size()));
        if (count <= flights.size()) {
            flights.resize(count);
            return flights;
        }
        flights.resize(count + 4);
    }
}

void PayloadSession::requestMediaThumbnail(const QString& name) {
    imago_media_request_thumbnail(session_, name.toUtf8().constData());
}

QByteArray PayloadSession::mediaThumbnail(const QString& name) const {
    const QByteArray key = name.toUtf8();
    // Thumbnails are a few KB; one retry if it was replaced between the calls.
    for (int attempt = 0; attempt < 2; ++attempt) {
        const size_t size = imago_media_copy_thumbnail(session_, key.constData(), nullptr, 0);
        if (size == 0) {
            return {};
        }
        QByteArray bytes(static_cast<qsizetype>(size), Qt::Uninitialized);
        const size_t copied = imago_media_copy_thumbnail(session_, key.constData(),
                                                         reinterpret_cast<uint8_t*>(bytes.data()), size);
        if (copied == size) {
            return bytes;
        }
    }
    return {};
}

uint32_t PayloadSession::downloadMedia(const QString& name, const QString& path) {
    return imago_media_download(session_, name.toUtf8().constData(), path.toUtf8().constData());
}

void PayloadSession::cancelMediaDownloads() { imago_media_cancel_downloads(session_); }

uint32_t PayloadSession::deleteMedia(const QString& name) {
    return imago_media_delete(session_, name.toUtf8().constData());
}

uint32_t PayloadSession::setMediaStorage(int index) {
    return imago_media_set_storage(session_, static_cast<uint32_t>(index));
}

uint32_t PayloadSession::track(float x, float y) { return imago_tracking_start_at(session_, x, y, 0.0f, 0.0f); }

uint32_t PayloadSession::track(const QRectF& box) {
    return imago_tracking_start_at(session_, static_cast<float>(box.center().x()),
                                   static_cast<float>(box.center().y()), static_cast<float>(box.width()),
                                   static_cast<float>(box.height()));
}

uint32_t PayloadSession::pointGimbal(float x, float y) { return imago_tracking_point_at(session_, x, y); }

uint32_t PayloadSession::stopTracking() { return imago_tracking_stop(session_); }

uint32_t PayloadSession::setDetection(bool enabled) {
    return imago_tracker_set_function(session_, enabled ? IMAGO_TRACKER_OBJECT_DETECTION
                                                        : IMAGO_TRACKER_OBJECT_TRACKING);
}

QList<float> PayloadSession::zoomLevels(bool superResolution) {
    float levels[64];
    const size_t count = std::min<size_t>(
        imago_zoom_levels(superResolution ? IMAGO_ZOOM_MODE_SUPER_RESOLUTION : IMAGO_ZOOM_MODE_COMBINED,
                          levels, 64),
        64);
    return QList<float>(levels, levels + count);
}
