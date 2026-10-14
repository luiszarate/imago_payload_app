// C API: thin wrappers over imago::Session. No exception may cross this boundary.

#include "imago/imago_core.h"
#include "payload/orus_protocol.h"
#include "platform/clock.h"
#include "session/session.h"

#include <cstring>
#include <new>

struct imago_session {
    imago::Session impl;
    imago_session(const imago_config& cfg, imago_event_fn e, imago_video_frame_fn f, void* u)
        : impl(cfg, e, f, u) {}
};

namespace {

template <typename F> auto guarded(imago_session* s, F&& f, decltype(f()) fallback) noexcept {
    if (s == nullptr) {
        return fallback;
    }
    try {
        return f();
    } catch (...) {
        return fallback;
    }
}

} // namespace

extern "C" {

void imago_config_init(imago_config* cfg) {
    if (cfg == nullptr) {
        return;
    }
    std::memset(cfg, 0, sizeof(*cfg));
    cfg->payload_port = imago::orus::kDefaultPort;
    cfg->system_id = 1;
    cfg->component_id = MAV_COMP_ID_ONBOARD_COMPUTER3;
    cfg->rtsp_use_payload_host = true;
    cfg->auto_start_video = true;
    cfg->rtsp_transport = IMAGO_RTSP_AUTO;
    cfg->nal_format = IMAGO_NAL_AVCC;
    cfg->link_timeout_ms = 3000;
    cfg->gimbal_rate_hz = 30;
    cfg->command_timeout_ms = 500;
    cfg->command_retries = 3;
    cfg->video_resync_on_lag = true;
    cfg->video_max_lag_ms = 500;
}

int64_t imago_now_ns(void) { return imago::monotonicNs(); }

imago_session* imago_session_create(const imago_config* cfg, imago_event_fn on_event,
                                    imago_video_frame_fn on_video_frame, void* user) {
    if (cfg == nullptr) {
        return nullptr;
    }
    imago_config c = *cfg;
    if (c.payload_port == 0) c.payload_port = imago::orus::kDefaultPort;
    if (c.system_id == 0) c.system_id = 1;
    if (c.component_id == 0) c.component_id = MAV_COMP_ID_ONBOARD_COMPUTER3;
    if (c.link_timeout_ms == 0) c.link_timeout_ms = 3000;
    if (c.gimbal_rate_hz == 0) c.gimbal_rate_hz = 30;
    if (c.command_timeout_ms == 0) c.command_timeout_ms = 500;
    if (c.command_retries == 0) c.command_retries = 3;
    if (c.video_max_lag_ms == 0) c.video_max_lag_ms = 500;
    if (c.payload_ip != nullptr && c.payload_ip[0] != '\0' &&
        !imago::net::Endpoint::parse(c.payload_ip, c.payload_port)) {
        return nullptr;
    }
    try {
        return new imago_session(c, on_event, on_video_frame, user);
    } catch (...) {
        return nullptr;
    }
}

int imago_session_start(imago_session* s) {
    return guarded(s, [&] { return s->impl.start(); }, static_cast<int>(IMAGO_ERR_STATE));
}

void imago_session_stop(imago_session* s) {
    guarded(s, [&] { s->impl.stop(); return 0; }, 0);
}

void imago_session_destroy(imago_session* s) {
    if (s == nullptr) {
        return;
    }
    try {
        s->impl.stop();
    } catch (...) {
    }
    delete s;
}

void imago_session_get_state(imago_session* s, imago_state* out) {
    if (out == nullptr) {
        return;
    }
    std::memset(out, 0, sizeof(*out));
    guarded(s, [&] { s->impl.getState(*out); return 0; }, 0);
}

int imago_video_start(imago_session* s, const char* url) {
    return guarded(s, [&] { return s->impl.startVideo(url); }, static_cast<int>(IMAGO_ERR_STATE));
}

void imago_video_stop(imago_session* s) {
    guarded(s, [&] { s->impl.stopVideo(); return 0; }, 0);
}

void imago_report_frame_presented(imago_session* s, int64_t receive_time_ns,
                                  int64_t decoded_time_ns, int64_t presented_time_ns) {
    guarded(s, [&] {
        s->impl.reportPresented(receive_time_ns, decoded_time_ns, presented_time_ns);
        return 0;
    }, 0);
}

void imago_gimbal_set_rate(imago_session* s, float pitch_dps, float yaw_dps) {
    guarded(s, [&] { s->impl.setGimbalRate(pitch_dps, yaw_dps); return 0; }, 0);
}

uint32_t imago_gimbal_set_angle(imago_session* s, float pitch_deg, float yaw_deg) {
    return guarded(s, [&] { return s->impl.setGimbalAngle(pitch_deg, yaw_deg); }, 0u);
}

uint32_t imago_gimbal_set_mode(imago_session* s, imago_gimbal_mode mode) {
    return guarded(s, [&] { return s->impl.setGimbalMode(mode); }, 0u);
}

void imago_gimbal_set_protocol(imago_session* s, imago_gimbal_protocol protocol) {
    guarded(s, [&] { s->impl.setGimbalProtocol(protocol); return 0; }, 0);
}

uint32_t imago_camera_set_mode(imago_session* s, imago_camera_mode mode) {
    return guarded(s, [&] { return s->impl.setCameraMode(mode); }, 0u);
}

uint32_t imago_camera_take_photo(imago_session* s) {
    return guarded(s, [&] { return s->impl.takePhoto(); }, 0u);
}

uint32_t imago_camera_set_recording(imago_session* s, bool recording) {
    return guarded(s, [&] { return s->impl.setRecording(recording); }, 0u);
}

uint32_t imago_camera_zoom(imago_session* s, int direction) {
    return guarded(s, [&] { return s->impl.zoom(direction); }, 0u);
}

uint32_t imago_camera_zoom_to(imago_session* s, float level) {
    return guarded(s, [&] { return s->impl.zoomTo(level); }, 0u);
}

uint32_t imago_camera_focus(imago_session* s, int direction) {
    return guarded(s, [&] { return s->impl.focus(direction); }, 0u);
}

uint32_t imago_camera_autofocus(imago_session* s) {
    return guarded(s, [&] { return s->impl.autoFocus(); }, 0u);
}

uint32_t imago_camera_set_view(imago_session* s, imago_view_source source) {
    return guarded(s, [&] {
        return s->impl.setCameraParam(imago::orus::param::kViewSource,
                                      static_cast<uint32_t>(source));
    }, 0u);
}

uint32_t imago_camera_set_param(imago_session* s, const char* param_id, uint32_t value) {
    if (param_id == nullptr) {
        return 0;
    }
    return guarded(s, [&] { return s->impl.setCameraParam(param_id, value); }, 0u);
}

uint32_t imago_tracker_set_function(imago_session* s, imago_tracker_function function) {
    return guarded(s, [&] {
        return s->impl.setCameraParam(imago::orus::param::kTrackerFunction,
                                      static_cast<uint32_t>(function));
    }, 0u);
}

uint32_t imago_tracking_set_mode(imago_session* s, imago_tracking_mode mode) {
    return guarded(s, [&] { return s->impl.setTrackingMode(mode); }, 0u);
}

uint32_t imago_tracking_start_at(imago_session* s, float x, float y, float w, float h) {
    return guarded(s, [&] { return s->impl.trackAt(x, y, w, h); }, 0u);
}

uint32_t imago_tracking_point_at(imago_session* s, float x, float y) {
    return guarded(s, [&] { return s->impl.pointAt(x, y); }, 0u);
}

uint32_t imago_tracking_stop(imago_session* s) {
    return guarded(s, [&] { return s->impl.setTrackingMode(IMAGO_TRACKING_STOP); }, 0u);
}

uint32_t imago_lrf_set_mode(imago_session* s, uint32_t mode) {
    if (mode > 3) {
        return 0;
    }
    return guarded(s, [&] { return s->impl.setCameraParam(imago::orus::param::kLrfMode, mode); },
                   0u);
}

uint32_t imago_stream_set_bitrate(imago_session* s, uint32_t cam_id, uint32_t bits_per_s) {
    return guarded(s, [&] { return s->impl.streamSetting(cam_id, 0, bits_per_s); }, 0u);
}

uint32_t imago_stream_set_resolution(imago_session* s, uint32_t cam_id, uint32_t level) {
    return guarded(s, [&] { return s->impl.streamSetting(cam_id, 1, level); }, 0u);
}

} // extern "C"
