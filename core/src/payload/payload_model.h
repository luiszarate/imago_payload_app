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
    bool linkUp() const;
    bool hasGimbalStatus() const;
    uint16_t gimbalFlags() const;
    bool hasStreamUri() const;
    int32_t cameraMode() const;
    // Records a camera mode the payload accepted, before CAMERA_SETTINGS confirms it.
    void setCameraMode(int32_t mode);

  private:
    void handleDebugValue(uint32_t index, float value, int64_t nowNs);
    void handleAutopilotAttitude(const mavlink_message_t& msg, int64_t nowNs);
    void learnIds(const mavlink_message_t& msg);

    const int64_t linkTimeoutNs_;
    mutable std::mutex mutex_;
    imago_state state_{};
    orus::Targets targets_;
    uint16_t gimbalFlags_ = 0;
    // Gimbal yaw as reported, in the frame its flags give (snapshot() derives both frames).
    bool attitudeStatusSeen_ = false;
    float rawYawDeg_ = 0;
    bool yawInEarthFrame_ = false;
    // Vehicle heading sources.
    float gimbalHeadingDeg_ = 0;
    int64_t gimbalHeadingNs_ = 0;
    uint8_t autopilotSystem_ = 0;
    float autopilotHeadingDeg_ = 0;
    int64_t autopilotHeadingNs_ = 0;
    int64_t lastTrackBoxNs_ = 0;
    int64_t lastDetectionNs_ = 0;
    int64_t lastLrfNs_ = 0;
    bool streamUriKnown_ = false;
    // STATUSTEXT chunks being reassembled: id -> text
    uint16_t statusTextId_ = 0;
    std::string statusTextBuffer_;
};

} // namespace imago
