// The desktop app's own logic: input shaping, profiles and the controller mapping.

#include "control/GamepadMapping.h"
#include "control/GimbalInputShaper.h"
#include "core/ConnectionProfile.h"

#include <doctest.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

TEST_CASE("shaper: dead zone, full deflection and zoom") {
    const GimbalInputShaper shaper;
    CHECK(shaper.shape(0.05f) == 0.0f);
    CHECK(shaper.shape(1.0f) == doctest::Approx(1.0f));
    CHECK(shaper.shape(-1.0f) == doctest::Approx(-1.0f));
    CHECK(shaper.shape(2.0f) == doctest::Approx(1.0f)); // clamped
    // Expo: finer than linear near the centre.
    CHECK(shaper.shape(0.5f) < 0.5f);

    const auto full = shaper.rates(1.0f, 1.0f, 1.0f);
    CHECK(full.yaw == doctest::Approx(60.0f));
    CHECK(full.pitch == doctest::Approx(60.0f));
    // At 4x the rate halves, so the picture moves at about the same speed.
    const auto zoomed = shaper.rates(1.0f, 0.0f, 4.0f);
    CHECK(zoomed.yaw == doctest::Approx(30.0f));
    // Digital inputs are on or off at their own speed.
    const auto digital = shaper.rates(-0.7f, 0.3f, 1.0f, true, true);
    CHECK(digital.yaw == doctest::Approx(-20.0f));
    CHECK(digital.pitch == 0.0f);
}

TEST_CASE("shaper: fields by name are clamped and persisted") {
    QTemporaryDir dir;
    QSettings settings(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
    GimbalInputShaper shaper;
    CHECK(shaper.set(QStringLiteral("maxYawDps"), 500.0));
    CHECK(shaper.maxYawDps == GimbalInputShaper::kMaxSpeed);
    CHECK(shaper.set(QStringLiteral("deadzone"), 0.12));
    CHECK_FALSE(shaper.set(QStringLiteral("nonsense"), 1.0));
    shaper.save(settings);
    CHECK(GimbalInputShaper::load(settings) == shaper);
}

TEST_CASE("profile: JSON matches the mobile apps") {
    const QByteArray json = R"({"id":"A1","name":"Field","mavlinkMode":"listen","payloadIP":"",
        "mavlinkPort":14566,"localPort":14550,"videoURL":"rtsp://192.168.43.1:8554/fpv_stream",
        "videoTransport":"tcp","gimbalProtocol":"manager","setStandardRCMode":true,
        "maxVideoLagMs":300,"keepWifiAwake":true,"videoSmoothingMs":100,"future":1})";
    const ConnectionProfile p = ConnectionProfile::fromJson(QJsonDocument::fromJson(json).object());
    CHECK(p.id == QStringLiteral("A1"));
    CHECK(p.mavlinkMode == ConnectionProfile::MavlinkMode::Listen);
    CHECK(p.videoTransport == ConnectionProfile::VideoTransport::Tcp);
    CHECK(p.gimbalProtocol == ConnectionProfile::GimbalProtocol::Manager);
    CHECK(p.setStandardRCMode);
    CHECK(p.maxVideoLagMs == 300);
    CHECK(p.keepWifiAwake);
    CHECK(p.videoSmoothingMs == 100);
    CHECK(p.validationError().isEmpty());
    CHECK(ConnectionProfile::fromJson(p.toJson()) == p);
}

TEST_CASE("profile: missing and unknown values take the defaults") {
    const QByteArray json = R"({"id":"B","name":"Old","videoTransport":"carrier-pigeon","wifiKeepAwake":true})";
    const ConnectionProfile p = ConnectionProfile::fromJson(QJsonDocument::fromJson(json).object());
    CHECK(p.videoTransport == ConnectionProfile::VideoTransport::Auto);
    CHECK(p.mavlinkPort == 14566);
    // The first version's key stalled the Herelink and is ignored on purpose.
    CHECK_FALSE(p.keepWifiAwake);
    CHECK(p.videoSmoothingMs == 0);
}

TEST_CASE("profile: validation") {
    ConnectionProfile p = ConnectionProfile::direct();
    CHECK(p.validationError().isEmpty());
    p.payloadIP = QStringLiteral("192.168.1.300");
    CHECK_FALSE(p.validationError().isEmpty());
    p.payloadIP = QStringLiteral("10.0.0.1");
    p.videoURL = QStringLiteral("http://camera");
    CHECK_FALSE(p.validationError().isEmpty());
    p.videoURL.clear();
    p.mavlinkMode = ConnectionProfile::MavlinkMode::Listen;
    CHECK(p.validationError() == QStringLiteral("Listen mode needs a video URL."));
    CHECK(ConnectionProfile::isIPv4(QStringLiteral(" 192.168.144.240 ")));
    CHECK_FALSE(ConnectionProfile::isIPv4(QStringLiteral("192.168.144")));
}

TEST_CASE("profile store: defaults once, then what was saved") {
    QTemporaryDir dir;
    QSettings settings(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
    ProfileStore store(settings);
    QList<ConnectionProfile> profiles = store.load();
    REQUIRE(profiles.size() == 3);
    CHECK(profiles[2].payloadIP == QStringLiteral("127.0.0.1"));
    profiles.removeAt(0);
    store.save(profiles);
    store.setSelectedId(profiles[0].id);
    CHECK(store.load() == profiles);
    CHECK(store.selectedId() == profiles[0].id);
}

TEST_CASE("mapping: one button per action, one action per button") {
    GamepadMapping m = GamepadMapping::standard();
    CHECK(m.button(GamepadAction::Shutter) == QStringLiteral("pad:a"));
    // Moving the shutter to B takes B from "record".
    m = m.bound(GamepadAction::Shutter, QStringLiteral("pad:b"));
    CHECK(m.button(GamepadAction::Shutter) == QStringLiteral("pad:b"));
    CHECK(m.button(GamepadAction::Record).isEmpty());
    CHECK_FALSE(m.buttons.contains(QStringLiteral("pad:a")));
}

TEST_CASE("mapping: one function per axis, persisted") {
    QTemporaryDir dir;
    QSettings settings(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
    GamepadMapping m = GamepadMapping::standard();
    m = m.bound(GamepadAxisFunction::Zoom, QStringLiteral("pad:leftx"), false);
    CHECK_FALSE(m.axes.contains(GamepadAxisFunction::GimbalYaw));
    m = m.withInverted(GamepadAxisFunction::Zoom, true);
    m = m.bound(GamepadAction::ZoomIn, QStringLiteral("joy:button3"));
    m.save(settings);
    const GamepadMapping loaded = GamepadMapping::load(settings);
    CHECK(loaded == m);
    CHECK(loaded.axes.value(GamepadAxisFunction::Zoom).inverted);
}

TEST_CASE("input labels") {
    CHECK(GamepadInput::label(QStringLiteral("pad:lefty")) == QStringLiteral("Left stick ↑↓"));
    CHECK(GamepadInput::label(QStringLiteral("pad:paddle2")) == QStringLiteral("Paddle 2"));
    CHECK(GamepadInput::label(QStringLiteral("joy:axis2")) == QStringLiteral("Axis 3"));
    CHECK(GamepadInput::label(QStringLiteral("joy:button0")) == QStringLiteral("Button 1"));
    CHECK(GamepadInput::label(QStringLiteral("joy:hat0y")) == QStringLiteral("Hat 1 ↑↓"));
    CHECK(GamepadInput::isDigitalAxis(QStringLiteral("joy:hat1x")));
    CHECK(GamepadInput::isDigitalAxis(QStringLiteral("pad:dpady")));
    CHECK_FALSE(GamepadInput::isDigitalAxis(QStringLiteral("pad:leftx")));
}
