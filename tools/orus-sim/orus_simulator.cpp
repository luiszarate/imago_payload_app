#include "orus_simulator.h"

#include "payload/orus_protocol.h"
#include "platform/clock.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace imago::sim {

namespace {

constexpr uint8_t kSystem = 1;
constexpr uint8_t kPayload = MAV_COMP_ID_USER2;
constexpr uint8_t kCamera = MAV_COMP_ID_CAMERA2;
constexpr uint8_t kGimbal = MAV_COMP_ID_GIMBAL;
constexpr int64_t kGimbalPeriodNs = 20 * kNsPerMs; // 50 Hz attitude
constexpr float kRadToDeg = 57.29578f;

std::string paramId(const char* raw) { return std::string(raw, strnlen(raw, 16)); }

} // namespace

OrusSimulator::~OrusSimulator() { stop(); }

bool OrusSimulator::start(const Options& options, std::string* error) {
    options_ = options;
    ignoreTracking_.store(options.ignoreTracking);
    if (!socket_.open(options.port, 256 * 1024, net::TrafficClass::Control, error)) {
        return false;
    }
    peer_ = options.pushTo;
    params_ = {{"C_SOURCE", 1},     {"GB_MODE", 1},      {"TRACK_MODE", 0},   {"OSD_MODE", 0},
               {"LRF_MODE", 3},     {"C_T_PALETTE", 0},  {"RC_MODE", 0},      {"C_V_ZM_MODE", 0},
               {"C_V_Z_SPD", 4},    {"C_V_STZOOM", 1},   {"C_V_FM", 1},       {"C_V_F_SPD", 4},
               {"C_V_DEFOG", 3},    {"C_V_DEFOG_LV", 1}, {"C_V_FLIP", 3},     {"C_V_EIS", 3},
               {"C_V_EIS_LV", 2},   {"C_V_NSREDUCT", 2}, {"C_V_FLREDUCT", 3}, {"C_V_ICR", 2},
               {"C_V_ICR_MAN", 3},  {"C_F_DEFOG", 0},    {"C_T_ZOOM", 0}};
    if (options.isotherm) {
        // F1 thermal core: gain and isotherm settings (all regions off).
        params_.insert({{"C_T_G", 0}, {"C_T_ISO_U", 1}, {"C_T_ISO_M", 0}});
        for (int region = 0; region < orus::kIsothermRegions; ++region) {
            for (const char* field : {"CLR_M", "TMP", "CLR_MN", "CLR_MX"}) {
                params_[orus::isothermParam(region, field)] = 0;
            }
        }
    }
    running_ = true;
    thread_ = std::thread([this] { run(); });
    return true;
}

void OrusSimulator::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
    socket_.close();
}

OrusSimulator::Received OrusSimulator::received() const {
    std::lock_guard lock(mutex_);
    return received_;
}

void OrusSimulator::clearReceived() {
    std::lock_guard lock(mutex_);
    received_ = Received{};
}

uint32_t OrusSimulator::param(const std::string& id) const {
    std::lock_guard lock(mutex_);
    const auto it = params_.find(id);
    return it == params_.end() ? 0 : it->second;
}

template <typename Encode> void OrusSimulator::send(uint8_t compid, Encode encode) {
    if (!peer_) {
        return;
    }
    mavlink_message_t msg;
    encode(msg, compid);
    uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
    const uint16_t len = mavlink_msg_to_send_buffer(buffer, &msg);
    socket_.send(*peer_, buffer, len);
}

void OrusSimulator::run() {
    uint8_t datagram[2048];
    while (running_) {
        const net::SocketHandle fd = socket_.fd();
        bool readable = false;
        net::waitReadable({&fd, 1}, 5, {&readable, 1});
        const int64_t now = monotonicNs();
        if (readable) {
            while (true) {
                net::Endpoint from;
                const long n = socket_.receive(datagram, sizeof(datagram), &from);
                if (n <= 0) {
                    break;
                }
                for (long i = 0; i < n; ++i) {
                    mavlink_message_t msg;
                    mavlink_status_t status;
                    if (mavlink_frame_char_buffer(&rxBuffer_, &rxStatus_, datagram[i], &msg,
                                                  &status) == MAVLINK_FRAMING_OK) {
                        handle(msg, from, now);
                    }
                }
            }
        }
        tick(now);
    }
}

void OrusSimulator::handle(const mavlink_message_t& msg, const net::Endpoint& from, int64_t nowNs) {
    peer_ = from; // answer whoever talks to us, like the real payload
    clientSystem_ = static_cast<uint8_t>(msg.sysid);
    clientComponent_ = msg.compid;

    switch (msg.msgid) {
    case MAVLINK_MSG_ID_HEARTBEAT: {
        std::lock_guard lock(mutex_);
        ++received_.heartbeats;
        break;
    }
    case MAVLINK_MSG_ID_NAMED_VALUE_FLOAT: {
        std::lock_guard lock(mutex_);
        ++received_.namedValues;
        break;
    }
    case MAVLINK_MSG_ID_COMMAND_LONG: {
        mavlink_command_long_t cmd;
        mavlink_msg_command_long_decode(&msg, &cmd);
        {
            std::lock_guard lock(mutex_);
            received_.commands.push_back(cmd);
        }
        if (handleAutopilotMessage(msg)) {
            break;
        }
        handleCommand(cmd, nowNs);
        break;
    }
    case MAVLINK_MSG_ID_PARAM_EXT_SET: {
        mavlink_param_ext_set_t set;
        mavlink_msg_param_ext_set_decode(&msg, &set);
        uint32_t value = 0;
        std::memcpy(&value, set.param_value, sizeof(value));
        const std::string id = paramId(set.param_id);
        // Without the F1 core's settings the camera rejects them, as an unknown setting.
        const bool known = options_.isotherm ||
                           (id.rfind("C_T_ISO", 0) != 0 && id.rfind("C_T_RG", 0) != 0 && id != "C_T_G");
        {
            std::lock_guard lock(mutex_);
            received_.params.push_back(set);
            if (known) {
                params_[id] = value;
            }
        }
        if (id == "C_V_ZM_CB_LV" || id == "C_V_ZM_SR_LV") {
            // The lens travels to that entry of the level table over the next seconds (tick).
            const auto levels = orus::zoomLevels(id == "C_V_ZM_SR_LV" ? IMAGO_ZOOM_MODE_SUPER_RESOLUTION
                                                                      : IMAGO_ZOOM_MODE_COMBINED);
            if (value < levels.size()) {
                zoomTarget_ = std::min(levels[value], zoomMax());
                zoomDirection_ = 0;
            }
        }
        if (id == "GB_MODE") {
            static constexpr uint16_t kFlags[] = {GIMBAL_DEVICE_FLAGS_RETRACT,
                                                  GIMBAL_DEVICE_FLAGS_YAW_LOCK, 0, 0x4000,
                                                  GIMBAL_DEVICE_FLAGS_NEUTRAL};
            if (value <= 4) {
                gimbalFlags_ = kFlags[value];
            }
            if (value == 4) {
                pitchDeg_ = 0;
                yawDeg_ = 0;
            }
        }
        if (options_.acknowledgeCommands) {
            send(kCamera, [&](mavlink_message_t& m, uint8_t c) {
                mavlink_param_ext_ack_t ack{};
                std::memcpy(ack.param_id, set.param_id, sizeof(ack.param_id));
                std::memcpy(ack.param_value, set.param_value, sizeof(ack.param_value));
                ack.param_type = set.param_type;
                ack.param_result = known ? PARAM_ACK_ACCEPTED : PARAM_ACK_FAILED;
                mavlink_msg_param_ext_ack_encode_status(kSystem, c, &txStatus_, &m, &ack);
            });
        }
        break;
    }
    case MAVLINK_MSG_ID_PARAM_EXT_REQUEST_READ: {
        mavlink_param_ext_request_read_t req;
        mavlink_msg_param_ext_request_read_decode(&msg, &req);
        const std::string id = paramId(req.param_id);
        uint32_t value = 0;
        uint16_t count = 0;
        {
            std::lock_guard lock(mutex_);
            const auto it = params_.find(id);
            if (it == params_.end()) {
                break;
            }
            value = it->second;
            count = static_cast<uint16_t>(params_.size());
        }
        send(kCamera, [&](mavlink_message_t& m, uint8_t c) {
            mavlink_param_ext_value_t v{};
            std::memcpy(v.param_id, req.param_id, sizeof(v.param_id));
            std::memcpy(v.param_value, &value, sizeof(value));
            v.param_type = MAV_PARAM_EXT_TYPE_UINT32;
            v.param_count = count;
            mavlink_msg_param_ext_value_encode_status(kSystem, c, &txStatus_, &m, &v);
        });
        break;
    }
    case MAVLINK_MSG_ID_GIMBAL_MANAGER_SET_ATTITUDE:
        handleAutopilotMessage(msg);
        break;
    case MAVLINK_MSG_ID_GIMBAL_DEVICE_SET_ATTITUDE: {
        mavlink_gimbal_device_set_attitude_t att;
        mavlink_msg_gimbal_device_set_attitude_decode(&msg, &att);
        {
            std::lock_guard lock(mutex_);
            received_.gimbal.emplace_back(nowNs, att);
        }
        if (std::isnan(att.q[0])) {
            pitchRate_ = std::isfinite(att.angular_velocity_y) ? att.angular_velocity_y * kRadToDeg : 0;
            yawRate_ = std::isfinite(att.angular_velocity_z) ? att.angular_velocity_z * kRadToDeg : 0;
        } else {
            float roll = 0, pitch = 0, yaw = 0;
            mavlink_quaternion_to_euler(att.q, &roll, &pitch, &yaw);
            pitchDeg_ = pitch * kRadToDeg;
            yawDeg_ = yaw * kRadToDeg;
            pitchRate_ = yawRate_ = 0;
        }
        break;
    }
    default:
        break;
    }
}

bool OrusSimulator::handleAutopilotMessage(const mavlink_message_t& msg) {
    if (!options_.autopilot) {
        return false;
    }
    if (msg.msgid == MAVLINK_MSG_ID_GIMBAL_MANAGER_SET_ATTITUDE) {
        mavlink_gimbal_manager_set_attitude_t att;
        mavlink_msg_gimbal_manager_set_attitude_decode(&msg, &att);
        // As PX4: only the component in primary control moves the gimbal.
        if (msg.sysid != primarySystem_ || msg.compid != primaryComponent_) {
            return true;
        }
        {
            std::lock_guard lock(mutex_);
            ++received_.managerSetpoints;
        }
        if (std::isnan(att.q[0])) {
            pitchRate_ = std::isfinite(att.angular_velocity_y) ? att.angular_velocity_y * kRadToDeg : 0;
            yawRate_ = std::isfinite(att.angular_velocity_z) ? att.angular_velocity_z * kRadToDeg : 0;
        } else {
            float roll = 0, pitch = 0, yaw = 0;
            mavlink_quaternion_to_euler(att.q, &roll, &pitch, &yaw);
            pitchDeg_ = pitch * kRadToDeg;
            yawDeg_ = yaw * kRadToDeg;
            pitchRate_ = yawRate_ = 0;
        }
        return true;
    }
    if (msg.msgid != MAVLINK_MSG_ID_COMMAND_LONG) {
        return false;
    }
    mavlink_command_long_t cmd;
    mavlink_msg_command_long_decode(&msg, &cmd);
    if (cmd.target_component != MAV_COMP_ID_AUTOPILOT1) {
        return false;
    }
    if (cmd.command == MAV_CMD_DO_GIMBAL_MANAGER_CONFIGURE) {
        // PX4's reading of p1/p2: an id, -1 unchanged, -2 the autopilot, -3 release if held.
        auto pick = [&](float param, uint8_t current, uint8_t sender, uint8_t own) -> uint8_t {
            const int value = static_cast<int>(std::lround(param));
            if (value >= 0) return static_cast<uint8_t>(value);
            if (value == -2) return own;
            if (value == -3) return current == sender ? 0 : current;
            return current;
        };
        const bool releasing = std::lround(cmd.param1) == -3;
        const bool held = primarySystem_ == msg.sysid && primaryComponent_ == msg.compid;
        primarySystem_ = pick(cmd.param1, primarySystem_, static_cast<uint8_t>(msg.sysid), kSystem);
        primaryComponent_ = releasing ? (held ? 0 : primaryComponent_)
                                      : pick(cmd.param2, primaryComponent_, msg.compid,
                                             MAV_COMP_ID_AUTOPILOT1);
        sendManagerStatus();
    }
    send(MAV_COMP_ID_AUTOPILOT1, [&](mavlink_message_t& m, uint8_t c) {
        mavlink_command_ack_t ack{};
        ack.command = cmd.command;
        ack.result = MAV_RESULT_ACCEPTED;
        ack.target_system = static_cast<uint8_t>(msg.sysid);
        ack.target_component = msg.compid;
        mavlink_msg_command_ack_encode_status(kSystem, c, &txStatus_, &m, &ack);
    });
    return true;
}

void OrusSimulator::sendManagerStatus() {
    send(MAV_COMP_ID_AUTOPILOT1, [&](mavlink_message_t& m, uint8_t c) {
        mavlink_gimbal_manager_status_t st{};
        st.gimbal_device_id = kGimbal;
        st.primary_control_sysid = primarySystem_;
        st.primary_control_compid = primaryComponent_;
        mavlink_msg_gimbal_manager_status_encode_status(kSystem, c, &txStatus_, &m, &st);
    });
}

void OrusSimulator::handleCommand(const mavlink_command_long_t& cmd, int64_t nowNs) {
    uint8_t result = MAV_RESULT_ACCEPTED;
    switch (cmd.command) {
    case MAV_CMD_SET_CAMERA_MODE:
        cameraMode_ = static_cast<int>(cmd.param2);
        break;
    case MAV_CMD_IMAGE_START_CAPTURE:
        if (cameraMode_ != 0) {
            result = MAV_RESULT_DENIED;
        } else {
            ++imageCount_;
        }
        break;
    case MAV_CMD_VIDEO_START_CAPTURE:
        if (cameraMode_ != 1) {
            result = MAV_RESULT_DENIED;
        } else {
            recording_ = true;
            recordingStartNs_ = nowNs;
        }
        break;
    case MAV_CMD_VIDEO_STOP_CAPTURE:
        recording_ = false;
        break;
    case MAV_CMD_SET_CAMERA_ZOOM:
        if (static_cast<int>(cmd.param1) == ZOOM_TYPE_RANGE) {
            // The lens travels there over the next seconds (tick).
            zoomTarget_ = orus::zoomLevelForPercent(cmd.param2, zoomMax());
            zoomDirection_ = 0;
        } else {
            zoomDirection_ = static_cast<int>(cmd.param2);
            zoomTarget_ = 0;
        }
        break;
    case MAV_CMD_REQUEST_VIDEO_STREAM_INFORMATION:
        sendStreamInfo();
        break;
    case MAV_CMD_REQUEST_STORAGE_INFORMATION:
        sendStorage();
        break;
    case MAV_CMD_REQUEST_CAMERA_CAPTURE_STATUS:
        sendCaptureStatus();
        break;
    case MAV_CMD_REQUEST_MESSAGE:
        if (static_cast<uint32_t>(cmd.param1) == MAVLINK_MSG_ID_COMPONENT_INFORMATION_BASIC) {
            sendComponentInfo();
        }
        break;
    case MAV_CMD_SET_MESSAGE_INTERVAL: {
        // The real payload sends no ACK for these.
        const auto key = static_cast<uint32_t>(cmd.param1);
        const auto intervalUs = static_cast<int64_t>(cmd.param2);
        if (intervalUs <= 0) {
            intervalsNs_.erase(key);
        } else {
            intervalsNs_[key] = intervalUs * 1000;
        }
        return;
    }
    case orus::kCustomCommand:
        handleCustom(cmd);
        break;
    case MAV_CMD_DO_MOUNT_CONTROL:
        if (static_cast<int>(cmd.param6) == 2) {
            pitchRate_ = cmd.param1;
            yawRate_ = cmd.param3;
        } else {
            pitchDeg_ = cmd.param1;
            yawDeg_ = cmd.param3;
            pitchRate_ = yawRate_ = 0;
        }
        break;
    default:
        result = MAV_RESULT_UNSUPPORTED;
        break;
    }
    if (options_.acknowledgeCommands) {
        sendAck(cmd.command, result);
    }
}

void OrusSimulator::handleCustom(const mavlink_command_long_t& cmd) {
    const int group = static_cast<int>(cmd.param1);
    const int function = static_cast<int>(cmd.param2);
    const int sub = static_cast<int>(cmd.param3);
    if (group == 4 && function == 0 && ignoreTracking_.load()) {
        return;
    }
    if (group == 4 && function == 0 && sub == 0) {
        trackingMode_ = static_cast<int>(cmd.param4);
        if (trackingMode_ == 0) {
            trackingState_ = 0;
        }
    } else if (group == 4 && function == 0 && sub == 1) {
        if (trackingMode_ == 1) {
            trackingState_ = 1;
            trackW_ = cmd.param6;
            trackH_ = cmd.param7;
            trackX_ = cmd.param4 - trackW_ / 2;
            trackY_ = cmd.param5 - trackH_ / 2;
        }
        // Eagle Eye or inactive tracker: re-point the gimbal towards the point.
        yawDeg_ += (cmd.param4 / orus::kFrameWidth - 0.5f) * 60.0f / zoomLevel_;
        pitchDeg_ -= (cmd.param5 / orus::kFrameHeight - 0.5f) * 34.0f / zoomLevel_;
    } else if (group == 2 && function == 6) {
        ffcMode_ = sub == 1 ? 1 : 0;
    } else if (group == 2 && function == 7) {
        ++ffcCount_;
    } else if (group == 2 && function == 0 && sub == 4) {
        // The lens travels there over the next seconds (tick).
        zoomTarget_ = std::clamp(cmd.param4, 1.0f, zoomMax());
        zoomDirection_ = 0;
    }
}

float OrusSimulator::zoomMax() const {
    return param("C_V_ZM_MODE") == 2 ? 25.0f : 300.0f;
}

void OrusSimulator::moveZoom(float dt) {
    // Constant speed in log scale: 1x to 300x in about 4 s, like a real zoom lens plus
    // digital zoom.
    constexpr float kLogPerSecond = 1.4f;
    const float max = zoomMax();
    float level = zoomLevel_;
    if (zoomTarget_ > 0) {
        const float step = kLogPerSecond * dt;
        const float distance = std::log(zoomTarget_ / level);
        level = std::fabs(distance) <= step ? zoomTarget_
                                             : level * std::exp(distance > 0 ? step : -step);
    } else if (zoomDirection_ != 0) {
        level *= std::exp(static_cast<float>(zoomDirection_) * kLogPerSecond * 0.5f * dt);
    }
    zoomLevel_ = std::clamp(level, 1.0f, max);
}

void OrusSimulator::zoomFromElsewhere(float level, uint8_t system, uint8_t component) {
    foreignTarget_.store(static_cast<uint16_t>(system << 8 | component));
    foreignZoom_.store(level);
}

void OrusSimulator::sendAck(uint16_t command, uint8_t result) {
    const uint8_t from = command == orus::kCustomCommand ? kPayload : kCamera;
    send(from, [&](mavlink_message_t& m, uint8_t c) {
        mavlink_command_ack_t ack{};
        ack.command = command;
        ack.result = result;
        ack.target_system = clientSystem_;
        ack.target_component = clientComponent_;
        mavlink_msg_command_ack_encode_status(kSystem, c, &txStatus_, &m, &ack);
    });
}

void OrusSimulator::sendHeartbeats() {
    if (options_.autopilot) {
        send(MAV_COMP_ID_AUTOPILOT1, [&](mavlink_message_t& m, uint8_t c) {
            mavlink_heartbeat_t hb{};
            hb.type = MAV_TYPE_QUADROTOR;
            hb.autopilot = MAV_AUTOPILOT_PX4;
            hb.system_status = MAV_STATE_ACTIVE;
            hb.mavlink_version = 3;
            mavlink_msg_heartbeat_encode_status(kSystem, c, &txStatus_, &m, &hb);
        });
    }
    for (uint8_t comp : {kPayload, kCamera, kGimbal}) {
        send(comp, [&](mavlink_message_t& m, uint8_t c) {
            mavlink_heartbeat_t hb{};
            hb.type = comp == kGimbal ? MAV_TYPE_GIMBAL : MAV_TYPE_CAMERA;
            hb.autopilot = MAV_AUTOPILOT_INVALID;
            hb.system_status = MAV_STATE_ACTIVE;
            hb.mavlink_version = 3;
            mavlink_msg_heartbeat_encode_status(kSystem, c, &txStatus_, &m, &hb);
        });
    }
}

void OrusSimulator::sendGimbalStatus() {
    send(kGimbal, [&](mavlink_message_t& m, uint8_t c) {
        mavlink_gimbal_device_attitude_status_t s{};
        mavlink_euler_to_quaternion(0, pitchDeg_ / kRadToDeg, yawDeg_ / kRadToDeg, s.q);
        s.angular_velocity_y = pitchRate_ / kRadToDeg;
        s.angular_velocity_z = yawRate_ / kRadToDeg;
        // Yaw is simulated relative to the vehicle; say so explicitly, as current MAVLink does.
        s.flags = static_cast<uint16_t>(gimbalFlags_ | GIMBAL_DEVICE_FLAGS_YAW_IN_VEHICLE_FRAME);
        s.delta_yaw = options_.headingDeg / kRadToDeg;
        s.delta_yaw_velocity = 0;
        s.target_system = clientSystem_;
        mavlink_msg_gimbal_device_attitude_status_encode_status(kSystem, c, &txStatus_, &m, &s);
    });
}

float OrusSimulator::dataValue(uint8_t index) const {
    using orus::DataIndex;
    switch (static_cast<DataIndex>(index)) {
    case DataIndex::EoZoom:
        return zoomLevel_;
    case DataIndex::IrZoom:
        return static_cast<float>(param("C_T_ZOOM") + 1);
    case DataIndex::IrType:
        return 0.0f;
    case DataIndex::IrPalette:
        return static_cast<float>(param("C_T_PALETTE"));
    case DataIndex::IrFfcMode:
        return static_cast<float>(ffcMode_.load());
    case DataIndex::LrfRange:
        return 412.5f;
    case DataIndex::TrackX:
        return trackX_;
    case DataIndex::TrackY:
        return trackY_;
    case DataIndex::TrackW:
        return trackW_;
    case DataIndex::TrackH:
        return trackH_;
    case DataIndex::TrackStatus:
        return static_cast<float>((trackingMode_ << 8) | trackingState_);
    case DataIndex::ViewMode:
        return static_cast<float>(param("C_SOURCE"));
    case DataIndex::GimbalMode:
        return static_cast<float>(param("GB_MODE"));
    case DataIndex::IrTempMax:
        return 48.2f;
    case DataIndex::IrTempMin:
        return 12.4f;
    case DataIndex::IrTempMean:
        return 23.9f;
    default:
        return 0.0f;
    }
}

void OrusSimulator::sendDebug(uint8_t index, float value) {
    send(kPayload, [&](mavlink_message_t& m, uint8_t c) {
        mavlink_debug_t d{};
        d.ind = index;
        d.value = value;
        mavlink_msg_debug_encode_status(kSystem, c, &txStatus_, &m, &d);
    });
}

void OrusSimulator::sendDetections() {
    if (param("TRACK_MODE") != 1) {
        return; // the real payload is silent while detection is off
    }
    ++detectionFrame_;
    send(kPayload, [&](mavlink_message_t& m, uint8_t c) {
        mavlink_v2_extension_t ext{};
        ext.message_type = 0x4001;
        uint8_t* p = ext.payload;
        const uint8_t count = 2;
        p[0] = 0x44;
        p[1] = 0x47;
        p[2] = 1;
        p[3] = count;
        std::memcpy(p + 4, &detectionFrame_, 4); // little-endian hosts
        for (uint8_t i = 0; i < count; ++i) {
            // Two "cars" drifting across the frame.
            const auto x = static_cast<uint16_t>((detectionFrame_ * 4 + i * 700) % 1700);
            const auto y = static_cast<uint16_t>(400 + i * 250);
            const uint16_t w = 160, h = 90;
            const auto track = static_cast<uint16_t>(i + 1);
            uint8_t* b = p + 8 + i * 12;
            std::memcpy(b, &x, 2);
            std::memcpy(b + 2, &y, 2);
            std::memcpy(b + 4, &w, 2);
            std::memcpy(b + 6, &h, 2);
            std::memcpy(b + 8, &track, 2);
            b[10] = 2;
            b[11] = static_cast<uint8_t>(200 + i * 30);
        }
        mavlink_msg_v2_extension_encode_status(kSystem, c, &txStatus_, &m, &ext);
    });
}

void OrusSimulator::sendCaptureStatus() {
    send(kCamera, [&](mavlink_message_t& m, uint8_t c) {
        mavlink_camera_capture_status_t s{};
        s.video_status = recording_ ? 1 : 0;
        s.recording_time_ms =
            recording_ ? static_cast<uint32_t>((monotonicNs() - recordingStartNs_) / kNsPerMs) : 0;
        s.image_count = imageCount_;
        mavlink_msg_camera_capture_status_encode_status(kSystem, c, &txStatus_, &m, &s);
    });
}

void OrusSimulator::sendStorage() {
    send(kCamera, [&](mavlink_message_t& m, uint8_t c) {
        mavlink_storage_information_t s{};
        s.storage_count = 1;
        s.storage_id = 1;
        s.status = STORAGE_STATUS_READY;
        s.total_capacity = 122000.0f;
        s.used_capacity = 18000.0f;
        s.available_capacity = 104000.0f;
        mavlink_msg_storage_information_encode_status(kSystem, c, &txStatus_, &m, &s);
    });
}

void OrusSimulator::sendStreamInfo() {
    send(kCamera, [&](mavlink_message_t& m, uint8_t c) {
        mavlink_video_stream_information_t s{};
        s.stream_id = 1;
        s.count = 1;
        s.type = VIDEO_STREAM_TYPE_RTSP;
        s.framerate = 30;
        s.resolution_h = 1920;
        s.resolution_v = 1080;
        s.bitrate = 4'000'000;
        s.encoding = VIDEO_STREAM_ENCODING_H264;
        std::snprintf(s.name, sizeof(s.name), "EO");
        std::snprintf(s.uri, sizeof(s.uri), "%s", options_.streamUri.c_str());
        mavlink_msg_video_stream_information_encode_status(kSystem, c, &txStatus_, &m, &s);
    });
}

void OrusSimulator::sendComponentInfo() {
    send(kPayload, [&](mavlink_message_t& m, uint8_t c) {
        mavlink_component_information_basic_t s{};
        std::snprintf(s.vendor_name, sizeof(s.vendor_name), "Gremsy");
        std::snprintf(s.model_name, sizeof(s.model_name), "ORUS L (simulated)");
        std::snprintf(s.software_version, sizeof(s.software_version), "3.0.0-sim");
        std::snprintf(s.serial_number, sizeof(s.serial_number), "SIM-0001");
        mavlink_msg_component_information_basic_encode_status(kSystem, c, &txStatus_, &m, &s);
    });
}

void OrusSimulator::tick(int64_t nowNs) {
    if (!peer_) {
        return;
    }
    if (const float level = foreignZoom_.exchange(0); level > 0) {
        zoomTarget_ = std::clamp(level, 1.0f, zoomMax());
        zoomDirection_ = 0;
        const uint16_t target = foreignTarget_.load();
        send(kCamera, [&](mavlink_message_t& m, uint8_t c) {
            mavlink_command_ack_t ack{};
            ack.command = MAV_CMD_SET_CAMERA_ZOOM;
            ack.result = MAV_RESULT_ACCEPTED;
            ack.target_system = static_cast<uint8_t>(target >> 8);
            ack.target_component = static_cast<uint8_t>(target);
            mavlink_msg_command_ack_encode_status(kSystem, c, &txStatus_, &m, &ack);
        });
    }
    if (nowNs >= nextHeartbeatNs_) {
        sendHeartbeats();
        nextHeartbeatNs_ = nowNs + kNsPerSec;
    }
    if (lastRateNs_ != 0) {
        const float dt = static_cast<float>(nowNs - lastRateNs_) / 1e9f;
        pitchDeg_ = std::clamp(pitchDeg_ + pitchRate_ * dt, -120.0f, 45.0f);
        yawDeg_ += yawRate_ * dt;
        yawDeg_ = std::remainder(yawDeg_, 360.0f);
        moveZoom(dt);
    }
    lastRateNs_ = nowNs;
    if (nowNs >= nextGimbalNs_) {
        sendGimbalStatus();
        nextGimbalNs_ = nowNs + kGimbalPeriodNs;
    }
    if (options_.autopilot && nowNs >= nextManagerStatusNs_) {
        sendManagerStatus();
        nextManagerStatusNs_ = nowNs + kNsPerSec / 2;
    }
    for (const auto& [key, interval] : intervalsNs_) {
        auto& next = nextSendNs_[key];
        if (nowNs < next) {
            continue;
        }
        next = nowNs + interval;
        if (key == MAVLINK_MSG_ID_V2_EXTENSION) {
            sendDetections();
        } else if (key < static_cast<uint32_t>(orus::DataIndex::Count)) {
            sendDebug(static_cast<uint8_t>(key), dataValue(static_cast<uint8_t>(key)));
        }
    }
}

} // namespace imago::sim
