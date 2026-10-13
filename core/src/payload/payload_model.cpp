#include "payload/payload_model.h"

#include "util/text.h"

#include <cmath>
#include <cstring>

namespace imago {

namespace {

constexpr int64_t kStaleNs = 1'500'000'000; // tracking box, detections and range expire

constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;

std::string fixedString(const char* data, size_t capacity) {
    return std::string(data, strnlen(data, capacity));
}

} // namespace

PayloadModel::PayloadModel(int64_t linkTimeoutNs) : linkTimeoutNs_(linkTimeoutNs) {
    state_.gimbal_mode = IMAGO_GIMBAL_MODE_UNKNOWN;
    state_.view_source = -1;
    state_.camera_mode = -1;
    state_.image_count = -1;
}

void PayloadModel::learnIds(const mavlink_message_t& msg) {
    const auto sysid = static_cast<uint8_t>(msg.sysid); // the link drops IDs above 255
    if (msg.compid == orus::kPayloadComponentId) {
        targets_.payloadSystem = sysid;
        state_.payload_system_id = sysid;
    } else if (orus::isCameraComponent(msg.compid)) {
        targets_.cameraSystem = sysid;
        targets_.cameraComponent = msg.compid;
        state_.camera_component_id = msg.compid;
    } else if (orus::isGimbalComponent(msg.compid)) {
        targets_.gimbalSystem = sysid;
        targets_.gimbalComponent = msg.compid;
        state_.gimbal_component_id = msg.compid;
    }
}

void PayloadModel::handle(const mavlink_message_t& msg, int64_t nowNs,
                          std::vector<Notice>& notices) {
    // Herelink and autopilot traffic shares the link; only payload components matter here.
    if (!orus::isPayloadComponent(msg.compid)) {
        return;
    }
    std::lock_guard lock(mutex_);
    learnIds(msg);

    switch (msg.msgid) {
    case MAVLINK_MSG_ID_HEARTBEAT: {
        state_.last_heartbeat_ns = nowNs;
        if (!state_.link_up) {
            state_.link_up = true;
            notices.push_back({Notice::Kind::LinkUp, {}, 0});
        }
        break;
    }
    case MAVLINK_MSG_ID_GIMBAL_DEVICE_ATTITUDE_STATUS: {
        mavlink_gimbal_device_attitude_status_t m;
        mavlink_msg_gimbal_device_attitude_status_decode(&msg, &m);
        float roll = 0, pitch = 0, yaw = 0;
        mavlink_quaternion_to_euler(m.q, &roll, &pitch, &yaw);
        state_.gimbal_valid = true;
        state_.gimbal_roll_deg = roll * kRadToDeg;
        state_.gimbal_pitch_deg = pitch * kRadToDeg;
        state_.gimbal_yaw_deg = yaw * kRadToDeg;
        gimbalFlags_ = m.flags;
        state_.gimbal_mode = orus::gimbalModeFromFlags(m.flags);
        break;
    }
    case MAVLINK_MSG_ID_MOUNT_ORIENTATION: {
        // Older firmware: only used while no GIMBAL_DEVICE_ATTITUDE_STATUS has arrived.
        if (gimbalFlags_ == 0 && !state_.gimbal_valid) {
            mavlink_mount_orientation_t m;
            mavlink_msg_mount_orientation_decode(&msg, &m);
            state_.gimbal_roll_deg = m.roll;
            state_.gimbal_pitch_deg = m.pitch;
            state_.gimbal_yaw_deg = m.yaw;
        }
        break;
    }
    case MAVLINK_MSG_ID_DEBUG: {
        if (msg.compid == orus::kPayloadComponentId) {
            mavlink_debug_t m;
            mavlink_msg_debug_decode(&msg, &m);
            handleDebugValue(m.ind, m.value, nowNs);
        }
        break;
    }
    case MAVLINK_MSG_ID_PARAM_VALUE: {
        if (msg.compid == orus::kPayloadComponentId) {
            mavlink_param_value_t m;
            mavlink_msg_param_value_decode(&msg, &m);
            handleDebugValue(m.param_index, m.param_value, nowNs);
        }
        break;
    }
    case MAVLINK_MSG_ID_V2_EXTENSION: {
        mavlink_v2_extension_t m;
        mavlink_msg_v2_extension_decode(&msg, &m);
        if (auto packet = orus::decodeDetections(m)) {
            state_.detection_frame = packet->frameId;
            state_.detection_count = packet->count;
            std::memcpy(state_.detections, packet->boxes, sizeof(state_.detections));
            lastDetectionNs_ = nowNs;
        }
        break;
    }
    case MAVLINK_MSG_ID_CAMERA_CAPTURE_STATUS: {
        mavlink_camera_capture_status_t m;
        mavlink_msg_camera_capture_status_decode(&msg, &m);
        state_.recording = m.video_status != 0;
        state_.recording_time_ms = m.recording_time_ms;
        state_.image_count = m.image_count;
        break;
    }
    case MAVLINK_MSG_ID_STORAGE_INFORMATION: {
        mavlink_storage_information_t m;
        mavlink_msg_storage_information_decode(&msg, &m);
        state_.storage_valid = m.status == STORAGE_STATUS_READY;
        state_.storage_total_mib = m.total_capacity;
        state_.storage_available_mib = m.available_capacity;
        break;
    }
    case MAVLINK_MSG_ID_CAMERA_SETTINGS: {
        mavlink_camera_settings_t m;
        mavlink_msg_camera_settings_decode(&msg, &m);
        state_.camera_mode = m.mode_id;
        if (std::isfinite(m.zoomLevel) && m.zoomLevel > 0) {
            state_.eo_zoom_level = m.zoomLevel;
        }
        break;
    }
    case MAVLINK_MSG_ID_CAMERA_FOV_STATUS: {
        mavlink_camera_fov_status_t m;
        mavlink_msg_camera_fov_status_decode(&msg, &m);
        if (m.camera_device_id == 0 && std::isfinite(m.hfov)) {
            state_.eo_hfov_deg = m.hfov;
        }
        break;
    }
    case MAVLINK_MSG_ID_DISTANCE_SENSOR: {
        mavlink_distance_sensor_t m;
        mavlink_msg_distance_sensor_decode(&msg, &m);
        state_.lrf_valid = m.current_distance > 0;
        state_.lrf_range_m = static_cast<float>(m.current_distance) / 100.0f;
        lastLrfNs_ = nowNs;
        break;
    }
    case MAVLINK_MSG_ID_VIDEO_STREAM_INFORMATION: {
        mavlink_video_stream_information_t m;
        mavlink_msg_video_stream_information_decode(&msg, &m);
        const std::string uri = fixedString(m.uri, sizeof(m.uri));
        if (!uri.empty()) {
            streamUriKnown_ = true;
            notices.push_back({Notice::Kind::StreamUri, uri, m.stream_id});
        }
        break;
    }
    case MAVLINK_MSG_ID_COMPONENT_INFORMATION_BASIC: {
        mavlink_component_information_basic_t m;
        mavlink_msg_component_information_basic_decode(&msg, &m);
        text::copyTo(state_.model_name, fixedString(m.model_name, sizeof(m.model_name)));
        text::copyTo(state_.software_version,
                     fixedString(m.software_version, sizeof(m.software_version)));
        text::copyTo(state_.serial_number, fixedString(m.serial_number, sizeof(m.serial_number)));
        break;
    }
    case MAVLINK_MSG_ID_STATUSTEXT: {
        mavlink_statustext_t m;
        mavlink_msg_statustext_decode(&msg, &m);
        const std::string chunk = fixedString(m.text, sizeof(m.text));
        if (m.id == 0) {
            notices.push_back({Notice::Kind::StatusText, chunk, m.severity});
            break;
        }
        if (m.id != statusTextId_ || m.chunk_seq == 0) {
            statusTextId_ = m.id;
            statusTextBuffer_.clear();
        }
        statusTextBuffer_ += chunk;
        if (chunk.size() < sizeof(m.text)) { // a short chunk ends the message
            notices.push_back({Notice::Kind::StatusText, statusTextBuffer_, m.severity});
            statusTextBuffer_.clear();
            statusTextId_ = 0;
        }
        break;
    }
    default:
        break;
    }
}

void PayloadModel::handleDebugValue(uint32_t index, float value, int64_t nowNs) {
    using orus::DataIndex;
    switch (static_cast<DataIndex>(index)) {
    case DataIndex::EoZoom:
        state_.eo_zoom_level = value;
        break;
    case DataIndex::IrZoom:
        state_.ir_zoom_level = value;
        break;
    case DataIndex::LrfRange:
        state_.lrf_valid = value > 0;
        state_.lrf_range_m = value;
        lastLrfNs_ = nowNs;
        break;
    case DataIndex::TrackX:
        state_.tracking_box.x = value / orus::kFrameWidth;
        lastTrackBoxNs_ = nowNs;
        break;
    case DataIndex::TrackY:
        state_.tracking_box.y = value / orus::kFrameHeight;
        lastTrackBoxNs_ = nowNs;
        break;
    case DataIndex::TrackW:
        state_.tracking_box.w = value / orus::kFrameWidth;
        lastTrackBoxNs_ = nowNs;
        break;
    case DataIndex::TrackH:
        state_.tracking_box.h = value / orus::kFrameHeight;
        lastTrackBoxNs_ = nowNs;
        break;
    case DataIndex::TrackStatus: {
        const auto status = orus::decodeTrackStatus(value);
        state_.tracking_mode = status.mode;
        state_.tracking_state = status.state;
        break;
    }
    case DataIndex::ViewMode:
        state_.view_source = static_cast<int32_t>(value);
        break;
    case DataIndex::GimbalMode:
        // Only used until the gimbal reports its flags directly.
        if (!state_.gimbal_valid && value >= 0 && value <= 4) {
            state_.gimbal_mode = static_cast<imago_gimbal_mode>(static_cast<int>(value));
        }
        break;
    case DataIndex::IrTempMax:
        state_.ir_temp_max_c = value;
        state_.ir_temp_valid = true;
        break;
    case DataIndex::IrTempMin:
        state_.ir_temp_min_c = value;
        state_.ir_temp_valid = true;
        break;
    case DataIndex::IrTempMean:
        state_.ir_temp_mean_c = value;
        state_.ir_temp_valid = true;
        break;
    default:
        break;
    }
}

void PayloadModel::tick(int64_t nowNs, std::vector<Notice>& notices) {
    std::lock_guard lock(mutex_);
    if (state_.link_up && nowNs - state_.last_heartbeat_ns > linkTimeoutNs_) {
        state_.link_up = false;
        notices.push_back({Notice::Kind::LinkDown, {}, 0});
    }
}

void PayloadModel::snapshot(int64_t nowNs, imago_state& out) const {
    std::lock_guard lock(mutex_);
    // Copy only the payload fields; link counters and video metrics are filled by the session.
    out.link_up = state_.link_up;
    out.last_heartbeat_ns = state_.last_heartbeat_ns;
    out.payload_system_id = state_.payload_system_id;
    out.camera_component_id = state_.camera_component_id;
    out.gimbal_component_id = state_.gimbal_component_id;
    std::memcpy(out.model_name, state_.model_name, sizeof(out.model_name));
    std::memcpy(out.software_version, state_.software_version, sizeof(out.software_version));
    std::memcpy(out.serial_number, state_.serial_number, sizeof(out.serial_number));

    out.gimbal_valid = state_.gimbal_valid;
    out.gimbal_roll_deg = state_.gimbal_roll_deg;
    out.gimbal_pitch_deg = state_.gimbal_pitch_deg;
    out.gimbal_yaw_deg = state_.gimbal_yaw_deg;
    out.gimbal_mode = state_.gimbal_mode;

    out.view_source = state_.view_source;
    out.camera_mode = state_.camera_mode;
    out.eo_zoom_level = state_.eo_zoom_level;
    out.ir_zoom_level = state_.ir_zoom_level;
    out.eo_hfov_deg = state_.eo_hfov_deg;
    out.recording = state_.recording;
    out.recording_time_ms = state_.recording_time_ms;
    out.image_count = state_.image_count;
    out.storage_valid = state_.storage_valid;
    out.storage_total_mib = state_.storage_total_mib;
    out.storage_available_mib = state_.storage_available_mib;

    const bool lrfFresh = nowNs - lastLrfNs_ <= kStaleNs;
    out.lrf_valid = state_.lrf_valid && lrfFresh;
    out.lrf_range_m = state_.lrf_range_m;

    out.tracking_mode = state_.tracking_mode;
    out.tracking_state = state_.tracking_state;
    out.tracking_box_valid = lastTrackBoxNs_ != 0 && nowNs - lastTrackBoxNs_ <= kStaleNs &&
                             state_.tracking_state == IMAGO_TRACKING_TRACKED;
    // The payload reports the box by its top-left corner, like the detections.
    out.tracking_box = state_.tracking_box;

    const bool detectionsFresh = lastDetectionNs_ != 0 && nowNs - lastDetectionNs_ <= kStaleNs;
    out.detection_frame = state_.detection_frame;
    out.detection_count = detectionsFresh ? state_.detection_count : 0;
    std::memcpy(out.detections, state_.detections, sizeof(out.detections));

    out.ir_temp_valid = state_.ir_temp_valid;
    out.ir_temp_max_c = state_.ir_temp_max_c;
    out.ir_temp_min_c = state_.ir_temp_min_c;
    out.ir_temp_mean_c = state_.ir_temp_mean_c;
}

orus::Targets PayloadModel::targets() const {
    std::lock_guard lock(mutex_);
    return targets_;
}

bool PayloadModel::linkUp() const {
    std::lock_guard lock(mutex_);
    return state_.link_up;
}

bool PayloadModel::hasGimbalStatus() const {
    std::lock_guard lock(mutex_);
    return state_.gimbal_valid;
}

uint16_t PayloadModel::gimbalFlags() const {
    std::lock_guard lock(mutex_);
    return gimbalFlags_;
}

int32_t PayloadModel::cameraMode() const {
    std::lock_guard lock(mutex_);
    return state_.camera_mode;
}

void PayloadModel::setCameraMode(int32_t mode) {
    std::lock_guard lock(mutex_);
    state_.camera_mode = mode;
}

bool PayloadModel::hasStreamUri() const {
    std::lock_guard lock(mutex_);
    return streamUriKnown_;
}

} // namespace imago
