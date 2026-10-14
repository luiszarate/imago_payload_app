#include "payload/payload_model.h"

#include "platform/clock.h"
#include "util/text.h"

#include <cmath>
#include <cstring>

namespace imago {

namespace {
// GIMBAL_MANAGER_STATUS older than this: no manager (or it stopped answering).
constexpr int64_t kManagerStatusTimeoutNs = 5 * kNsPerSec;
} // namespace

namespace {

constexpr int64_t kStaleNs = 1'500'000'000; // tracking box, detections and range expire

constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;

float wrap360(float deg) {
    const float r = std::fmod(deg, 360.0f);
    return r < 0 ? r + 360.0f : r;
}

float wrap180(float deg) { return wrap360(deg + 180.0f) - 180.0f; }

std::string fixedString(const char* data, size_t capacity) {
    return std::string(data, strnlen(data, capacity));
}

} // namespace

PayloadModel::PayloadModel(int64_t linkTimeoutNs) : linkTimeoutNs_(linkTimeoutNs) {
    state_.gimbal_mode = IMAGO_GIMBAL_MODE_UNKNOWN;
    state_.view_source = -1;
    state_.camera_mode = -1;
    state_.eo_zoom_mode = IMAGO_ZOOM_MODE_UNKNOWN;
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
    // Herelink and autopilot traffic shares the link. Only the payload components matter here,
    // plus the autopilot's attitude for the vehicle heading.
    if (msg.compid == MAV_COMP_ID_AUTOPILOT1) {
        if (msg.msgid == MAVLINK_MSG_ID_ATTITUDE) {
            handleAutopilotAttitude(msg, nowNs);
        } else if (msg.msgid == MAVLINK_MSG_ID_HEARTBEAT &&
                   mavlink_msg_heartbeat_get_autopilot(&msg) != MAV_AUTOPILOT_INVALID) {
            // The gimbal manager runs here (PX4).
            std::lock_guard lock(mutex_);
            targets_.autopilotSystem = static_cast<uint8_t>(msg.sysid);
            targets_.autopilotComponent = msg.compid;
        } else if (msg.msgid == MAVLINK_MSG_ID_GIMBAL_MANAGER_STATUS) {
            mavlink_gimbal_manager_status_t m;
            mavlink_msg_gimbal_manager_status_decode(&msg, &m);
            std::lock_guard lock(mutex_);
            state_.gimbal_control_system = m.primary_control_sysid;
            state_.gimbal_control_component = m.primary_control_compid;
            state_.gimbal_control = m.primary_control_sysid == 0 ? IMAGO_GIMBAL_CONTROL_NOBODY
                                                                 : IMAGO_GIMBAL_CONTROL_OTHER;
            lastManagerStatusNs_ = nowNs;
        }
        return;
    }
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
        attitudeStatusSeen_ = true;
        state_.gimbal_valid = true;
        state_.gimbal_roll_deg = roll * kRadToDeg;
        state_.gimbal_pitch_deg = pitch * kRadToDeg;
        rawYawDeg_ = yaw * kRadToDeg;
        yawInEarthFrame_ = orus::yawInEarthFrame(m.flags);
        // delta_yaw is a MAVLink 2 extension: absent (zero-filled) from older senders.
        if (msg.len > MAVLINK_MSG_ID_GIMBAL_DEVICE_ATTITUDE_STATUS_MIN_LEN &&
            std::isfinite(m.delta_yaw)) {
            gimbalHeadingDeg_ = wrap360(m.delta_yaw * kRadToDeg);
            gimbalHeadingNs_ = nowNs;
        }
        gimbalFlags_ = m.flags;
        state_.gimbal_mode = orus::gimbalModeFromFlags(m.flags);
        break;
    }
    case MAVLINK_MSG_ID_MOUNT_ORIENTATION: {
        // Older firmware: only used while no GIMBAL_DEVICE_ATTITUDE_STATUS has arrived.
        if (!attitudeStatusSeen_) {
            mavlink_mount_orientation_t m;
            mavlink_msg_mount_orientation_decode(&msg, &m);
            state_.gimbal_valid = true;
            state_.gimbal_roll_deg = m.roll;
            state_.gimbal_pitch_deg = m.pitch;
            rawYawDeg_ = m.yaw; // relative to the vehicle
            yawInEarthFrame_ = false;
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
    case MAVLINK_MSG_ID_PARAM_EXT_VALUE: {
        mavlink_param_ext_value_t m;
        mavlink_msg_param_ext_value_decode(&msg, &m);
        handleCameraParam(fixedString(m.param_id, sizeof(m.param_id)),
                          orus::paramExtUint(m.param_value));
        break;
    }
    case MAVLINK_MSG_ID_PARAM_EXT_ACK: {
        // A setting the payload accepted, whoever asked for it.
        mavlink_param_ext_ack_t m;
        mavlink_msg_param_ext_ack_decode(&msg, &m);
        if (m.param_result == PARAM_ACK_ACCEPTED) {
            handleCameraParam(fixedString(m.param_id, sizeof(m.param_id)),
                              orus::paramExtUint(m.param_value));
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

void PayloadModel::handleCameraParam(const std::string& id, uint32_t value) {
    if (id == orus::param::kZoomMode) {
        state_.eo_zoom_mode = static_cast<int32_t>(value);
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

void PayloadModel::handleAutopilotAttitude(const mavlink_message_t& msg, int64_t nowNs) {
    const auto sysid = static_cast<uint8_t>(msg.sysid);
    std::lock_guard lock(mutex_);
    // Follow one vehicle: the first one heard, or the payload's own once that is known.
    if (sysid != autopilotSystem_) {
        if (autopilotSystem_ != 0 && sysid != state_.payload_system_id) {
            return;
        }
        autopilotSystem_ = sysid;
    }
    mavlink_attitude_t m;
    mavlink_msg_attitude_decode(&msg, &m);
    if (std::isfinite(m.yaw)) {
        autopilotHeadingDeg_ = wrap360(m.yaw * kRadToDeg);
        autopilotHeadingNs_ = nowNs;
    }
}

void PayloadModel::tick(int64_t nowNs, std::vector<Notice>& notices) {
    std::lock_guard lock(mutex_);
    if (state_.link_up && nowNs - state_.last_heartbeat_ns > linkTimeoutNs_) {
        state_.link_up = false;
        state_.eo_zoom_level = 0; // unknown until the payload reports it again
        notices.push_back({Notice::Kind::LinkDown, {}, 0});
    }
}

void PayloadModel::snapshot(int64_t nowNs, imago_state& out) const {
    std::lock_guard lock(mutex_);
    // Copy only the payload fields; link counters and video metrics are filled by the session.
    out.link_up = state_.link_up;
    out.last_heartbeat_ns = state_.last_heartbeat_ns;
    // The gimbal manager's control, while its status keeps coming (the session marks ours).
    if (lastManagerStatusNs_ != 0 && nowNs - lastManagerStatusNs_ < kManagerStatusTimeoutNs) {
        out.gimbal_control = state_.gimbal_control;
        out.gimbal_control_system = state_.gimbal_control_system;
        out.gimbal_control_component = state_.gimbal_control_component;
    }
    out.payload_system_id = state_.payload_system_id;
    out.camera_component_id = state_.camera_component_id;
    out.gimbal_component_id = state_.gimbal_component_id;
    std::memcpy(out.model_name, state_.model_name, sizeof(out.model_name));
    std::memcpy(out.software_version, state_.software_version, sizeof(out.software_version));
    std::memcpy(out.serial_number, state_.serial_number, sizeof(out.serial_number));

    out.gimbal_valid = state_.gimbal_valid;
    out.gimbal_roll_deg = state_.gimbal_roll_deg;
    out.gimbal_pitch_deg = state_.gimbal_pitch_deg;
    out.gimbal_mode = state_.gimbal_mode;

    out.vehicle_heading_valid = false;
    out.vehicle_heading_deg = 0;
    out.gimbal_yaw_valid = false;
    out.gimbal_yaw_deg = 0;
    out.gimbal_yaw_north_valid = false;
    out.gimbal_yaw_north_deg = 0;
    // Vehicle heading: the gimbal's own delta_yaw keeps its yaw conversions consistent, so it
    // comes first; the autopilot's attitude is the fallback.
    if (gimbalHeadingNs_ != 0 && nowNs - gimbalHeadingNs_ <= kStaleNs) {
        out.vehicle_heading_valid = true;
        out.vehicle_heading_deg = gimbalHeadingDeg_;
    } else if (autopilotHeadingNs_ != 0 && nowNs - autopilotHeadingNs_ <= kStaleNs) {
        out.vehicle_heading_valid = true;
        out.vehicle_heading_deg = autopilotHeadingDeg_;
    }
    if (state_.gimbal_valid) {
        if (yawInEarthFrame_) {
            out.gimbal_yaw_north_valid = true;
            out.gimbal_yaw_north_deg = wrap360(rawYawDeg_);
            if (out.vehicle_heading_valid) {
                out.gimbal_yaw_valid = true;
                out.gimbal_yaw_deg = wrap180(rawYawDeg_ - out.vehicle_heading_deg);
            }
        } else {
            out.gimbal_yaw_valid = true;
            out.gimbal_yaw_deg = wrap180(rawYawDeg_);
            if (out.vehicle_heading_valid) {
                out.gimbal_yaw_north_valid = true;
                out.gimbal_yaw_north_deg = wrap360(out.vehicle_heading_deg + rawYawDeg_);
            }
        }
    }

    out.view_source = state_.view_source;
    out.camera_mode = state_.camera_mode;
    out.eo_zoom_level = state_.eo_zoom_level;
    out.eo_zoom_mode = state_.eo_zoom_mode;
    out.eo_zoom_max = zoomMaxLocked();
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

float PayloadModel::zoomMaxLocked() const {
    const float max = orus::zoomMaxForMode(state_.eo_zoom_mode);
    if (max == 0 && state_.eo_zoom_level > IMAGO_EO_ZOOM_OPTICAL_MAX) {
        return orus::kZoomCombinedMax; // only the combined mode goes past the optical range
    }
    return max;
}

float PayloadModel::zoomMax() const {
    std::lock_guard lock(mutex_);
    return zoomMaxLocked();
}

float PayloadModel::zoomLevel() const {
    std::lock_guard lock(mutex_);
    return state_.eo_zoom_level;
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

bool PayloadModel::zoomModeKnown() const {
    std::lock_guard lock(mutex_);
    return state_.eo_zoom_mode != IMAGO_ZOOM_MODE_UNKNOWN;
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
