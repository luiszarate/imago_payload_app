#include "payload/orus_protocol.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace imago::orus {

namespace {

constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

// Custom command groups (param1 of MAV_CMD_USER_4).
constexpr float kGroupCamera = 2.0f;
constexpr float kGroupPayload = 4.0f;

constexpr uint16_t kDetectionMessageType = 0x4001;
constexpr uint16_t kDetectionMagic = 0x4744;
constexpr uint8_t kDetectionVersion = 1;
constexpr size_t kDetectionHeaderSize = 8;
constexpr size_t kDetectionBoxSize = 12;

uint16_t readU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

uint32_t readU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

mavlink_command_long_t toPayload(const Targets& t, const std::array<float, 7>& params) {
    return commandLong(t.payloadSystem, t.payloadComponent, kCustomCommand, params);
}

mavlink_command_long_t toCamera(const Targets& t, uint16_t command,
                                const std::array<float, 7>& params) {
    return commandLong(t.cameraSystem, t.cameraComponent, command, params);
}

} // namespace

bool isCameraComponent(uint8_t componentId) {
    return componentId >= MAV_COMP_ID_CAMERA && componentId <= MAV_COMP_ID_CAMERA6;
}

bool isGimbalComponent(uint8_t componentId) {
    return componentId == MAV_COMP_ID_GIMBAL ||
           (componentId >= MAV_COMP_ID_GIMBAL2 && componentId <= MAV_COMP_ID_GIMBAL6);
}

bool isPayloadComponent(uint8_t componentId) {
    return componentId == kPayloadComponentId || isCameraComponent(componentId) ||
           isGimbalComponent(componentId);
}

mavlink_heartbeat_t heartbeat() {
    mavlink_heartbeat_t m{};
    m.type = MAV_TYPE_ONBOARD_CONTROLLER;
    m.autopilot = MAV_AUTOPILOT_INVALID;
    m.base_mode = 0;
    m.custom_mode = 0;
    m.system_status = MAV_STATE_ACTIVE;
    m.mavlink_version = 3;
    return m;
}

mavlink_command_long_t commandLong(uint8_t system, uint8_t component, uint16_t command,
                                   const std::array<float, 7>& params) {
    mavlink_command_long_t m{};
    m.target_system = system;
    m.target_component = component;
    m.command = command;
    m.confirmation = 0;
    m.param1 = params[0];
    m.param2 = params[1];
    m.param3 = params[2];
    m.param4 = params[3];
    m.param5 = params[4];
    m.param6 = params[5];
    m.param7 = params[6];
    return m;
}

mavlink_param_ext_set_t setCameraParam(const Targets& t, std::string_view id, uint32_t value) {
    mavlink_param_ext_set_t m{};
    m.target_system = t.cameraSystem;
    m.target_component = t.cameraComponent;
    std::memcpy(m.param_id, id.data(), std::min(id.size(), sizeof(m.param_id)));
    // The Orus L reads the value as raw little-endian bytes of a uint32.
    const uint8_t bytes[4] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
                              static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
    std::memcpy(m.param_value, bytes, sizeof(bytes));
    m.param_type = MAV_PARAM_EXT_TYPE_UINT32;
    return m;
}

mavlink_param_ext_request_list_t requestCameraParams(const Targets& t) {
    mavlink_param_ext_request_list_t m{};
    m.target_system = t.cameraSystem;
    m.target_component = t.cameraComponent;
    return m;
}

mavlink_command_long_t takePhoto(const Targets& t) {
    return toCamera(t, MAV_CMD_IMAGE_START_CAPTURE, {0, 0, 1, 0, 0, 0, 0});
}

mavlink_command_long_t startRecording(const Targets& t) {
    return toCamera(t, MAV_CMD_VIDEO_START_CAPTURE, {0, 0, 0, 0, 0, 0, 0});
}

mavlink_command_long_t stopRecording(const Targets& t) {
    return toCamera(t, MAV_CMD_VIDEO_STOP_CAPTURE, {0, 0, 0, 0, 0, 0, 0});
}

mavlink_command_long_t zoomContinuous(const Targets& t, int direction) {
    const float value = direction > 0 ? 1.0f : (direction < 0 ? -1.0f : 0.0f);
    return toCamera(t, MAV_CMD_SET_CAMERA_ZOOM,
                    {static_cast<float>(ZOOM_TYPE_CONTINUOUS), value, 0, 0, 0, 0, 0});
}

mavlink_command_long_t zoomToLevel(const Targets& t, float level) {
    // Custom "zoom to position": group camera, function 0, sub-function 4, p4 = level.
    return toPayload(t, {kGroupCamera, 0.0f, 4.0f, level, 0, 0, 0});
}

mavlink_command_long_t focusContinuous(const Targets& t, int direction) {
    const float value = direction > 0 ? 1.0f : (direction < 0 ? -1.0f : 0.0f);
    return toCamera(t, MAV_CMD_SET_CAMERA_FOCUS,
                    {static_cast<float>(FOCUS_TYPE_CONTINUOUS), value, 0, 0, 0, 0, 0});
}

mavlink_command_long_t autoFocus(const Targets& t) {
    return toCamera(t, MAV_CMD_SET_CAMERA_FOCUS,
                    {static_cast<float>(FOCUS_TYPE_AUTO), 0, 0, 0, 0, 0, 0});
}

mavlink_command_long_t trackingMode(const Targets& t, imago_tracking_mode mode) {
    return toPayload(t, {kGroupPayload, 0.0f, 0.0f, static_cast<float>(mode), 0, 0, 0});
}

mavlink_command_long_t trackingPosition(const Targets& t, float x, float y, float w, float h) {
    return toPayload(t, {kGroupPayload, 0.0f, 1.0f, x, y, w, h});
}

mavlink_command_long_t streamSetting(const Targets& t, uint32_t cameraId, uint32_t setting,
                                     uint32_t value) {
    return toPayload(t, {kGroupPayload, 2.0f, static_cast<float>(cameraId),
                         static_cast<float>(setting), static_cast<float>(value), 0, 0});
}

mavlink_command_long_t requestVideoStreamInformation(const Targets& t) {
    return toCamera(t, MAV_CMD_REQUEST_VIDEO_STREAM_INFORMATION, {0, 0, 0, 0, 0, 0, 0});
}

mavlink_command_long_t requestComponentInformation(const Targets& t) {
    return commandLong(t.payloadSystem, t.payloadComponent, MAV_CMD_REQUEST_MESSAGE,
                       {static_cast<float>(MAVLINK_MSG_ID_COMPONENT_INFORMATION_BASIC), 0, 0, 0, 0,
                        0, 0});
}

mavlink_command_long_t requestStorageInformation(const Targets& t) {
    return toCamera(t, MAV_CMD_REQUEST_STORAGE_INFORMATION, {0, 0, 0, 0, 0, 0, 0});
}

mavlink_command_long_t requestCaptureStatus(const Targets& t) {
    return toCamera(t, MAV_CMD_REQUEST_CAMERA_CAPTURE_STATUS, {0, 0, 0, 0, 0, 0, 0});
}

mavlink_command_long_t requestFieldOfView(const Targets& t, uint8_t cameraId) {
    return toCamera(t, MAV_CMD_REQUEST_MESSAGE,
                    {static_cast<float>(MAVLINK_MSG_ID_CAMERA_FOV_STATUS),
                     static_cast<float>(cameraId), 0, 0, 0, 0, 0});
}

mavlink_command_long_t setDataInterval(const Targets& t, uint32_t indexOrMessageId,
                                       uint32_t intervalMs) {
    // p7 = 1: the payload answers to the requester.
    return commandLong(t.payloadSystem, t.payloadComponent, MAV_CMD_SET_MESSAGE_INTERVAL,
                       {static_cast<float>(indexOrMessageId),
                        static_cast<float>(intervalMs) * 1000.0f, 0, 0, 0, 0, 1.0f});
}

mavlink_gimbal_device_set_attitude_t gimbalRate(const Targets& t, uint16_t flags, float pitchDps,
                                                float yawDps) {
    mavlink_gimbal_device_set_attitude_t m{};
    m.target_system = t.gimbalSystem;
    m.target_component = t.gimbalComponent;
    m.flags = flags;
    m.q[0] = m.q[1] = m.q[2] = m.q[3] = kNaN;
    m.angular_velocity_x = 0.0f; // roll
    m.angular_velocity_y = pitchDps * kDegToRad;
    m.angular_velocity_z = yawDps * kDegToRad;
    return m;
}

mavlink_gimbal_device_set_attitude_t gimbalAngle(const Targets& t, uint16_t flags, float pitchDeg,
                                                 float yawDeg) {
    mavlink_gimbal_device_set_attitude_t m{};
    m.target_system = t.gimbalSystem;
    m.target_component = t.gimbalComponent;
    m.flags = flags;
    mavlink_euler_to_quaternion(0.0f, std::clamp(pitchDeg, -90.0f, 90.0f) * kDegToRad,
                                std::clamp(yawDeg, -180.0f, 180.0f) * kDegToRad, m.q);
    m.angular_velocity_x = m.angular_velocity_y = m.angular_velocity_z = kNaN;
    return m;
}

mavlink_command_long_t mountControl(const Targets& t, float pitch, float yaw, bool rate) {
    // p6: input mode, 1 angle / 2 rate (Gremsy convention); p7: MAVLink targeting.
    return commandLong(t.gimbalSystem, t.gimbalComponent, MAV_CMD_DO_MOUNT_CONTROL,
                       {pitch, 0.0f, yaw, 0, 0, rate ? 2.0f : 1.0f,
                        static_cast<float>(MAV_MOUNT_MODE_MAVLINK_TARGETING)});
}

imago_gimbal_mode gimbalModeFromFlags(uint16_t flags) {
    if ((flags & GIMBAL_DEVICE_FLAGS_YAW_LOCK) != 0) {
        return IMAGO_GIMBAL_MODE_LOCK;
    }
    if ((flags & GIMBAL_DEVICE_FLAGS_RETRACT) != 0) {
        return IMAGO_GIMBAL_MODE_OFF;
    }
    if ((flags & GIMBAL_DEVICE_FLAGS_NEUTRAL) != 0) {
        return IMAGO_GIMBAL_MODE_RESET;
    }
    if ((flags & 0x4000) != 0) {
        return IMAGO_GIMBAL_MODE_MAPPING;
    }
    return IMAGO_GIMBAL_MODE_FOLLOW;
}

bool yawInEarthFrame(uint16_t flags) {
    if ((flags & GIMBAL_DEVICE_FLAGS_YAW_IN_EARTH_FRAME) != 0) {
        return true;
    }
    if ((flags & GIMBAL_DEVICE_FLAGS_YAW_IN_VEHICLE_FRAME) != 0) {
        return false;
    }
    return (flags & GIMBAL_DEVICE_FLAGS_YAW_LOCK) != 0;
}

TrackStatus decodeTrackStatus(float raw) {
    const auto value = static_cast<uint32_t>(std::max(raw, 0.0f));
    const uint32_t mode = (value >> 8) & 0xFF;
    const uint32_t state = value & 0xFF;
    return TrackStatus{
        mode <= 2 ? static_cast<imago_tracking_mode>(mode) : IMAGO_TRACKING_STOP,
        state <= 2 ? static_cast<imago_tracking_state>(state) : IMAGO_TRACKING_IDLE,
    };
}

std::optional<DetectionPacket> decodeDetections(const mavlink_v2_extension_t& ext) {
    if (ext.message_type != kDetectionMessageType) {
        return std::nullopt;
    }
    const uint8_t* p = ext.payload;
    if (readU16(p) != kDetectionMagic || p[2] != kDetectionVersion) {
        return std::nullopt;
    }
    DetectionPacket packet;
    const size_t maxBoxes = (sizeof(ext.payload) - kDetectionHeaderSize) / kDetectionBoxSize;
    packet.count = static_cast<uint8_t>(
        std::min<size_t>({p[3], static_cast<size_t>(IMAGO_MAX_DETECTIONS), maxBoxes}));
    packet.frameId = readU32(p + 4);
    for (uint8_t i = 0; i < packet.count; ++i) {
        const uint8_t* b = p + kDetectionHeaderSize + i * kDetectionBoxSize;
        imago_detection& d = packet.boxes[i];
        d.box.x = static_cast<float>(readU16(b)) / kFrameWidth;
        d.box.y = static_cast<float>(readU16(b + 2)) / kFrameHeight;
        d.box.w = static_cast<float>(readU16(b + 4)) / kFrameWidth;
        d.box.h = static_cast<float>(readU16(b + 6)) / kFrameHeight;
        d.track_id = readU16(b + 8);
        d.class_id = b[10];
        d.confidence = static_cast<float>(b[11]) / 255.0f;
    }
    return packet;
}

} // namespace imago::orus
