#include "imago/imago_core.h"
#include "payload/orus_protocol.h"
#include "payload/payload_model.h"
#include "web/http_client.h"
#include "web/orus_media.h"
#include "web/orus_web.h"

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

    const auto zoom = orus::zoomRange(t, 37.5f);
    CHECK(zoom.command == MAV_CMD_SET_CAMERA_ZOOM);
    CHECK(zoom.param1 == static_cast<float>(ZOOM_TYPE_RANGE));
    CHECK(zoom.param2 == 37.5f);
}

TEST_CASE("zoom level tables follow the camera's level settings") {
    const auto combined = orus::zoomLevels(IMAGO_ZOOM_MODE_COMBINED);
    REQUIRE(combined.size() == 24);
    CHECK(combined[0] == 1.0f);
    CHECK(combined[1] == 2.0f);
    CHECK(combined[11] == 22.0f);
    CHECK(combined[12] == 25.0f);
    CHECK(combined[13] == 50.0f);
    CHECK(combined[23] == 300.0f);
    const auto sr = orus::zoomLevels(IMAGO_ZOOM_MODE_SUPER_RESOLUTION);
    REQUIRE(sr.size() == 13);
    CHECK(sr[12] == 25.0f);
    CHECK(orus::zoomLevels(IMAGO_ZOOM_MODE_UNKNOWN).size() == 24);

    CHECK(orus::zoomLevelParam(IMAGO_ZOOM_MODE_SUPER_RESOLUTION) == "C_V_ZM_SR_LV");
    CHECK(orus::zoomLevelParam(IMAGO_ZOOM_MODE_COMBINED) == "C_V_ZM_CB_LV");

    // Nearest on a log scale, so each step weighs the same.
    CHECK(orus::nearestZoomIndex(combined, 10.5f) == 5);  // 10x
    CHECK(orus::nearestZoomIndex(combined, 37.0f) == 13); // 50x, not 25x
    CHECK(orus::nearestZoomIndex(combined, 0.5f) == 0);
    CHECK(orus::nearestZoomIndex(combined, 1000.0f) == 23);
    CHECK(orus::nearestZoomIndex(sr, 100.0f) == 12);
}

TEST_CASE("zoom range percent maps linearly over the zoom factor of the current mode") {
    CHECK(orus::zoomPercentForLevel(1.0f, 300.0f) == 0.0f);
    CHECK(orus::zoomPercentForLevel(300.0f, 300.0f) == 100.0f);
    CHECK(orus::zoomPercentForLevel(13.0f, 25.0f) == doctest::Approx(50.0f));
    CHECK(orus::zoomPercentForLevel(500.0f, 25.0f) == 100.0f);
    CHECK(orus::zoomPercentForLevel(0.5f, 25.0f) == 0.0f);
    CHECK(orus::zoomLevelForPercent(50.0f, 25.0f) == doctest::Approx(13.0f));
    CHECK(orus::zoomLevelForPercent(100.0f, 300.0f) == doctest::Approx(300.0f));
    CHECK(orus::zoomLevelForPercent(orus::zoomPercentForLevel(11.0f, 300.0f), 300.0f) ==
          doctest::Approx(11.0f));
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
    // YAW_LOCK without a frame flag: yaw is relative to North and the heading is unknown.
    CHECK_FALSE(st.gimbal_yaw_valid);
    CHECK(st.gimbal_yaw_north_valid);
    CHECK(st.gimbal_yaw_north_deg == doctest::Approx(45.0f).epsilon(0.01));
    CHECK_FALSE(st.vehicle_heading_valid);
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

TEST_CASE("payload model: gimbal yaw against the vehicle and North") {
    PayloadModel model(3'000'000'000);
    std::vector<PayloadModel::Notice> notices;
    const int64_t t0 = 10'000'000'000;
    imago_state st{};

    auto status = [&](float yawDeg, uint16_t flags, float deltaYawDeg) {
        mavlink_gimbal_device_attitude_status_t att{};
        mavlink_euler_to_quaternion(0, -0.5236f, yawDeg * 3.14159265f / 180.0f, att.q);
        att.flags = flags;
        att.delta_yaw = deltaYawDeg * 3.14159265f / 180.0f;
        return fromPayload(154, [&](mavlink_message_t& m, mavlink_status_t& s, uint8_t c) {
            mavlink_msg_gimbal_device_attitude_status_encode_status(1, c, &s, &m, &att);
        });
    };

    SUBCASE("vehicle frame with delta_yaw") {
        model.handle(status(-40, GIMBAL_DEVICE_FLAGS_YAW_IN_VEHICLE_FRAME, 350), t0, notices);
        model.snapshot(t0, st);
        CHECK(st.gimbal_yaw_valid);
        CHECK(st.gimbal_yaw_deg == doctest::Approx(-40.0f).epsilon(0.001));
        CHECK(st.vehicle_heading_valid);
        CHECK(st.vehicle_heading_deg == doctest::Approx(350.0f).epsilon(0.001));
        CHECK(st.gimbal_yaw_north_deg == doctest::Approx(310.0f).epsilon(0.001));
        // The heading expires with the messages that carry it.
        model.snapshot(t0 + 2'000'000'000, st);
        CHECK_FALSE(st.vehicle_heading_valid);
        CHECK_FALSE(st.gimbal_yaw_north_valid);
        CHECK(st.gimbal_yaw_valid);
    }
    SUBCASE("earth frame converted with delta_yaw") {
        model.handle(status(10, GIMBAL_DEVICE_FLAGS_YAW_LOCK | GIMBAL_DEVICE_FLAGS_YAW_IN_EARTH_FRAME, 300),
                     t0, notices);
        model.snapshot(t0, st);
        CHECK(st.gimbal_yaw_north_deg == doctest::Approx(10.0f).epsilon(0.001));
        CHECK(st.gimbal_yaw_valid);
        CHECK(st.gimbal_yaw_deg == doctest::Approx(70.0f).epsilon(0.001)); // 10 - 300 wrapped
    }
    SUBCASE("no delta_yaw (older sender): heading from the autopilot") {
        // delta_yaw = 0 is truncated away by MAVLink 2, just like a sender without the field.
        model.handle(status(20, 0, 0), t0, notices);
        model.snapshot(t0, st);
        CHECK(st.gimbal_yaw_valid);
        CHECK(st.gimbal_yaw_deg == doctest::Approx(20.0f).epsilon(0.001));
        CHECK_FALSE(st.vehicle_heading_valid);

        mavlink_attitude_t att{};
        att.yaw = -1.5708f; // -90 deg = 270
        auto autopilot = fromPayload(1, [&](mavlink_message_t& m, mavlink_status_t& s, uint8_t c) {
            mavlink_msg_attitude_encode_status(1, c, &s, &m, &att);
        });
        model.handle(autopilot, t0, notices);
        model.snapshot(t0, st);
        CHECK(st.vehicle_heading_valid);
        CHECK(st.vehicle_heading_deg == doctest::Approx(270.0f).epsilon(0.001));
        CHECK(st.gimbal_yaw_north_deg == doctest::Approx(290.0f).epsilon(0.001));

        // A second vehicle on the link is ignored.
        att.yaw = 0.5f;
        auto other = fromPayload(1, [&](mavlink_message_t& m, mavlink_status_t& s, uint8_t c) {
            mavlink_msg_attitude_encode_status(7, c, &s, &m, &att);
        });
        model.handle(other, t0, notices);
        model.snapshot(t0, st);
        CHECK(st.vehicle_heading_deg == doctest::Approx(270.0f).epsilon(0.001));
    }
    SUBCASE("older firmware: MOUNT_ORIENTATION until the attitude status arrives") {
        mavlink_mount_orientation_t mo{};
        mo.pitch = -60;
        mo.yaw = 15;
        auto mount = fromPayload(154, [&](mavlink_message_t& m, mavlink_status_t& s, uint8_t c) {
            mavlink_msg_mount_orientation_encode_status(1, c, &s, &m, &mo);
        });
        model.handle(mount, t0, notices);
        model.snapshot(t0, st);
        CHECK(st.gimbal_valid);
        CHECK(st.gimbal_pitch_deg == doctest::Approx(-60.0f));
        CHECK(st.gimbal_yaw_deg == doctest::Approx(15.0f));
        model.handle(status(-5, GIMBAL_DEVICE_FLAGS_YAW_IN_VEHICLE_FRAME, 0), t0, notices);
        model.handle(mount, t0, notices);
        model.snapshot(t0, st);
        CHECK(st.gimbal_yaw_deg == doctest::Approx(-5.0f).epsilon(0.001));
    }
}

TEST_CASE("payload model: pan follows the yaw across +-180 and corrects a wrong start") {
    PayloadModel model(3'000'000'000);
    std::vector<PayloadModel::Notice> notices;
    const int64_t t0 = 10'000'000'000;
    imago_state st{};
    auto status = [&](float yawDeg, uint16_t flags = GIMBAL_DEVICE_FLAGS_YAW_IN_VEHICLE_FRAME,
                      float deltaYawDeg = 0) {
        mavlink_gimbal_device_attitude_status_t att{};
        mavlink_euler_to_quaternion(0, -0.3f, yawDeg * 3.14159265f / 180.0f, att.q);
        att.flags = flags;
        att.delta_yaw = deltaYawDeg * 3.14159265f / 180.0f;
        return fromPayload(154, [&](mavlink_message_t& m, mavlink_status_t& s, uint8_t c) {
            mavlink_msg_gimbal_device_attitude_status_encode_status(1, c, &s, &m, &att);
        });
    };
    auto pan = [&](float yawDeg, int64_t t) {
        model.handle(status(yawDeg), t, notices);
        model.snapshot(t, st);
        return st.gimbal_pan_deg;
    };
    auto near = [](float deg) { return doctest::Approx(deg).epsilon(0.001); };
    const int64_t ms = 1'000'000;

    SUBCASE("past 180 the pan keeps counting, and warns near the travel with hysteresis") {
        CHECK(pan(0, t0) == near(0));
        CHECK(pan(170, t0 + 50 * ms) == near(170));
        CHECK(pan(-170, t0 + 100 * ms) == near(190)); // the yaw wrapped, the pan did not
        CHECK(pan(-100, t0 + 150 * ms) == near(260));
        CHECK_FALSE(st.gimbal_pan_near_limit);
        CHECK(pan(-70, t0 + 200 * ms) == near(290));
        CHECK(st.gimbal_pan_valid);
        CHECK(st.gimbal_pan_near_limit);
        CHECK(pan(-75, t0 + 250 * ms) == near(285));
        CHECK(st.gimbal_pan_near_limit); // still, until 10 below the line
        CHECK(pan(-85, t0 + 300 * ms) == near(275));
        CHECK_FALSE(st.gimbal_pan_near_limit);
        CHECK(pan(100, t0 + 350 * ms) == near(100)); // back the same way
    }
    SUBCASE("a start on the far side of 180 shows once the travel is exceeded") {
        // Really at 190 (reads -170), turning back through 150 and 100 to 60.
        CHECK(pan(-170, t0) == near(-170));
        CHECK(pan(150, t0 + 50 * ms) == near(-210));
        CHECK(pan(100, t0 + 100 * ms) == near(-260));
        CHECK(pan(60, t0 + 150 * ms) == near(-300)); // still possible, so still believed
        CHECK(pan(20, t0 + 200 * ms) == near(20));    // the front sector settles it
    }
    SUBCASE("the front sector anchors the pan: a wrong start heals there") {
        CHECK(pan(-170, t0) == near(-170)); // really at 190
        CHECK(pan(-100, t0 + 50 * ms) == near(-100));
        CHECK(pan(-20, t0 + 100 * ms) == near(-20)); // within +-30: the pan is the yaw
        CHECK(pan(-170, t0 + 150 * ms) == near(-170));
    }
    SUBCASE("yaw reported against North uses the heading, same as gimbal_yaw_deg") {
        model.handle(status(50, GIMBAL_DEVICE_FLAGS_YAW_LOCK, 30), t0, notices);
        model.snapshot(t0, st);
        CHECK(st.gimbal_yaw_deg == near(20));
        CHECK(st.gimbal_pan_deg == near(20));
        model.handle(status(-120, GIMBAL_DEVICE_FLAGS_YAW_LOCK, 30), t0 + 50 * ms, notices);
        model.snapshot(t0 + 50 * ms, st);
        CHECK(st.gimbal_yaw_deg == near(-150));
        CHECK(st.gimbal_pan_deg == near(-150));
    }
    SUBCASE("an unknown attitude neither poisons nor moves the pan") {
        CHECK(pan(100, t0) == near(100));
        mavlink_gimbal_device_attitude_status_t att{};
        att.q[0] = att.q[1] = att.q[2] = att.q[3] = NAN;
        att.flags = GIMBAL_DEVICE_FLAGS_YAW_IN_VEHICLE_FRAME;
        model.handle(fromPayload(154, [&](mavlink_message_t& m, mavlink_status_t& s, uint8_t c) {
            mavlink_msg_gimbal_device_attitude_status_encode_status(1, c, &s, &m, &att);
        }), t0 + 50 * ms, notices);
        CHECK(pan(110, t0 + 100 * ms) == near(110));
    }
    SUBCASE("stale when the attitude stops arriving") {
        CHECK(pan(100, t0) == near(100));
        model.snapshot(t0 + 2'000 * ms, st);
        CHECK_FALSE(st.gimbal_pan_valid);
    }
    SUBCASE("unknown again after the link drops") {
        CHECK(pan(-170, t0) == near(-170));
        mavlink_heartbeat_t hb{};
        model.handle(fromPayload(26, [&](mavlink_message_t& m, mavlink_status_t& s, uint8_t c) {
            mavlink_msg_heartbeat_encode_status(1, c, &s, &m, &hb);
        }), t0, notices);
        model.tick(t0 + 4'000'000'000, notices);
        model.snapshot(t0 + 4'000'000'000, st);
        CHECK_FALSE(st.gimbal_pan_valid);
    }
}

TEST_CASE("zoom range follows the camera's zoom mode") {
    CHECK(orus::zoomMaxForMode(IMAGO_ZOOM_MODE_COMBINED) == 300.0f);
    CHECK(orus::zoomMaxForMode(IMAGO_ZOOM_MODE_SUPER_RESOLUTION) == 25.0f);
    CHECK(orus::zoomMaxForMode(IMAGO_ZOOM_MODE_UNKNOWN) == 0.0f);

    orus::Targets t;
    const auto read = orus::requestCameraParam(t, orus::param::kZoomMode);
    CHECK(read.target_component == MAV_COMP_ID_CAMERA2);
    CHECK(read.param_index == -1);
    CHECK(std::string(read.param_id) == "C_V_ZM_MODE");
    CHECK(orus::paramExtUint(orus::setCameraParam(t, "X", 0x01020304).param_value) == 0x01020304);

    PayloadModel model(3'000'000'000);
    std::vector<PayloadModel::Notice> notices;
    const int64_t t0 = 10'000'000'000;
    imago_state st{};
    model.snapshot(t0, st);
    CHECK(st.eo_zoom_mode == IMAGO_ZOOM_MODE_UNKNOWN);
    CHECK(st.eo_zoom_max == 0.0f);
    CHECK_FALSE(model.zoomModeKnown());

    // A level beyond the optical range is only possible in combined mode.
    mavlink_debug_t d{};
    d.ind = 0;
    d.value = 40;
    model.handle(fromPayload(26, [&](mavlink_message_t& m, mavlink_status_t& s, uint8_t c) {
                     mavlink_msg_debug_encode_status(1, c, &s, &m, &d);
                 }),
                 t0, notices);
    model.snapshot(t0, st);
    CHECK(st.eo_zoom_max == 300.0f);

    const auto camera = [](auto encode) { return fromPayload(MAV_COMP_ID_CAMERA2, encode); };
    mavlink_param_ext_value_t value{};
    std::strcpy(value.param_id, "C_V_ZM_MODE");
    value.param_value[0] = 2;
    model.handle(camera([&](mavlink_message_t& m, mavlink_status_t& s, uint8_t c) {
                     mavlink_msg_param_ext_value_encode_status(1, c, &s, &m, &value);
                 }),
                 t0, notices);
    model.snapshot(t0, st);
    CHECK(model.zoomModeKnown());
    CHECK(st.eo_zoom_mode == IMAGO_ZOOM_MODE_SUPER_RESOLUTION);
    CHECK(st.eo_zoom_max == 25.0f);

    // A rejected change leaves the mode alone; an accepted one applies.
    mavlink_param_ext_ack_t ack{};
    std::strcpy(ack.param_id, "C_V_ZM_MODE");
    ack.param_result = PARAM_ACK_FAILED;
    const auto ackMessage = [&] {
        return camera([&](mavlink_message_t& m, mavlink_status_t& s, uint8_t c) {
            mavlink_msg_param_ext_ack_encode_status(1, c, &s, &m, &ack);
        });
    };
    model.handle(ackMessage(), t0, notices);
    model.snapshot(t0, st);
    CHECK(st.eo_zoom_mode == IMAGO_ZOOM_MODE_SUPER_RESOLUTION);
    ack.param_result = PARAM_ACK_ACCEPTED;
    model.handle(ackMessage(), t0, notices);
    model.snapshot(t0, st);
    CHECK(st.eo_zoom_mode == IMAGO_ZOOM_MODE_COMBINED);
    CHECK(st.eo_zoom_max == 300.0f);
}

TEST_CASE("web settings: form values from the payload's page, broken markup and all") {
    // Shaped like firmware 3.0.5's page (2026-10-16): repeated ids, a <head> in the body,
    // attributes in either quote style, a select whose first option is the default.
    const std::string html = R"html(<body><head></head><form id="setupForm" method="post">
        <select id="is0ju" name='IsoTherms_Status' onchange="toggleThreshold()">
          <option value="0">Disable</option><option value="1" selected>Enable</option></select>
        <input type="number" name="Threshold" min="0" max="550" value="35">
        <SELECT name=Measurement_Range><option value="0">-20~150</option>
          <option value="1">-20~550</option></SELECT>
        <input type="submit" name="submit" value="Apply"></form>
        <form id="setupForm"><input name="FFC_Period" value='2'>
        <input type="text" name="Distance" value="50"/></form></body>)html";
    const auto v = web::formValues(html);
    CHECK(v.at("IsoTherms_Status") == "1");
    CHECK(v.at("Threshold") == "35");
    CHECK(v.at("Measurement_Range") == "0"); // nothing selected: the first option
    CHECK(v.at("FFC_Period") == "2");
    CHECK(v.at("Distance") == "50");
    CHECK(v.count("submit") == 0); // the button is not a setting

    CHECK(web::isothermForm(true, 35, 0) == "IsoTherms_Status=1&Threshold=35&Measurement_Range=0&submit=Apply");
    CHECK(web::restartForm(false) == "Restart_Service=1");
    CHECK(web::restartForm(true) == "Reboot_Service=1");
    CHECK(web::formEncode("-20 ~150 C&x") == "-20+~150+C%26x");
}

TEST_CASE("media: the list page, names, kinds and times") {
    // The links Gremsy's SDK reads from /list-file, plus a download-only link, a duplicate,
    // a name with a space and the storage choice of payloads with an SD card.
    const std::string html = R"html(<ul>
        <li><a href="/download/EO_20261019_110233.jpg">EO</a>
            <a href="/delete/EO_20261019_110233.jpg" class="delete-link">[Delete]</a></li>
        <li><a href='/delete/calibration%20shot.jpg' class="delete-link">[Delete]</a></li>
        <li><a href="/download/VID_2026-10-19_11-05-00.mp4">VID</a></li>
        <li><a href="/static/style.css">x</a><a href="/delete/">empty</a></li></ul>
        <select id="storage-select"><option value="internal">Internal</option>
          <option value="sdcard" selected> SD Card </option></select>)html";
    const auto listing = web::parseMediaListing(html);
    CHECK_FALSE(listing.loginRequired);
    REQUIRE(listing.names.size() == 3);
    CHECK(listing.names[0] == "EO_20261019_110233.jpg");
    CHECK(listing.names[1] == "calibration shot.jpg");
    CHECK(listing.names[2] == "VID_2026-10-19_11-05-00.mp4");
    REQUIRE(listing.storages.size() == 2);
    CHECK(listing.storages[1].value == "sdcard");
    CHECK(listing.storages[1].label == "SD Card");
    CHECK(listing.currentStorage == 1);
    CHECK(web::parseMediaListing("<form action=\"/smb-login\">").loginRequired);
    CHECK(web::parseMediaListing("<select id=\"storage-select\"><option value=\"a\">A</option></select>")
              .storages.empty()); // one storage: nothing to choose

    CHECK(web::mediaKindOf("a.JPG") == web::MediaKind::Photo);
    CHECK(web::mediaKindOf("a.mp4") == web::MediaKind::Video);
    CHECK(web::mediaKindOf("a.tiff") == web::MediaKind::Radiometric);
    CHECK(web::mediaKindOf("notes") == web::MediaKind::Other);
    CHECK(web::isThermalName("IR_20261019.jpg"));
    CHECK(web::isThermalName("photo-ir.jpg"));
    CHECK_FALSE(web::isThermalName("FIRE_01.jpg"));

    const int64_t t = 1792407753; // 2026-10-19 11:02:33 UTC
    CHECK(web::timeFromName("EO_20261019_110233.jpg") == t);
    CHECK(web::timeFromName("20261019110233.jpg") == t);
    CHECK(web::timeFromName("VID_2026-10-19_11-02-33.mp4") == t);
    CHECK(web::timeFromName("IMG_0042.jpg") == -1);
    CHECK(web::timeFromName("EO_20261399_110233.jpg") == -1); // month 13
    CHECK(web::timeFromExif("2026:10:19 11:02:33") == t);
    // What the Orus L writes (field report 2026-10-20): local time with its offset.
    CHECK(web::timeFromExif("2026-10-19T05:02:33-0600") == t);
    CHECK(web::timeFromExif("2026-10-19T13:32:33+02:30") == t);
    CHECK(web::timeFromExif("2026-10-19T11:02:33Z") == t);
    CHECK(web::timeFromExif(std::string_view("2026-10-19T11:02:33\0", 20)) == t);
    CHECK(web::timeFromExif("2026-10-19 11:02:33") == -1);
    CHECK(web::timeFromHttpDate("Mon, 19 Oct 2026 11:02:33 GMT") == t);
    CHECK(web::timeFromHttpDate("garbage") == -1);

    // One shot, two cameras: the Orus L's names (field report 2026-10-20).
    const auto eo = web::shotOf("IMG_EO_205.jpeg");
    const auto ir = web::shotOf("IMG_IR_205.jpeg");
    CHECK(eo.camera == web::MediaCamera::Eo);
    CHECK(ir.camera == web::MediaCamera::Ir);
    CHECK(eo.key == ir.key);
    CHECK_FALSE(eo.key.empty());
    CHECK(web::shotOf("VID_IR_36.mp4").key == web::shotOf("VID_EO_36.mp4").key);
    CHECK(web::shotOf("VID_EO_36.mp4").key != web::shotOf("IMG_EO_36.jpeg").key);
    CHECK(web::shotOf("IMG_EO_205.jpeg").key != web::shotOf("IMG_EO_206.jpeg").key);
    CHECK(web::shotOf("EO_20261019_110233.jpg").key.empty());
    CHECK(web::shotOf("calibration shot.jpg").key.empty());
    CHECK(web::mediaKindOf("IMG_EO_205.jpeg") == web::MediaKind::Photo);

    CHECK(web::pathEncode("calibration shot.jpg") == "calibration%20shot.jpg");
    CHECK(web::percentDecode("a%20b%2") == "a b%2");
    CHECK(web::percentDecode("a+b") == "a+b");
}

TEST_CASE("media: EXIF thumbnail and capture time from the start of a JPEG") {
    // SOI, APP0, then APP1 Exif (little-endian TIFF): IFD0 with the Exif IFD pointer, the Exif
    // IFD with DateTimeOriginal, IFD1 with a 6-byte "thumbnail".
    std::vector<uint8_t> tiff = {'I', 'I', 42, 0, 8, 0, 0, 0};
    auto u16 = [&](uint32_t v) {
        tiff.push_back(static_cast<uint8_t>(v & 255));
        tiff.push_back(static_cast<uint8_t>((v >> 8) & 255));
    };
    auto u32 = [&](uint32_t v) { u16(v & 0xFFFF); u16(v >> 16); };
    auto entry = [&](uint32_t tag, uint32_t type, uint32_t count, uint32_t value) {
        u16(tag); u16(type); u32(count); u32(value);
    };
    // IFD0 at 8: 1 entry (18 bytes) -> Exif IFD at 26: 1 entry -> IFD1 at 44: 2 entries
    // (30 bytes) -> data at 74: date (20), thumbnail (6).
    u16(1); entry(0x8769, 4, 1, 26); u32(44);
    u16(1); entry(0x9003, 2, 20, 74); u32(0);
    u16(2); entry(0x0201, 4, 1, 94); entry(0x0202, 4, 1, 6); u32(0);
    for (const char c : std::string("2026:10:19 11:02:33")) tiff.push_back(static_cast<uint8_t>(c));
    tiff.push_back(0);
    for (const int b : {0xFF, 0xD8, 1, 2, 0xFF, 0xD9}) tiff.push_back(static_cast<uint8_t>(b));
    std::vector<uint8_t> jpeg = {0xFF, 0xD8, 0xFF, 0xE0, 0, 4, 0, 0};
    const size_t app1 = jpeg.size();
    const size_t length = 2 + 6 + tiff.size();
    jpeg.insert(jpeg.end(), {0xFF, 0xE1, static_cast<uint8_t>(length >> 8), static_cast<uint8_t>(length & 255),
                             'E', 'x', 'i', 'f', 0, 0});
    jpeg.insert(jpeg.end(), tiff.begin(), tiff.end());
    jpeg.insert(jpeg.end(), {0xFF, 0xDA, 0, 2, 0x55});

    const auto exif = web::readJpegExif(jpeg.data(), jpeg.size());
    CHECK(exif.isJpeg);
    REQUIRE(exif.hasThumbnail);
    CHECK(exif.thumbnailOffset == app1 + 10 + 94);
    CHECK(exif.thumbnailLength == 6);
    CHECK(jpeg[exif.thumbnailOffset] == 0xFF);
    CHECK(exif.captureTime == 1792407753);
    CHECK(exif.needed <= jpeg.size());

    // Cut inside the segment: asks for the whole of it.
    const auto partial = web::readJpegExif(jpeg.data(), app1 + 20);
    CHECK_FALSE(partial.hasThumbnail);
    CHECK(partial.needed == app1 + 2 + length);
    const uint8_t png[] = {0x89, 'P', 'N', 'G'};
    CHECK_FALSE(web::readJpegExif(png, sizeof(png)).isJpeg);
}

TEST_CASE("media: flights from capture times") {
    // Newest first; a gap over 30 min starts a flight; unknown times go last on their own.
    const std::vector<int64_t> times = {10000, 9000, 8000, 2000, 1500, -1, -1};
    const auto flights = web::groupIntoFlights(times, 1800);
    CHECK(flights == std::vector<uint32_t>{0, 0, 0, 1, 1, 2, 2});
    CHECK(web::groupIntoFlights({}, 1800).empty());
    CHECK(web::groupIntoFlights({-1}, 1800) == std::vector<uint32_t>{0});
}
