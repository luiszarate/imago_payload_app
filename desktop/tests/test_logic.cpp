// The desktop app's own logic: input shaping, profiles, the controller mapping, the thermal bar
// and the payload's toasts.

#include "control/GamepadMapping.h"
#include "control/GimbalInputShaper.h"
#include "core/ConnectionProfile.h"
#include "core/MarkedPoints.h"
#include "core/PayloadMessages.h"
#include "core/Thermal.h"

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

TEST_CASE("thermal: palette names and colours from the core") {
    for (int i = 0; i < IMAGO_IR_PALETTE_COUNT; ++i) {
        CHECK_FALSE(Thermal::paletteName(-1, i).isEmpty());
        const QStringList colors = Thermal::paletteColors(-1, i);
        CHECK(colors.size() >= 2);
        CHECK(colors.size() <= 8);
        for (const QString& color : colors) {
            CHECK(color.size() == 7);
            CHECK(color.startsWith(QLatin1Char('#')));
        }
    }
    CHECK(Thermal::paletteName(-1, IMAGO_IR_PALETTE_COUNT) == QStringLiteral("Palette 11"));
    CHECK(Thermal::paletteColors(-1, -1) == QStringList{QStringLiteral("#000000"), QStringLiteral("#ffffff")});
}

TEST_CASE("thermal: isotherm band limits and steppers") {
    CHECK(Thermal::isValid({35, 40}));
    CHECK(Thermal::isValid({0, 550}));
    CHECK_FALSE(Thermal::isValid({40, 40}));
    CHECK_FALSE(Thermal::isValid({-1, 40}));
    CHECK_FALSE(Thermal::isValid({100, 551}));
    for (const Thermal::Preset& preset : Thermal::presets()) {
        CHECK(Thermal::isValid(preset.band));
    }
    // Fire needs low gain.
    CHECK(Thermal::presets().last().band.high > Thermal::kHighGainMaxC);
    CHECK(Thermal::clampLow(45, 40) == 39);
    CHECK(Thermal::clampLow(-5, 40) == 0);
    CHECK(Thermal::clampHigh(30, 35) == 36);
    CHECK(Thermal::clampHigh(600, 35) == 550);
}

TEST_CASE("thermal: last band remembered, invalid ones ignored") {
    QTemporaryDir dir;
    QSettings settings(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
    CHECK(Thermal::rememberedBand(settings) == Thermal::Band{35, 40});
    Thermal::rememberBand(settings, {100, 150});
    CHECK(Thermal::rememberedBand(settings) == Thermal::Band{100, 150});
    Thermal::rememberBand(settings, {200, 150});
    CHECK(Thermal::rememberedBand(settings) == Thermal::Band{35, 40});
}

TEST_CASE("thermal: settings from the camera values, else the data stream") {
    const Thermal::Band remembered{100, 150};
    QVariantMap values;
    Thermal::Settings s = Thermal::settings(values, -1, 0, remembered);
    CHECK(s.palette == -1);
    CHECK(s.zoom == 0);
    CHECK_FALSE(s.isothermSupported);
    CHECK(s.lowGain == -1);
    CHECK(s.band == remembered);

    s = Thermal::settings(values, 3, 2, remembered);
    CHECK(s.palette == 3);
    CHECK(s.zoom == 2);

    values = {{QStringLiteral("C_T_PALETTE"), 5}, {QStringLiteral("C_T_ZOOM"), 3}, {QStringLiteral("C_T_ISO_M"), 0},
              {QStringLiteral("C_T_G"), IMAGO_IR_GAIN_LOW}, {QStringLiteral("C_T_RG0_TMP"), 35},
              {QStringLiteral("C_T_RG1_TMP"), 40}};
    s = Thermal::settings(values, 3, 2, remembered);
    CHECK(s.palette == 5);
    CHECK(s.zoom == 4);
    CHECK(s.isothermSupported);
    CHECK_FALSE(s.isothermOn);
    CHECK(s.lowGain == 1);
    // Off: the band applied here, not the camera's.
    CHECK(s.band == remembered);

    values.insert(QStringLiteral("C_T_ISO_M"), 1);
    s = Thermal::settings(values, 3, 2, remembered);
    CHECK(s.isothermOn);
    CHECK(s.band == Thermal::Band{35, 40});
    // A band the camera holds that makes no sense falls back too.
    values.insert(QStringLiteral("C_T_RG1_TMP"), 20);
    CHECK(Thermal::settings(values, 3, 2, remembered).band == remembered);
}

TEST_CASE("thermal: data stream changes go into the camera values") {
    QVariantMap values{{QStringLiteral("C_T_PALETTE"), 1}};
    CHECK_FALSE(Thermal::followStream(values, 1, 1, 0, 0));
    CHECK_FALSE(Thermal::followStream(values, 1, -1, 0, 0));
    CHECK(Thermal::followStream(values, 1, 4, 0, 0));
    CHECK(values.value(QStringLiteral("C_T_PALETTE")).toInt() == 4);
    CHECK_FALSE(values.contains(QStringLiteral("C_T_ZOOM")));
    CHECK(Thermal::followStream(values, 4, 4, 0, 3));
    CHECK(values.value(QStringLiteral("C_T_ZOOM")).toInt() == 2);
    CHECK(Thermal::settingsToRead().contains(QStringLiteral("C_T_ISO_M")));
}

TEST_CASE("thermal: temperature scale") {
    CHECK(Thermal::fraction(25, 20, 30).value() == doctest::Approx(0.5));
    CHECK(Thermal::fraction(10, 20, 30).value() == 0.0);
    CHECK(Thermal::fraction(40, 20, 30).value() == 1.0);
    CHECK_FALSE(Thermal::fraction(20, 20, 20).has_value());

    const auto span = Thermal::bandSpan({35, 40}, 20, 40);
    REQUIRE(span.has_value());
    CHECK(span->from == doctest::Approx(0.75));
    CHECK(span->to == doctest::Approx(1.0));
    // Partly outside: clamped to the scale; wholly outside: not drawn.
    CHECK(Thermal::bandSpan({10, 25}, 20, 40)->from == 0.0);
    CHECK_FALSE(Thermal::bandSpan({100, 150}, 20, 40).has_value());
    CHECK_FALSE(Thermal::bandSpan({35, 40}, 40, 60).has_value());
    CHECK_FALSE(Thermal::bandSpan({35, 40}, 30, 30).has_value());

    CHECK(Thermal::celsius(36.54) == QStringLiteral("36.5°"));
    CHECK(Thermal::celsius(-4.0) == QStringLiteral("-4.0°"));
    CHECK(Thermal::celsius(120.4) == QStringLiteral("120°"));
}

TEST_CASE("messages: setting names, ignored tracking") {
    CHECK(PayloadMessages::paramName(QStringLiteral("C_SOURCE")) == QStringLiteral("View"));
    CHECK(PayloadMessages::paramName(QStringLiteral("C_T_G")) == QStringLiteral("IR gain"));
    CHECK(PayloadMessages::paramName(QStringLiteral("C_T_ISO_M")) == QStringLiteral("Isotherm"));
    CHECK(PayloadMessages::paramName(QStringLiteral("C_T_RG1_TMP")) == QStringLiteral("Isotherm"));
    CHECK(PayloadMessages::paramName(QStringLiteral("C_V_FLIP")) == QStringLiteral("C_V_FLIP"));

    CHECK(PayloadMessages::trackingIgnored(QStringLiteral("stop"), IMAGO_TRACKING_TRACKED) ==
          QStringLiteral("Payload accepted Stop but is still tracking. Try changing the view, or restart the camera service in Settings."));
    CHECK(PayloadMessages::trackingIgnored(QStringLiteral("track"), IMAGO_TRACKING_LOST) ==
          QStringLiteral("Payload accepted the point but did not track it (target lost). Try changing the view, or restart the camera service in Settings."));
    CHECK(PayloadMessages::trackingIgnored(QStringLiteral("track"), IMAGO_TRACKING_IDLE).contains(QStringLiteral("(idle)")));
}

TEST_CASE("messages: pan near its limit, once per approach") {
    PanLimitWarning warning;
    CHECK_FALSE(warning.update(false, true, 100.0f).has_value());
    const auto text = warning.update(true, true, 295.4f);
    REQUIRE(text.has_value());
    CHECK(*text == QStringLiteral("Gimbal pan +295°: near its ±320° limit"));
    // Still near: no repeat.
    CHECK_FALSE(warning.update(true, true, 300.0f).has_value());
    CHECK_FALSE(warning.update(false, true, 250.0f).has_value());
    CHECK(warning.update(true, true, -292.0f).value() == QStringLiteral("Gimbal pan -292°: near its ±320° limit"));
    // A new connection warns again.
    warning.reset();
    CHECK(warning.update(true, true, 291.0f).has_value());
}

TEST_CASE("messages: web settings failures") {
    CHECK(PayloadMessages::webFailed(WebRequest::Save, 500, QString()) ==
          QStringLiteral("Saving the payload's web settings failed: code 500"));
    CHECK(PayloadMessages::webFailed(WebRequest::Read, 0, QStringLiteral("no answer")) ==
          QStringLiteral("Reading the payload's web settings failed: no answer"));
}

TEST_CASE("marked points: names, rounding and the files") {
    const TargetPosition target = MarkedPoints::rounded(22.169844, -101.053016, 1843.6f);
    CHECK(target.coordinateText() == QStringLiteral("22.16984, -101.05302"));
    CHECK(target.altitude == doctest::Approx(1844.0f));
    CHECK(target.mapsUrl().toString() == QStringLiteral("https://www.google.com/maps/search/?api=1&query=22.16984,-101.05302"));

    const QDateTime time = QDateTime::fromString(QStringLiteral("2026-10-19T15:04:05Z"), Qt::ISODate);
    QList<MarkedPoint> points;
    points.append(MarkedPoints::make(points, target, time));
    points.append(MarkedPoints::make(points, target, time));
    CHECK(points[1].name == QStringLiteral("Point 2"));
    points.removeFirst();
    // Numbers keep counting from the highest left, never reuse a later one.
    CHECK(MarkedPoints::make(points, target, time).name == QStringLiteral("Point 3"));

    const QList<MarkedPoint> back = MarkedPoints::fromJson(MarkedPoints::toJson(points));
    REQUIRE(back.size() == 1);
    CHECK(back[0] == points[0]);
    CHECK(MarkedPoints::fromJson("not json").isEmpty());

    points[0].name = QStringLiteral("A, <b> & c");
    CHECK(MarkedPoints::toCsv(points) == QStringLiteral("name,time_utc,latitude,longitude,altitude_m\n"
                                                        "A  <b> & c,2026-10-19T15:04:05Z,22.16984,-101.05302,1844\n"));
    const QString kml = MarkedPoints::toKml(points);
    CHECK(kml.contains(QStringLiteral("<name>A, &lt;b> &amp; c</name>")));
    CHECK(kml.contains(QStringLiteral("<coordinates>-101.05302,22.16984,1844</coordinates>")));
}
