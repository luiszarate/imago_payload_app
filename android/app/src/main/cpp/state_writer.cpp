#include "state_writer.h"

#include <cstring>

namespace imago::android {

namespace {

class Writer {
public:
    Writer(uint8_t* out, size_t capacity) : out_(out), capacity_(capacity) {}

    void i(int64_t value) {
        const auto v = static_cast<uint32_t>(value);
        if (!room(4)) {
            return;
        }
        out_[size_++] = static_cast<uint8_t>(v);
        out_[size_++] = static_cast<uint8_t>(v >> 8);
        out_[size_++] = static_cast<uint8_t>(v >> 16);
        out_[size_++] = static_cast<uint8_t>(v >> 24);
    }

    void b(bool value) { i(value ? 1 : 0); }

    void f(float value) {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof bits);
        i(bits);
    }

    /// A NUL-terminated char array of at most `capacity` bytes.
    void s(const char* text, size_t capacity) {
        const size_t length = strnlen(text, capacity);
        i(static_cast<int64_t>(length));
        if (!room(length)) {
            return;
        }
        std::memcpy(out_ + size_, text, length);
        size_ += length;
    }

    size_t finish() const { return overflow_ ? 0 : size_; }

private:
    bool room(size_t bytes) {
        if (overflow_ || size_ + bytes > capacity_) {
            overflow_ = true;
            return false;
        }
        return true;
    }

    uint8_t* out_;
    size_t capacity_;
    size_t size_ = 0;
    bool overflow_ = false;
};

} // namespace

size_t writeState(const imago_state& st, uint8_t* out, size_t capacity) {
    Writer w(out, capacity);

    // Link
    w.b(st.link_up);
    w.s(st.mavlink_peer, sizeof st.mavlink_peer);
    w.i(st.camera_component_id);
    w.i(st.gimbal_component_id);
    w.i(st.mavlink_rx_messages);
    w.i(st.mavlink_tx_messages);
    w.i(st.mavlink_rx_dropped);
    w.f(st.command_rtt_ms_last);
    w.f(st.command_rtt_ms_avg);
    w.i(st.commands_pending);

    // Identity
    w.s(st.model_name, sizeof st.model_name);
    w.s(st.software_version, sizeof st.software_version);
    w.s(st.serial_number, sizeof st.serial_number);

    // Gimbal
    w.b(st.gimbal_valid);
    w.f(st.gimbal_roll_deg);
    w.f(st.gimbal_pitch_deg);
    w.i(st.gimbal_mode);
    w.i(st.gimbal_control);
    w.i(st.gimbal_control_system);
    w.i(st.gimbal_control_component);
    w.b(st.gimbal_yaw_valid);
    w.f(st.gimbal_yaw_deg);
    w.b(st.gimbal_yaw_north_valid);
    w.f(st.gimbal_yaw_north_deg);
    w.b(st.vehicle_heading_valid);
    w.f(st.vehicle_heading_deg);

    // Camera
    w.i(st.view_source);
    w.i(st.camera_mode);
    w.f(st.eo_zoom_level);
    w.i(st.eo_zoom_mode);
    w.f(st.eo_zoom_max);
    w.f(st.ir_zoom_level);
    w.f(st.eo_hfov_deg);
    w.b(st.recording);
    w.i(st.recording_time_ms);
    w.i(st.image_count);
    w.b(st.storage_valid);
    w.f(st.storage_total_mib);
    w.f(st.storage_available_mib);
    w.b(st.lrf_valid);
    w.f(st.lrf_range_m);

    // Tracking and detections
    w.i(st.tracking_mode);
    w.i(st.tracking_state);
    w.b(st.tracking_box_valid);
    w.f(st.tracking_box.x);
    w.f(st.tracking_box.y);
    w.f(st.tracking_box.w);
    w.f(st.tracking_box.h);
    const int count = st.detection_count < IMAGO_MAX_DETECTIONS ? st.detection_count : IMAGO_MAX_DETECTIONS;
    w.i(count);
    for (int k = 0; k < count; ++k) {
        const imago_detection& d = st.detections[k];
        w.f(d.box.x);
        w.f(d.box.y);
        w.f(d.box.w);
        w.f(d.box.h);
        w.i(d.track_id);
        w.i(d.class_id);
        w.f(d.confidence);
    }

    // IR temperatures
    w.b(st.ir_temp_valid);
    w.f(st.ir_temp_max_c);
    w.f(st.ir_temp_min_c);
    w.f(st.ir_temp_mean_c);

    // Video
    w.b(st.video_active);
    w.i(st.video_codec);
    w.i(st.video_transport);
    w.f(st.video_fps);
    w.f(st.video_bitrate_kbps);
    w.i(st.video_packets_received);
    w.i(st.video_packets_lost);
    w.i(st.video_packets_late);
    w.i(st.video_frames_delivered);
    w.i(st.video_frames_corrupt);
    w.f(st.video_jitter_ms);
    w.f(st.video_frame_transfer_ms);
    w.f(st.video_frame_gap_p95_ms);
    w.f(st.video_frame_gap_max_ms);
    w.f(st.latency_assembly_ms);
    w.f(st.latency_decode_ms);
    w.f(st.latency_present_ms);
    w.f(st.video_lag_ms);
    w.i(st.video_resyncs);

    w.i(kStateEndMarker);
    return w.finish();
}

} // namespace imago::android
