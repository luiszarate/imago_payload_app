#include "operator.h"

#include "hud_layout.h"
#include "jni_util.h"

#include <android/log.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace imago::quest {

namespace {

constexpr const char* kTag = "imago-quest";
constexpr int64_t kNsPerSec = 1'000'000'000;
// Gimbal manager: retake control at most this often (as the other apps do).
constexpr int64_t kControlRetryNs = 5 * kNsPerSec;
// A stick past this is a click-like command on an analog input (triggers, grips).
constexpr float kStickZoomThreshold = 0.5f;

void log(const char* text) { __android_log_print(ANDROID_LOG_INFO, kTag, "%s", text); }

} // namespace

Operator::Operator(jobject activity, std::vector<Profile> profiles)
    : activity_(activity), profiles_(std::move(profiles)) {
    JNIEnv* env = android::currentEnv();
    jclass cls = env->GetObjectClass(activity_);
    onVideoSize_ = env->GetMethodID(cls, "onVideoSize", "(II)V");
    onNotice_ = env->GetMethodID(cls, "onNotice", "([B)V");
    env->DeleteLocalRef(cls);
    // The decoder reports the picture size to the activity, like the Android renderer.
    decoder_ = std::make_unique<android::VideoDecoder>(env->NewGlobalRef(activity_));
}

Operator::~Operator() {
    disconnect();
    decoder_.reset();
}

void Operator::setVideoWindow(ANativeWindow* window) { decoder_->setSurface(window); }

void Operator::connect(size_t profileIndex) {
    disconnect();
    if (profiles_.empty()) {
        notice("No connection profile");
        return;
    }
    profileIndex_ = profileIndex % profiles_.size();
    const Profile& p = profiles_[profileIndex_];

    imago_config config;
    imago_config_init(&config);
    config.payload_ip = p.listen || p.payloadIp.empty() ? nullptr : p.payloadIp.c_str();
    config.payload_port = static_cast<uint16_t>(p.mavlinkPort);
    config.local_port = static_cast<uint16_t>(p.localPort);
    config.rtsp_url = p.videoUrl.empty() ? nullptr : p.videoUrl.c_str();
    config.rtsp_use_payload_host = true;
    config.auto_start_video = true;
    config.rtsp_transport = static_cast<imago_rtsp_transport>(p.transport);
    config.nal_format = IMAGO_NAL_ANNEXB; // what AMediaCodec takes
    config.gimbal_protocol = static_cast<imago_gimbal_protocol>(p.gimbalProtocol);
    config.video_smoothing_ms = static_cast<uint32_t>(std::max(0, p.videoSmoothingMs));
    config.video_resync_on_lag = p.maxVideoLagMs > 0;
    if (p.maxVideoLagMs > 0) {
        config.video_max_lag_ms = static_cast<uint32_t>(p.maxVideoLagMs);
    }
    session_ = imago_session_create(&config, &Operator::onEvent, &Operator::onVideoFrame, this);
    if (session_ == nullptr) {
        notice("Profile \"" + p.name + "\" is invalid");
        return;
    }
    imago_head_track_configure(session_, &p.head);
    decoder_->setSession(session_);
    decoder_->reset();
    if (imago_session_start(session_) != 0) {
        notice("Could not open the MAVLink socket");
        decoder_->setSession(nullptr);
        imago_session_destroy(session_);
        session_ = nullptr;
        return;
    }
    headOn_ = false;
    headStarted_ = false;
    controlReleasedByUs_ = false;
    nextControlNs_ = 0;
    zoomDirection_ = 0;
    connected_ = true;
    notice("Connecting: " + p.name);
}

void Operator::disconnect() {
    if (session_ == nullptr) {
        return;
    }
    connected_ = false;
    linkUp_ = false;
    decoder_->setSession(nullptr);
    imago_session_destroy(session_); // stops first: the gimbal gets a zero rate
    session_ = nullptr;
    std::lock_guard lock(stateMutex_);
    std::memset(&state_, 0, sizeof(state_));
}

// MARK: Core callbacks (core threads)

void Operator::onEvent(const imago_event* event, void* user) {
    static_cast<Operator*>(user)->handleEvent(*event);
}

void Operator::onVideoFrame(const imago_video_frame* frame, void* user) {
    auto* self = static_cast<Operator*>(user);
    self->decoder_->onFrame(*frame);
}

void Operator::label(uint32_t requestId, const char* what) {
    if (requestId == 0) {
        notice(std::string(what) + " not sent");
        return;
    }
    std::lock_guard lock(labelsMutex_);
    labels_[requestId] = what;
    if (labels_.size() > 64) {
        labels_.erase(labels_.begin());
    }
}

void Operator::handleEvent(const imago_event& e) {
    const std::string text = e.text != nullptr ? e.text : "";
    switch (e.type) {
    case IMAGO_EVENT_LINK_UP:
        linkUp_ = true;
        notice("Link up");
        break;
    case IMAGO_EVENT_LINK_DOWN:
        linkUp_ = false;
        notice("Link down");
        break;
    case IMAGO_EVENT_VIDEO_ERROR:
        notice("Video: " + text);
        break;
    case IMAGO_EVENT_VIDEO_RESYNC:
        notice("Video restarted: " + std::to_string(e.code) + " ms behind live");
        break;
    case IMAGO_EVENT_STATUS_TEXT:
        notice("Payload: " + text);
        break;
    case IMAGO_EVENT_TRACKING_IGNORED:
        notice(std::string("Tracker ignored the ") + text);
        break;
    case IMAGO_EVENT_ZOOM_CHANGED_EXTERNALLY:
        notice("Zoom moved by another controller: " + text);
        break;
    case IMAGO_EVENT_COMMAND_RESULT:
    case IMAGO_EVENT_PARAM_RESULT: {
        std::string what;
        {
            std::lock_guard lock(labelsMutex_);
            auto it = labels_.find(e.request_id);
            if (it == labels_.end()) {
                return;
            }
            what = it->second;
            labels_.erase(it);
        }
        if (e.result == IMAGO_RESULT_ACCEPTED) {
            if (what == "Photo") {
                notice("Photo taken");
            }
        } else if (e.result == IMAGO_RESULT_TIMEOUT) {
            notice(what + ": no answer");
        } else {
            notice(what + " failed (code " + std::to_string(e.result) + ")");
        }
        break;
    }
    default:
        break;
    }
}

void Operator::notice(const std::string& text) {
    log(text.c_str());
    JNIEnv* env = android::currentEnv();
    if (env == nullptr || onNotice_ == nullptr) {
        return;
    }
    const auto length = static_cast<jsize>(text.size());
    jbyteArray bytes = env->NewByteArray(length);
    if (bytes == nullptr) {
        return;
    }
    env->SetByteArrayRegion(bytes, 0, length, reinterpret_cast<const jbyte*>(text.data()));
    env->CallVoidMethod(activity_, onNotice_, bytes);
    android::clearException(env);
    env->DeleteLocalRef(bytes);
}

void Operator::setVideoSize(int width, int height) {
    if (width > 0 && height > 0) {
        videoAspect_ = static_cast<float>(width) / static_cast<float>(height);
    }
}

// MARK: Per frame (XR thread)

float Operator::shape(float value) const {
    const Profile& p = profile();
    const float magnitude = std::min(std::fabs(value), 1.0f);
    if (magnitude <= p.deadzone) {
        return 0;
    }
    const float n = (magnitude - p.deadzone) / (1 - p.deadzone);
    const float curved = (1 - p.expo) * n + p.expo * n * n * n;
    return value < 0 ? -curved : curved;
}

void Operator::setHeadTracking(bool on) {
    if (session_ == nullptr) {
        return;
    }
    headOn_ = on;
    imago_head_track_set_mode(session_, on ? static_cast<imago_head_track_mode>(profile().headMode)
                                           : IMAGO_HEAD_TRACK_OFF);
    notice(on ? "Head tracking on" : "Head tracking off");
}

void Operator::headPose(const XrQuaternionf& q, int64_t timeNs) {
    if (session_ != nullptr) {
        imago_head_track_pose_quaternion(session_, q.w, q.x, q.y, q.z, timeNs);
    }
}

void Operator::tick(const Input& in, const Pointer& pointer, int64_t nowNs) {
    pointer_ = pointer;
    const auto rose = [&](bool Input::*field) { return in.*field && !(last_.*field); };

    // Profiles and the screen work without a session.
    if (rose(&Input::menu)) {
        connect(profileIndex_ + 1);
    }
    if (rose(&Input::rightStickClick)) {
        placePanel_ = true;
    }

    imago_state state{};
    if (session_ != nullptr) {
        imago_session_get_state(session_, &state);
        {
            std::lock_guard lock(stateMutex_);
            state_ = state;
        }
        keepGimbalControl(state, nowNs);
        if (linkUp_ && !headStarted_ && profile().headOnAtStart) {
            // On from the first link; the A button toggles it afterwards.
            headStarted_ = true;
            setHeadTracking(true);
        }
    }

    if (session_ != nullptr && in.focused) {
        const Profile& p = profile();
        // Right stick: the gimbal joystick, or a trim while the head drives the gimbal.
        const float zoomFactor = std::pow(std::max(1.0f, state.eo_zoom_level), -p.zoomCompensation);
        float yawDps = shape(in.rightStick.x) * p.maxYawDps * zoomFactor;
        float pitchDps = shape(in.rightStick.y) * p.maxPitchDps * zoomFactor;
        if (headOn_) {
            yawDps *= p.trimFactor;
            pitchDps *= p.trimFactor;
        }
        imago_gimbal_set_rate(session_, pitchDps, yawDps);

        // Left stick up/down: continuous zoom, one stop when released.
        const int zoom = in.leftStick.y > kStickZoomThreshold ? 1
                         : in.leftStick.y < -kStickZoomThreshold ? -1 : 0;
        if (zoom != zoomDirection_) {
            zoomDirection_ = zoom;
            imago_camera_zoom(session_, zoom);
        }

        if (rose(&Input::a)) {
            setHeadTracking(!headOn_);
        }
        if (rose(&Input::b)) {
            label(imago_tracking_stop(session_), "Stop tracking");
        }
        if (rose(&Input::rightTrigger) && pointer.valid) {
            label(imago_tracking_start_at(session_, pointer.x, pointer.y, 0, 0), "Track");
        }
        if (rose(&Input::rightGrip) && pointer.valid) {
            label(imago_tracking_point_at(session_, pointer.x, pointer.y), "Point");
        }
        if (rose(&Input::x)) {
            label(imago_camera_take_photo(session_), "Photo");
        }
        if (rose(&Input::y)) {
            label(imago_camera_set_recording(session_, !state.recording),
                  state.recording ? "Stop recording" : "Record");
        }
        if (rose(&Input::leftTrigger)) {
            static constexpr imago_view_source kCycle[] = {IMAGO_VIEW_EO_IR, IMAGO_VIEW_EO,
                                                           IMAGO_VIEW_IR, IMAGO_VIEW_IR_EO};
            size_t next = 0;
            for (size_t k = 0; k < 4; ++k) {
                if (kCycle[k] == state.view_source) {
                    next = (k + 1) % 4;
                }
            }
            label(imago_camera_set_view(session_, kCycle[next]), "View");
        }
        if (rose(&Input::leftGrip)) {
            label(imago_gimbal_set_mode(session_, IMAGO_GIMBAL_MODE_RESET), "Center");
        }
        if (rose(&Input::leftStickClick)) {
            imago_head_track_recentre(session_);
            notice("Head recentred: looking at the nose");
        }
    } else if (session_ != nullptr && last_.focused) {
        // Focus lost (the system menu): never leave anything moving.
        imago_gimbal_set_rate(session_, 0, 0);
        if (zoomDirection_ != 0) {
            zoomDirection_ = 0;
            imago_camera_zoom(session_, 0);
        }
    }
    last_ = in;
}

void Operator::keepGimbalControl(const imago_state& state, int64_t nowNs) {
    if (profile().gimbalProtocol != IMAGO_GIMBAL_PROTOCOL_MANAGER || !linkUp_) {
        return;
    }
    if (state.gimbal_control != IMAGO_GIMBAL_CONTROL_NOBODY &&
        state.gimbal_control != IMAGO_GIMBAL_CONTROL_OTHER) {
        return;
    }
    // A ground station (255, or component 190) in control is left alone, as in the other apps.
    if (state.gimbal_control == IMAGO_GIMBAL_CONTROL_OTHER &&
        (state.gimbal_control_system == 255 || state.gimbal_control_component == 190)) {
        return;
    }
    if (controlReleasedByUs_ || nowNs < nextControlNs_) {
        return;
    }
    nextControlNs_ = nowNs + kControlRetryNs;
    label(imago_gimbal_take_control(session_, true), "Take control");
}

// MARK: HUD

void Operator::hudSnapshot(float* out) const {
    std::memset(out, 0, sizeof(float) * hud::kSize);
    imago_state s;
    {
        std::lock_guard lock(stateMutex_);
        s = state_;
    }
    const Profile& p = profile();
    out[hud::kLinkUp] = s.link_up ? 1.0f : 0.0f;
    out[hud::kConnected] = connected_ ? 1.0f : 0.0f;
    out[hud::kProfileIndex] = static_cast<float>(profileIndex_);
    out[hud::kVideoActive] = s.video_active ? 1.0f : 0.0f;
    out[hud::kFps] = s.video_fps;
    out[hud::kBitrateKbps] = s.video_bitrate_kbps;
    out[hud::kLagMs] = s.video_lag_ms;
    out[hud::kLagStale] = p.maxVideoLagMs > 0 && s.video_lag_ms >= static_cast<float>(p.maxVideoLagMs) ? 1.0f : 0.0f;
    out[hud::kGimbalValid] = s.gimbal_valid ? 1.0f : 0.0f;
    out[hud::kPitch] = s.gimbal_pitch_deg;
    out[hud::kYaw] = s.gimbal_yaw_valid ? s.gimbal_yaw_deg : 0.0f;
    out[hud::kPanNearLimit] = s.gimbal_pan_near_limit ? 1.0f : 0.0f;
    out[hud::kGimbalMode] = static_cast<float>(s.gimbal_mode);
    out[hud::kZoom] = s.eo_zoom_level;
    out[hud::kViewSource] = static_cast<float>(s.view_source);
    out[hud::kRecording] = s.recording ? 1.0f : 0.0f;
    out[hud::kRecordingTimeMs] = static_cast<float>(s.recording_time_ms);
    out[hud::kTrackingMode] = static_cast<float>(s.tracking_mode);
    out[hud::kTrackingState] = static_cast<float>(s.tracking_state);
    out[hud::kBoxValid] = s.tracking_box_valid ? 1.0f : 0.0f;
    out[hud::kBoxX] = s.tracking_box.x;
    out[hud::kBoxY] = s.tracking_box.y;
    out[hud::kBoxW] = s.tracking_box.w;
    out[hud::kBoxH] = s.tracking_box.h;
    out[hud::kHeadMode] = static_cast<float>(s.head_track_mode);
    out[hud::kHeadFollowing] = s.head_track_following ? 1.0f : 0.0f;
    out[hud::kHeadHeld] = s.head_track_held_by_tracker ? 1.0f : 0.0f;
    out[hud::kHeadPoseFresh] = s.head_track_pose_fresh ? 1.0f : 0.0f;
    out[hud::kHeadError] = s.head_track_error_deg;
    out[hud::kPointerValid] = pointer_.valid ? 1.0f : 0.0f;
    out[hud::kPointerX] = pointer_.x;
    out[hud::kPointerY] = pointer_.y;
    out[hud::kLatencyPresentMs] = s.latency_present_ms;
    out[hud::kLrfValid] = s.lrf_valid ? 1.0f : 0.0f;
    out[hud::kLrfRangeM] = s.lrf_range_m;
    out[hud::kGimbalControl] = static_cast<float>(s.gimbal_control);
    const int count = std::min<int>(s.detection_count, hud::kMaxDetections);
    out[hud::kDetectionCount] = static_cast<float>(count);
    for (int k = 0; k < count; ++k) {
        float* d = out + hud::kDetections + k * hud::kDetectionStride;
        d[0] = s.detections[k].box.x;
        d[1] = s.detections[k].box.y;
        d[2] = s.detections[k].box.w;
        d[3] = s.detections[k].box.h;
        d[4] = static_cast<float>(s.detections[k].class_id);
        d[5] = s.detections[k].confidence;
    }
}

} // namespace imago::quest
