// C API: thin wrappers over imago::Session. No exception may cross this boundary.

#include "imago/imago_core.h"
#include "payload/orus_protocol.h"
#include "platform/clock.h"
#include "session/session.h"

#include <algorithm>
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
    cfg->wifi_keep_awake = false;
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

uint32_t imago_gimbal_take_control(imago_session* s, bool take) {
    return guarded(s, [&] { return s->impl.takeGimbalControl(take); }, 0u);
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

uint32_t imago_camera_zoom_range(imago_session* s, float percent) {
    return guarded(s, [&] { return s->impl.zoomRange(percent); }, 0u);
}

size_t imago_zoom_levels(int32_t zoom_mode, float* out, size_t capacity) {
    const auto levels = imago::orus::zoomLevels(zoom_mode);
    for (size_t i = 0; out != nullptr && i < levels.size() && i < capacity; ++i) {
        out[i] = levels[i];
    }
    return levels.size();
}

int imago_session_start_recording(imago_session* s, const char* path) {
    if (path == nullptr) {
        return IMAGO_ERR_INVALID_ARG;
    }
    return guarded(s, [&] { return s->impl.startRecording(path); }, static_cast<int>(IMAGO_ERR_STATE));
}

int imago_session_stop_recording(imago_session* s) {
    return guarded(s, [&] { return s->impl.stopRecording(); }, static_cast<int>(IMAGO_ERR_STATE));
}

uint32_t imago_web_read_ir_settings(imago_session* s) {
    return guarded(s, [&] { return s->impl.webReadIrSettings(); }, 0u);
}

uint32_t imago_web_set_isotherm(imago_session* s, bool enabled, int32_t threshold_c,
                                int32_t measurement_range) {
    return guarded(s, [&] { return s->impl.webSetIsotherm(enabled, threshold_c, measurement_range); }, 0u);
}

uint32_t imago_web_restart(imago_session* s, bool full_reboot) {
    return guarded(s, [&] { return s->impl.webRestart(full_reboot); }, 0u);
}

void imago_web_get_ir_settings(imago_session* s, imago_web_ir_settings* out) {
    if (out == nullptr) {
        return;
    }
    *out = imago_web_ir_settings{};
    guarded(s, [&] { *out = s->impl.webIrSettings(); return 0; }, 0);
}

void imago_media_get_state(imago_session* s, imago_media_state* out) {
    if (out == nullptr) {
        return;
    }
    *out = imago_media_state{};
    guarded(s, [&] { *out = s->impl.media().state(); return 0; }, 0);
}

uint32_t imago_media_refresh(imago_session* s) {
    return guarded(s, [&] { return s->impl.mediaRefresh(); }, 0u);
}

uint32_t imago_media_get_files(imago_session* s, imago_media_file* out, uint32_t capacity) {
    return guarded(s, [&] { return s->impl.media().files(out, capacity); }, 0u);
}

uint32_t imago_media_get_flights(imago_session* s, imago_media_flight* out, uint32_t capacity) {
    return guarded(s, [&] { return s->impl.media().flights(out, capacity); }, 0u);
}

void imago_media_set_active(imago_session* s, bool active) {
    guarded(s, [&] { s->impl.mediaSetActive(active); return 0; }, 0);
}

void imago_media_request_thumbnail(imago_session* s, const char* name) {
    if (name == nullptr) {
        return;
    }
    guarded(s, [&] { s->impl.media().requestThumbnail(name); return 0; }, 0);
}

size_t imago_media_copy_thumbnail(imago_session* s, const char* name, uint8_t* out, size_t capacity) {
    if (name == nullptr) {
        return 0;
    }
    return guarded(s, [&] { return s->impl.media().copyThumbnail(name, out, capacity); }, size_t{0});
}

uint32_t imago_media_download(imago_session* s, const char* name, const char* path) {
    if (name == nullptr || path == nullptr) {
        return 0;
    }
    return guarded(s, [&] { return s->impl.mediaDownload(name, path); }, 0u);
}

void imago_media_cancel_downloads(imago_session* s) {
    guarded(s, [&] { s->impl.mediaCancelDownloads(); return 0; }, 0);
}

uint32_t imago_media_delete(imago_session* s, const char* name) {
    if (name == nullptr) {
        return 0;
    }
    return guarded(s, [&] { return s->impl.mediaDelete(name); }, 0u);
}

uint32_t imago_media_set_storage(imago_session* s, uint32_t index) {
    return guarded(s, [&] { return s->impl.mediaSetStorage(index); }, 0u);
}

void imago_session_set_recording_detail(imago_session* s, imago_recording_detail detail) {
    guarded(s, [&] { s->impl.setRecordingDetail(detail == IMAGO_RECORDING_COMPACT); return 0; }, 0);
}

void imago_camera_zoom_target(imago_session* s, float level) {
    guarded(s, [&] { s->impl.setZoomTarget(level); return 0; }, 0);
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

int imago_camera_read_param(imago_session* s, const char* param_id) {
    if (param_id == nullptr) {
        return IMAGO_ERR_INVALID_ARG;
    }
    return guarded(s, [&] { return s->impl.readCameraParam(param_id); }, IMAGO_ERR_STATE);
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

const char* imago_ir_palette_name(int32_t ir_camera_type, uint32_t index) {
    const auto* palette = imago::orus::irPalette(ir_camera_type, index);
    return palette != nullptr ? palette->name.data() : "";
}

size_t imago_ir_palette_colors(int32_t ir_camera_type, uint32_t index, uint32_t* out_rgb,
                               size_t capacity) {
    const auto* palette = imago::orus::irPalette(ir_camera_type, index);
    if (palette == nullptr) {
        return 0;
    }
    if (out_rgb != nullptr) {
        std::memcpy(out_rgb, palette->colours.data(),
                    std::min(capacity, palette->count) * sizeof(uint32_t));
    }
    return palette->count;
}

uint32_t imago_ir_ffc(imago_session* s) {
    return guarded(s, [&] { return s->impl.irFfc(); }, 0u);
}

uint32_t imago_ir_set_ffc_mode(imago_session* s, bool automatic) {
    return guarded(s, [&] { return s->impl.setIrFfcMode(automatic); }, 0u);
}

uint32_t imago_ir_set_isotherm(imago_session* s, bool enabled, float low_c, float high_c) {
    return guarded(s, [&] { return s->impl.setIsotherm(enabled, low_c, high_c); }, 0u);
}

uint32_t imago_stream_set_bitrate(imago_session* s, uint32_t cam_id, uint32_t bits_per_s) {
    return guarded(s, [&] { return s->impl.streamSetting(cam_id, 0, bits_per_s); }, 0u);
}

uint32_t imago_stream_set_resolution(imago_session* s, uint32_t cam_id, uint32_t level) {
    return guarded(s, [&] { return s->impl.streamSetting(cam_id, 1, level); }, 0u);
}

} // extern "C"
