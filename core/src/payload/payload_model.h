#pragma once

#include "imago/imago_core.h"
#include "mavlink/mavlink.h"
#include "payload/orus_protocol.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace imago {

// Payload state built from incoming MAVLink. handle() runs on the network thread; snapshot()
// and the accessors may be called from any thread.
class PayloadModel {
  public:
    struct Notice {
        enum class Kind { LinkUp, LinkDown, StreamUri, StatusText } kind;
        std::string text;
        uint32_t code = 0;
    };

    explicit PayloadModel(int64_t linkTimeoutNs);

    // Updates the state from one message. Discrete changes are appended to notices.
    void handle(const mavlink_message_t& msg, int64_t nowNs, std::vector<Notice>& notices);
    // Detects heartbeat timeouts.
    void tick(int64_t nowNs, std::vector<Notice>& notices);

    // Copies the payload part of the state (link, gimbal, camera, tracking, detections).
    void snapshot(int64_t nowNs, imago_state& out) const;

    orus::Targets targets() const;
    // A payload component (manager, camera, gimbal) of the payload's own system. Herelink's
    // camera component (42/100) has a camera's ID but is not the Orus L.
    bool isPayloadSender(const mavlink_message_t& msg) const;
    bool linkUp() const;
    bool hasGimbalStatus() const;
    uint16_t gimbalFlags() const;
    bool hasStreamUri() const;
    int32_t cameraMode() const;
    bool zoomModeKnown() const;
    // Top of the EO zoom range in the current mode, 0 unknown.
    float zoomMax() const;
    // EO zoom level the payload last reported, 0 unknown.
    float zoomLevel() const;
    // How far the gimbal's absolute yaw (the frame it takes angle setpoints in) is from its yaw
    // to the nose, from MOUNT_ORIENTATION: send nose yaw + this. false: none recently.
    // absoluteDeg: yaw_absolute itself, the gimbal's yaw over the ground.
    bool angleYawOffset(int64_t nowNs, float& deg, float* absoluteDeg = nullptr) const;
    // Records a camera mode the payload accepted, before CAMERA_SETTINGS confirms it.
    void setCameraMode(int32_t mode);
    int32_t trackingState() const;
    // Centre of the tracking box the payload last reported (normalized), if it is fresh.
    bool trackingBoxCentre(int64_t nowNs, float& x, float& y) const;

  private:
    void handleDebugValue(uint32_t index, float value, int64_t nowNs);
    void handleCameraParam(const std::string& id, uint32_t value);
    float zoomMaxLocked() const;
    void handleAutopilotTelemetry(const mavlink_message_t& msg, int64_t nowNs);
    void learnIds(const mavlink_message_t& msg);
    bool isPayloadSenderLocked(const mavlink_message_t& msg) const;
    // Follows the yaw against the nose across +-180 (see imago_state.gimbal_pan_deg).
    void trackPan(int64_t nowNs);

    const int64_t linkTimeoutNs_;
    mutable std::mutex mutex_;
    imago_state state_{};
    orus::Targets targets_;
    // The payload manager's heartbeat names the payload's system; until then every system's
    // payload components count.
    bool payloadSystemKnown_ = false;
    uint16_t gimbalFlags_ = 0;
    // Gimbal yaw as reported, in the frame its flags give (snapshot() derives both frames).
    bool attitudeStatusSeen_ = false;
    float rawYawDeg_ = 0;
    bool yawInEarthFrame_ = false;
    bool panKnown_ = false;
    float panDeg_ = 0;
    float lastBodyYawDeg_ = 0;
    int64_t lastPanNs_ = 0;
    bool panNearLimit_ = false;
    // Vehicle heading sources.
    float gimbalHeadingDeg_ = 0;
    int64_t gimbalHeadingNs_ = 0;
    uint8_t autopilotSystem_ = 0;
    int64_t lastManagerStatusNs_ = 0; // GIMBAL_MANAGER_STATUS
    float autopilotHeadingDeg_ = 0;
    int64_t autopilotHeadingNs_ = 0;
    // The aircraft's telemetry (same vehicle as the heading), with when each arrived.
    float vehicleAltitudeM_ = 0;
    int64_t vehicleAltitudeNs_ = 0;
    float vehicleGroundspeed_ = 0, vehicleAirspeed_ = 0;
    int64_t vehicleSpeedNs_ = 0;
    float vehicleBatteryV_ = 0;
    int32_t vehicleBatteryPercent_ = -1;
    int64_t vehicleBatteryNs_ = 0;
    int64_t lastTrackBoxNs_ = 0;
    int64_t lastDetectionNs_ = 0;
    int64_t lastLrfNs_ = 0;
    int64_t lastTargetNs_ = 0;
    bool streamUriKnown_ = false;
    float angleYawOffsetDeg_ = 0;
    float angleYawAbsoluteDeg_ = 0;
    int64_t angleYawOffsetNs_ = 0;
    static constexpr int64_t kAngleYawOffsetFreshNs = 2'000'000'000;
    // STATUSTEXT chunks being reassembled: id -> text
    uint16_t statusTextId_ = 0;
    std::string statusTextBuffer_;
};

} // namespace imago
