// Writes a state snapshot with a distinct value in every field, through the same writeState the
// app uses, for PayloadSnapshotTest on the Kotlin side. Run by tools/make_state_fixture.sh.

#include "../../main/cpp/state_writer.h"

#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s out.bin\n", argv[0]);
        return 2;
    }
    imago_state st;
    std::memset(&st, 0, sizeof st);
    st.link_up = true;
    std::strcpy(st.mavlink_peer, "192.168.144.240:14566");
    st.camera_component_id = 100;
    st.gimbal_component_id = 154;
    st.mavlink_rx_messages = 1001;
    st.mavlink_tx_messages = 1002;
    st.mavlink_rx_dropped = 1003;
    st.command_rtt_ms_last = 1.5f;
    st.command_rtt_ms_avg = 2.5f;
    st.commands_pending = 4;
    std::strcpy(st.model_name, "Orus L");
    std::strcpy(st.software_version, "1.2.3");
    std::strcpy(st.serial_number, "SN9");
    st.gimbal_valid = true;
    st.gimbal_roll_deg = 3.25f;
    st.gimbal_pitch_deg = -45.5f;
    st.gimbal_mode = IMAGO_GIMBAL_MODE_FOLLOW;
    st.gimbal_control = IMAGO_GIMBAL_CONTROL_OTHER;
    st.gimbal_control_system = 255;
    st.gimbal_control_component = 190;
    st.gimbal_yaw_valid = true;
    st.gimbal_yaw_deg = 12.5f;
    st.gimbal_yaw_north_valid = true;
    st.gimbal_yaw_north_deg = 270.25f;
    st.gimbal_pan_valid = true;
    st.gimbal_pan_deg = -302.46f;
    st.gimbal_pan_near_limit = true;
    st.vehicle_heading_valid = true;
    st.vehicle_heading_deg = 90.5f;
    st.view_source = IMAGO_VIEW_IR;
    st.camera_mode = IMAGO_CAMERA_MODE_VIDEO;
    st.eo_zoom_level = 12.3f;
    st.eo_zoom_mode = IMAGO_ZOOM_MODE_SUPER_RESOLUTION;
    st.eo_zoom_max = 25.f;
    st.ir_zoom_level = 2.f;
    st.eo_hfov_deg = 33.3f;
    st.recording = true;
    st.recording_time_ms = 61000;
    st.image_count = 7;
    st.storage_valid = true;
    st.storage_total_mib = 1000.f;
    st.storage_available_mib = 512.f;
    st.lrf_valid = true;
    st.lrf_range_m = 123.4f;
    st.tracking_mode = IMAGO_TRACKING_ACTIVE;
    st.tracking_state = IMAGO_TRACKING_LOST;
    st.tracking_box_valid = true;
    st.tracking_box = imago_rect{0.1f, 0.2f, 0.3f, 0.4f};
    st.detection_count = 2;
    st.detections[0] = imago_detection{imago_rect{0.5f, 0.5f, 0.1f, 0.1f}, 7, 0, 0.9f};
    st.detections[1] = imago_detection{imago_rect{0.2f, 0.3f, 0.05f, 0.06f}, 8, 2, 0.75f};
    st.ir_temp_valid = true;
    st.ir_temp_max_c = 50.5f;
    st.ir_temp_min_c = -5.5f;
    st.ir_temp_mean_c = 20.25f;
    st.ir_camera_type = 3;
    st.ir_palette = 4;
    st.ir_ffc_mode = 1;
    st.target_valid = true;
    st.target_lat_deg = 22.169844;
    st.target_lon_deg = -101.053016;
    st.target_alt_m = 1874.6f;
    st.video_active = true;
    st.video_codec = IMAGO_CODEC_H265;
    st.video_transport = IMAGO_RTSP_TCP;
    st.video_fps = 29.97f;
    st.video_bitrate_kbps = 4000.f;
    st.video_packets_received = 5000;
    st.video_packets_lost = 3;
    st.video_packets_late = 2;
    st.video_frames_delivered = 900;
    st.video_frames_corrupt = 1;
    st.video_jitter_ms = 1.25f;
    st.video_frame_transfer_ms = 15.5f;
    st.video_frame_gap_p95_ms = 34.5f;
    st.video_frame_gap_max_ms = 101.f;
    st.latency_assembly_ms = 0.02f;
    st.latency_decode_ms = 4.5f;
    st.latency_present_ms = 20.5f;
    st.video_lag_ms = 120.f;
    st.video_resyncs = 1;

    unsigned char buffer[imago::android::kStateBufferSize];
    const size_t size = imago::android::writeState(st, buffer, sizeof buffer);
    if (size == 0) {
        std::fprintf(stderr, "buffer too small\n");
        return 1;
    }
    FILE* out = std::fopen(argv[1], "wb");
    if (out == nullptr || std::fwrite(buffer, 1, size, out) != size || std::fclose(out) != 0) {
        std::perror(argv[1]);
        return 1;
    }
    return 0;
}
