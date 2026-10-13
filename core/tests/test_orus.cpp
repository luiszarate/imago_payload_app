#include "imago/imago_core.h"
#include "payload/orus_protocol.h"
#include "payload/payload_model.h"

#include <doctest.h>

#include <cmath>
#include <cstring>
#include <vector>

using namespace imago;

TEST_CASE("camera parameters are sent as little-endian uint32 to the camera component") {
    orus::Targets t;
    t.cameraSystem = 1;
    t.cameraComponent = 100;
    const auto m = orus::setCameraParam(t, "C_SOURCE", 0x01020304);
    CHECK(m.target_component == 100);
    CHECK(std::string(m.param_id, strnlen(m.param_id, 16)) == "C_SOURCE");
    CHECK(static_cast<uint8_t>(m.param_value[0]) == 0x04);
    CHECK(static_cast<uint8_t>(m.param_value[3]) == 0x01);
    CHECK(m.param_type == MAV_PARAM_EXT_TYPE_UINT32);

    // A full 16-character id has no terminator and must not overflow.
    const auto longId = orus::setCameraParam(t, "ABCDEFGHIJKLMNOP", 1);
    CHECK(std::string(longId.param_id, strnlen(longId.param_id, 16)) == "ABCDEFGHIJKLMNOP");
}

TEST_CASE("tracking commands use MAV_CMD_USER_4 on the payload manager") {
    orus::Targets t;
    const auto mode = orus::trackingMode(t, IMAGO_TRACKING_ACTIVE);
    CHECK(mode.command == 31013);
    CHECK(mode.target_component == 26);
    CHECK(mode.param1 == 4);
    CHECK(mode.param2 == 0);
    CHECK(mode.param3 == 0);
    CHECK(mode.param4 == 1);

    const auto pos = orus::trackingPosition(t, 960, 540, 128, 64);
    CHECK(pos.param3 == 1);
    CHECK(pos.param4 == 960);
    CHECK(pos.param5 == 540);
    CHECK(pos.param6 == 128);
    CHECK(pos.param7 == 64);

    const auto zoom = orus::zoomToLevel(t, 12.5f);
    CHECK(zoom.param1 == 2);
    CHECK(zoom.param3 == 4);
    CHECK(zoom.param4 == 12.5f);
}

TEST_CASE("data interval uses the value index and microseconds") {
    orus::Targets t;
    const auto m = orus::setDataInterval(t, static_cast<uint32_t>(orus::DataIndex::TrackStatus), 50);
    CHECK(m.command == MAV_CMD_SET_MESSAGE_INTERVAL);
    CHECK(m.target_component == 26);
    CHECK(m.param1 == 7);
    CHECK(m.param2 == 50000);
    CHECK(m.param7 == 1);
}

TEST_CASE("gimbal rate and angle messages") {
    orus::Targets t;
    const auto rate = orus::gimbalRate(t, GIMBAL_DEVICE_FLAGS_YAW_LOCK, 90.0f, -45.0f);
    CHECK(std::isnan(rate.q[0]));
    CHECK(rate.flags == GIMBAL_DEVICE_FLAGS_YAW_LOCK);
    CHECK(rate.angular_velocity_y == doctest::Approx(1.5708f));
    CHECK(rate.angular_velocity_z == doctest::Approx(-0.7854f));
    CHECK(rate.target_component == MAV_COMP_ID_GIMBAL);

    const auto angle = orus::gimbalAngle(t, 0, -90.0f, 0.0f);
    CHECK(std::isnan(angle.angular_velocity_x));
    float roll = 0, pitch = 0, yaw = 0;
    mavlink_quaternion_to_euler(angle.q, &roll, &pitch, &yaw);
    CHECK(pitch == doctest::Approx(-1.5708f).epsilon(0.001));
}

TEST_CASE("gimbal mode from device flags") {
    CHECK(orus::gimbalModeFromFlags(GIMBAL_DEVICE_FLAGS_YAW_LOCK) == IMAGO_GIMBAL_MODE_LOCK);
    CHECK(orus::gimbalModeFromFlags(GIMBAL_DEVICE_FLAGS_RETRACT) == IMAGO_GIMBAL_MODE_OFF);
    CHECK(orus::gimbalModeFromFlags(GIMBAL_DEVICE_FLAGS_NEUTRAL) == IMAGO_GIMBAL_MODE_RESET);
    CHECK(orus::gimbalModeFromFlags(0x4000) == IMAGO_GIMBAL_MODE_MAPPING);
    CHECK(orus::gimbalModeFromFlags(0) == IMAGO_GIMBAL_MODE_FOLLOW);
}

TEST_CASE("track status packs mode and state") {
    const auto s = orus::decodeTrackStatus(static_cast<float>((1 << 8) | 2));
    CHECK(s.mode == IMAGO_TRACKING_ACTIVE);
    CHECK(s.state == IMAGO_TRACKING_LOST);
}

namespace {

mavlink_v2_extension_t detectionPacket(uint8_t count, uint16_t magic = 0x4744) {
    mavlink_v2_extension_t ext{};
    ext.message_type = 0x4001;
    uint8_t* p = ext.payload;
    p[0] = static_cast<uint8_t>(magic);
    p[1] = static_cast<uint8_t>(magic >> 8);
    p[2] = 1;
    p[3] = count;
    p[4] = 42; // frame id
    for (uint8_t i = 0; i < count; ++i) {
        uint8_t* b = p + 8 + i * 12;
        const uint16_t x = 960, y = 540, w = 192, h = 108, track = static_cast<uint16_t>(7 + i);
        b[0] = x & 0xFF; b[1] = x >> 8;
        b[2] = y & 0xFF; b[3] = y >> 8;
        b[4] = w & 0xFF; b[5] = w >> 8;
        b[6] = h & 0xFF; b[7] = h >> 8;
        b[8] = static_cast<uint8_t>(track & 0xFF);
        b[9] = static_cast<uint8_t>(track >> 8);
        b[10] = 2;   // car
        b[11] = 255; // confidence 1.0
    }
    return ext;
}

} // namespace

TEST_CASE("detection packets decode into normalized boxes") {
    const auto packet = orus::decodeDetections(detectionPacket(2));
    REQUIRE(packet);
    CHECK(packet->frameId == 42);
    REQUIRE(packet->count == 2);
    CHECK(packet->boxes[0].box.x == doctest::Approx(0.5f));
    CHECK(packet->boxes[0].box.y == doctest::Approx(0.5f));
    CHECK(packet->boxes[0].box.w == doctest::Approx(0.1f));
    CHECK(packet->boxes[0].box.h == doctest::Approx(0.1f));
    CHECK(packet->boxes[1].track_id == 8);
    CHECK(packet->boxes[1].class_id == 2);
    CHECK(packet->boxes[1].confidence == doctest::Approx(1.0f));

    CHECK_FALSE(orus::decodeDetections(detectionPacket(1, 0x1234)).has_value());
    auto ext = detectionPacket(1);
    ext.message_type = 0x1000;
    CHECK_FALSE(orus::decodeDetections(ext).has_value());
    // More boxes than fit are clamped.
    ext = detectionPacket(20);
    ext.payload[3] = 200;
    CHECK(orus::decodeDetections(ext)->count == 20);
}

namespace {

mavlink_message_t fromPayload(uint8_t compid, auto encode) {
    mavlink_message_t msg{};
    mavlink_status_t status{};
    encode(msg, status, compid);
    return msg;
}

} // namespace

TEST_CASE("payload model: link, gimbal, tracking and detections") {
    PayloadModel model(3'000'000'000);
    std::vector<PayloadModel::Notice> notices;
    const int64_t t0 = 10'000'000'000;

    mavlink_heartbeat_t hb{};
    auto heartbeat = fromPayload(26, [&](mavlink_message_t& m, mavlink_status_t& s, uint8_t c) {
        mavlink_msg_heartbeat_encode_status(1, c, &s, &m, &hb);
    });
    model.handle(heartbeat, t0, notices);
    REQUIRE(notices.size() == 1);
    CHECK(notices[0].kind == PayloadModel::Notice::Kind::LinkUp);
    CHECK(model.linkUp());

    // Autopilot traffic on a shared Herelink link is ignored.
    notices.clear();
    auto autopilot = fromPayload(1, [&](mavlink_message_t& m, mavlink_status_t& s, uint8_t c) {
        mavlink_msg_heartbeat_encode_status(1, c, &s, &m, &hb);
    });
    model.handle(autopilot, t0, notices);
    CHECK(notices.empty());

    mavlink_gimbal_device_attitude_status_t att{};
    mavlink_euler_to_quaternion(0, -0.5236f, 0.7854f, att.q); // pitch -30, yaw 45
    att.flags = GIMBAL_DEVICE_FLAGS_YAW_LOCK;
    auto gimbal = fromPayload(154, [&](mavlink_message_t& m, mavlink_status_t& s, uint8_t c) {
        mavlink_msg_gimbal_device_attitude_status_encode_status(1, c, &s, &m, &att);
    });
    model.handle(gimbal, t0, notices);

    auto debug = [&](uint8_t index, float value) {
        mavlink_debug_t d{};
        d.ind = index;
        d.value = value;
        return fromPayload(26, [&](mavlink_message_t& m, mavlink_status_t& s, uint8_t c) {
            mavlink_msg_debug_encode_status(1, c, &s, &m, &d);
        });
    };
    model.handle(debug(3, 960), t0, notices);
    model.handle(debug(4, 540), t0, notices);
    model.handle(debug(5, 192), t0, notices);
    model.handle(debug(6, 108), t0, notices);
    model.handle(debug(7, (1 << 8) | 1), t0, notices);

    auto ext = detectionPacket(3);
    auto detections = fromPayload(26, [&](mavlink_message_t& m, mavlink_status_t& s, uint8_t c) {
        mavlink_msg_v2_extension_encode_status(1, c, &s, &m, &ext);
    });
    model.handle(detections, t0, notices);

    imago_state st{};
    model.snapshot(t0 + 100'000'000, st);
    CHECK(st.link_up);
    CHECK(st.gimbal_valid);
    CHECK(st.gimbal_pitch_deg == doctest::Approx(-30.0f).epsilon(0.01));
    CHECK(st.gimbal_yaw_deg == doctest::Approx(45.0f).epsilon(0.01));
    CHECK(st.gimbal_mode == IMAGO_GIMBAL_MODE_LOCK);
    CHECK(model.gimbalFlags() == GIMBAL_DEVICE_FLAGS_YAW_LOCK);
    CHECK(st.tracking_state == IMAGO_TRACKING_TRACKED);
    CHECK(st.tracking_box_valid);
    CHECK(st.tracking_box.x == doctest::Approx(0.5f));
    CHECK(st.detection_count == 3);
    CHECK(st.gimbal_component_id == 154);

    // Detections and tracking expire; the link drops after the heartbeat timeout.
    model.snapshot(t0 + 2'000'000'000, st);
    CHECK(st.detection_count == 0);
    CHECK_FALSE(st.tracking_box_valid);
    notices.clear();
    model.tick(t0 + 3'500'000'000, notices);
    REQUIRE(notices.size() == 1);
    CHECK(notices[0].kind == PayloadModel::Notice::Kind::LinkDown);
}
