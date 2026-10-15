#pragma once

// Simulated Gremsy Orus L: answers the MAVLink protocol described in docs/protocol/orus-l.md.
// Used by the core's integration tests and as a development stand-in for the real payload.

#include "mavlink/mavlink.h"
#include "platform/socket.h"

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace imago::sim {

class OrusSimulator {
  public:
    struct Options {
        uint16_t port = 14566;            // MAVLink port we listen on (0 = any free port)
        std::string streamUri = "rtsp://127.0.0.1:8554/live";
        bool acknowledgeCommands = true;  // false: never send COMMAND_ACK / PARAM_EXT_ACK
        float headingDeg = 30;            // vehicle heading reported as delta_yaw
        // Push mode (like Herelink): send to this endpoint before anyone talks to us.
        std::optional<net::Endpoint> pushTo;
        bool verbose = false;
        // Also play a PX4-like autopilot (1/1) with a gimbal manager in front of the gimbal:
        // it holds primary control itself at first, and only moves the gimbal for whoever does.
        bool autopilot = false;
        // Acknowledge tracking commands but leave the tracker as it is, like a real Orus L did
        // in the field (2026-10-15).
        bool ignoreTracking = false;
        // The IR camera has Gremsy's F1 gain and isotherm settings (C_T_G, C_T_ISO_*,
        // C_T_RGn_*). false: it neither answers nor accepts them.
        bool isotherm = true;
    };

    struct Received {
        std::vector<mavlink_command_long_t> commands;
        std::vector<mavlink_param_ext_set_t> params;
        std::vector<std::pair<int64_t, mavlink_gimbal_device_set_attitude_t>> gimbal;
        int heartbeats = 0;
        int managerSetpoints = 0; // GIMBAL_MANAGER_SET_ATTITUDE the manager acted on
        int namedValues = 0;      // NAMED_VALUE_FLOAT: the app records these, never sends them
    };

    ~OrusSimulator();

    bool start(const Options& options, std::string* error);
    void stop();
    uint16_t port() const { return socket_.localPort(); }

    Received received() const;
    void clearReceived();

    // Current simulated values, for tests.
    uint32_t param(const std::string& id) const;

    // Another controller (the autopilot on the payload's serial port, a GCS) zooms the camera
    // to `level`; the payload acknowledges that command to (system, component).
    void zoomFromElsewhere(float level, uint8_t system, uint8_t component);
    // Options::ignoreTracking, switchable while running.
    void setIgnoreTracking(bool ignore) { ignoreTracking_.store(ignore); }
    // IR flat-field corrections triggered so far.
    int ffcCount() const { return ffcCount_.load(); }

  private:
    void run();
    // The autopilot's part (Options::autopilot): true if the message was for it.
    bool handleAutopilotMessage(const mavlink_message_t& msg);
    void sendManagerStatus();
    void handle(const mavlink_message_t& msg, const net::Endpoint& from, int64_t nowNs);
    void handleCommand(const mavlink_command_long_t& cmd, int64_t nowNs);
    void handleCustom(const mavlink_command_long_t& cmd);
    void tick(int64_t nowNs);

    void sendHeartbeats();
    void sendAck(uint16_t command, uint8_t result);
    void sendGimbalStatus();
    void sendDebug(uint8_t index, float value);
    void sendDetections();
    void sendCaptureStatus();
    void sendStorage();
    void sendStreamInfo();
    void sendComponentInfo();
    float dataValue(uint8_t index) const;
    float zoomMax() const;
    void moveZoom(float dt);

    template <typename Encode> void send(uint8_t compid, Encode encode);

    Options options_;
    std::atomic<bool> ignoreTracking_{false};
    net::UdpSocket socket_;
    std::thread thread_;
    std::atomic<bool> running_{false};

    mutable std::mutex mutex_;
    Received received_;
    std::map<std::string, uint32_t> params_;

    // Simulation state (simulator thread only, except params_ under mutex_).
    std::optional<net::Endpoint> peer_;
    mavlink_status_t txStatus_{};
    mavlink_status_t rxStatus_{};
    mavlink_message_t rxBuffer_{};
    uint8_t clientSystem_ = 1;
    uint8_t clientComponent_ = 0;
    float pitchDeg_ = 0, yawDeg_ = 0;
    float pitchRate_ = 0, yawRate_ = 0;
    int64_t lastRateNs_ = 0;
    uint16_t gimbalFlags_ = GIMBAL_DEVICE_FLAGS_YAW_LOCK;
    std::atomic<int> ffcMode_{1}; // 0 manual, 1 auto
    std::atomic<int> ffcCount_{0};
    int trackingMode_ = 0;   // 0 stop, 1 active, 2 eagle eye
    int trackingState_ = 0;  // 0 idle, 1 tracking, 2 lost
    float trackX_ = 0, trackY_ = 0, trackW_ = 0, trackH_ = 0;
    bool recording_ = false;
    int64_t recordingStartNs_ = 0;
    int imageCount_ = 0;
    int cameraMode_ = 0;
    float zoomLevel_ = 1.0f;
    int zoomDirection_ = 0;
    float zoomTarget_ = 0; // level the lens travels to, 0 = none
    // zoomFromElsewhere(), applied by the simulator thread: level (0 = none), ACK target.
    std::atomic<float> foreignZoom_{0};
    std::atomic<uint16_t> foreignTarget_{0};
    uint32_t detectionFrame_ = 0;
    std::map<uint32_t, int64_t> intervalsNs_;  // data index or message id -> interval
    std::map<uint32_t, int64_t> nextSendNs_;
    int64_t nextHeartbeatNs_ = 0;
    int64_t nextGimbalNs_ = 0;
    int64_t nextManagerStatusNs_ = 0;
    // Gimbal manager primary control (autopilot option), simulator thread only.
    uint8_t primarySystem_ = 1;
    uint8_t primaryComponent_ = MAV_COMP_ID_AUTOPILOT1;
};

} // namespace imago::sim
