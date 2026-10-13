#pragma once

// Gremsy Orus L wire protocol. See docs/protocol/orus-l.md for the source of every value.
// Functions here are pure: they build MAVLink structs or decode payloads, they never send.

#include "imago/imago_core.h"
#include "mavlink/mavlink.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace imago::orus {

constexpr uint16_t kDefaultPort = 14566;
constexpr uint8_t kPayloadComponentId = MAV_COMP_ID_USER2; // 26
constexpr uint16_t kCustomCommand = MAV_CMD_USER_4;        // 31013

// Coordinate space for tracking and detection boxes on the wire.
constexpr float kFrameWidth = 1920.0f;
constexpr float kFrameHeight = 1080.0f;
// Box the payload uses when the caller does not choose a size.
constexpr float kDefaultTrackBoxPx = 128.0f;

// Camera settings written with PARAM_EXT_SET (uint32 values).
namespace param {
inline constexpr std::string_view kViewSource = "C_SOURCE";
inline constexpr std::string_view kRecordSource = "C_V_REC";
inline constexpr std::string_view kTrackerFunction = "TRACK_MODE";
inline constexpr std::string_view kOsdMode = "OSD_MODE";
inline constexpr std::string_view kGimbalMode = "GB_MODE";
inline constexpr std::string_view kLrfMode = "LRF_MODE";
inline constexpr std::string_view kIrPalette = "C_T_PALETTE";
} // namespace param

// Values the payload manager streams as DEBUG(ind, value) once a rate is requested.
enum class DataIndex : uint8_t {
    EoZoom = 0,
    IrZoom = 1,
    LrfRange = 2,
    TrackX = 3,
    TrackY = 4,
    TrackW = 5,
    TrackH = 6,
    TrackStatus = 7,
    LrfOffsetX = 8,
    LrfOffsetY = 9,
    TargetLon = 10,
    TargetLat = 11,
    TargetAlt = 12,
    PayloadLon = 13,
    PayloadLat = 14,
    PayloadAlt = 15,
    AppVersionX = 16,
    AppVersionY = 17,
    AppVersionZ = 18,
    ViewMode = 19,
    RecordSource = 20,
    IrType = 21,
    IrPalette = 22,
    IrFfcMode = 23,
    GimbalMode = 24,
    IrTempMax = 25,
    IrTempMin = 26,
    IrTempMean = 27,
    Count = 28,
};

// Where commands go. IDs are learned from the payload's own messages.
struct Targets {
    uint8_t payloadSystem = 1;
    uint8_t payloadComponent = kPayloadComponentId;
    uint8_t cameraSystem = 1;
    uint8_t cameraComponent = MAV_COMP_ID_CAMERA2; // 101
    uint8_t gimbalSystem = 1;
    uint8_t gimbalComponent = MAV_COMP_ID_GIMBAL; // 154
};

bool isCameraComponent(uint8_t componentId);
bool isGimbalComponent(uint8_t componentId);
// Payload manager, camera or gimbal: every component that belongs to the Orus L.
bool isPayloadComponent(uint8_t componentId);

// --- Builders ---------------------------------------------------------------------------------

mavlink_heartbeat_t heartbeat();

mavlink_command_long_t commandLong(uint8_t system, uint8_t component, uint16_t command,
                                   const std::array<float, 7>& params);

// PARAM_EXT_SET with a uint32 value, as the Orus L expects for every camera setting.
mavlink_param_ext_set_t setCameraParam(const Targets& t, std::string_view id, uint32_t value);
mavlink_param_ext_request_list_t requestCameraParams(const Targets& t);

mavlink_command_long_t takePhoto(const Targets& t);
mavlink_command_long_t startRecording(const Targets& t);
mavlink_command_long_t stopRecording(const Targets& t);
mavlink_command_long_t zoomContinuous(const Targets& t, int direction);
mavlink_command_long_t zoomToLevel(const Targets& t, float level);
mavlink_command_long_t focusContinuous(const Targets& t, int direction);
mavlink_command_long_t autoFocus(const Targets& t);

mavlink_command_long_t trackingMode(const Targets& t, imago_tracking_mode mode);
// Box in wire pixels (1920 x 1080), (x, y) = centre point.
mavlink_command_long_t trackingPosition(const Targets& t, float x, float y, float w, float h);

mavlink_command_long_t streamSetting(const Targets& t, uint32_t cameraId, uint32_t setting,
                                     uint32_t value);

mavlink_command_long_t requestVideoStreamInformation(const Targets& t);
mavlink_command_long_t requestComponentInformation(const Targets& t);
mavlink_command_long_t requestStorageInformation(const Targets& t);
mavlink_command_long_t requestCaptureStatus(const Targets& t);
mavlink_command_long_t requestFieldOfView(const Targets& t, uint8_t cameraId);
// Rate for one DataIndex value (or a real message ID such as MAVLINK_MSG_ID_V2_EXTENSION).
// intervalMs = 0 stops it.
mavlink_command_long_t setDataInterval(const Targets& t, uint32_t indexOrMessageId,
                                       uint32_t intervalMs);

// flags must be the latest GIMBAL_DEVICE_ATTITUDE_STATUS flags so the gimbal keeps its mode.
mavlink_gimbal_device_set_attitude_t gimbalRate(const Targets& t, uint16_t flags, float pitchDps,
                                                float yawDps);
mavlink_gimbal_device_set_attitude_t gimbalAngle(const Targets& t, uint16_t flags, float pitchDeg,
                                                 float yawDeg);

// MAV_CMD_DO_MOUNT_CONTROL to the gimbal. rate = true: values are deg/s, else degrees.
mavlink_command_long_t mountControl(const Targets& t, float pitch, float yaw, bool rate);

// --- Decoders ---------------------------------------------------------------------------------

imago_gimbal_mode gimbalModeFromFlags(uint16_t flags);

struct TrackStatus {
    imago_tracking_mode mode;
    imago_tracking_state state;
};
TrackStatus decodeTrackStatus(float raw);

struct DetectionPacket {
    uint32_t frameId = 0;
    uint8_t count = 0;
    imago_detection boxes[IMAGO_MAX_DETECTIONS] = {};
};

// Decodes a V2_EXTENSION detection payload. Boxes are converted to normalized coordinates.
// Returns nullopt for other message types, a bad magic or an unknown version.
std::optional<DetectionPacket> decodeDetections(const mavlink_v2_extension_t& ext);

} // namespace imago::orus
