// The desktop app's own logic: input shaping, profiles, the controller mapping, the thermal bar,
// the payload's toasts and the Media window.

#include "control/GamepadMapping.h"
#include "control/GimbalInputShaper.h"
#include "core/ConnectionProfile.h"
#include "core/MarkedPoints.h"
#include "core/MediaLibrary.h"
#include "core/PayloadMessages.h"
#include "core/Thermal.h"

#include <doctest.h>

#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstring>

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
    // Zoom compensation: none keeps the °/s, full keeps the pace on screen.
    GimbalInputShaper noCompensation;
    noCompensation.zoomCompensation = 0.0f;
    CHECK(noCompensation.rates(1.0f, 0.0f, 16.0f).yaw == doctest::Approx(60.0f));
    GimbalInputShaper fullCompensation;
    fullCompensation.zoomCompensation = 1.0f;
    CHECK(fullCompensation.rates(1.0f, 0.0f, 16.0f).yaw == doctest::Approx(3.75f));
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
    CHECK(shaper.set(QStringLiteral("zoomCompensation"), 1.5));
    CHECK(shaper.zoomCompensation == 1.0f);
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

namespace {

/// UTC seconds of a local time, as the payload's capture times are shown.
qint64 localTime(int y, int mo, int d, int h, int mi, int s = 0) {
    return QDateTime(QDate(y, mo, d), QTime(h, mi, s)).toSecsSinceEpoch();
}

} // namespace

TEST_CASE("media: texts of a flight") {
    Media::Flight flight;
    flight.start = localTime(2026, 10, 19, 11, 2, 30);
    flight.end = localTime(2026, 10, 19, 11, 33);
    CHECK(Media::flightDateText(flight) == QStringLiteral("Oct 19"));
    CHECK(Media::flightTimeText(flight) == QStringLiteral("11:02 - 11:33"));
    CHECK(Media::flightFolderName(flight) == QStringLiteral("2026-10-19 11-02"));
    CHECK(Media::captureTimeText(flight.start) == QStringLiteral("11:02:30"));
    CHECK(Media::captureTimeText(-1).isEmpty());

    const Media::Flight untimed;
    CHECK(Media::flightDateText(untimed) == QStringLiteral("No capture time"));
    CHECK(Media::flightTimeText(untimed).isEmpty());
    CHECK(Media::flightFolderName(untimed) == QStringLiteral("No capture time"));

    CHECK(Media::countsText(22, 2) == QStringLiteral("22 photos · 2 videos"));
    CHECK(Media::countsText(1, 0) == QStringLiteral("1 photo"));
    CHECK(Media::countsText(0, 1) == QStringLiteral("1 video"));
    CHECK(Media::countsText(0, 0) == QStringLiteral("0 photos"));
}

TEST_CASE("media: sizes, storage and speed") {
    CHECK(Media::sizeText(-1).isEmpty());
    CHECK(Media::sizeText(0) == QStringLiteral("0.0 MB"));
    CHECK(Media::sizeText(2000) == QStringLiteral("0.1 MB")); // never 0.0 for a file with bytes
    CHECK(Media::sizeText(qint64(18.2 * 1024 * 1024)) == QStringLiteral("18.2 MB"));
    CHECK(Media::sizeText(qint64(1.4 * 1024 * 1024 * 1024)) == QStringLiteral("1.4 GB"));
    CHECK(Media::storageText(18.2 * 1024, 64 * 1024) == QStringLiteral("18.2 of 64.0 GB"));
    CHECK(Media::rateText(1.24 * 1024 * 1024) == QStringLiteral("1.2 MB/s"));
}

TEST_CASE("media: download paths stay in their flight's folder") {
    Media::Flight flight;
    flight.start = localTime(2026, 10, 19, 11, 2);
    const QString root = QDir::cleanPath(QDir::tempPath() + QStringLiteral("/m"));
    CHECK(Media::downloadPath(root, flight, QStringLiteral("DSC_0001.JPG")) ==
          root + QStringLiteral("/2026-10-19 11-02/DSC_0001.JPG"));
    CHECK(Media::downloadPath(root, flight, QStringLiteral("../x.jpg")) ==
          root + QStringLiteral("/2026-10-19 11-02/.._x.jpg"));
    CHECK(Media::downloadPath(root, Media::Flight{}, QStringLiteral("a.jpg")) ==
          root + QStringLiteral("/No capture time/a.jpg"));
}

TEST_CASE("media: timeline positions and the nearest capture") {
    Media::Flight flight;
    flight.start = 1000;
    flight.end = 2000;
    CHECK(Media::timelineFraction(1000, flight) == doctest::Approx(0.0));
    CHECK(Media::timelineFraction(1250, flight) == doctest::Approx(0.25));
    CHECK(Media::timelineFraction(5000, flight) == doctest::Approx(1.0));
    CHECK(Media::timelineFraction(-1, flight) < 0);
    Media::Flight moment;
    moment.start = moment.end = 1000;
    CHECK(Media::timelineFraction(1000, moment) == doctest::Approx(0.5));

    // Oldest first, as the sheet shows them.
    const QList<qint64> times{1000, 1300, 1700, 2000};
    CHECK(Media::nearestIndex(times, flight, 0.0) == 0);
    CHECK(Media::nearestIndex(times, flight, 0.68) == 2);
    CHECK(Media::nearestIndex(times, flight, 1.0) == 3);
    CHECK(Media::nearestIndex({}, flight, 0.5) == -1);
}

TEST_CASE("media: the sheet reads oldest first, untimed files last") {
    // The core lists a flight newest first; files 0 and 5 belong to other flights.
    std::vector<Media::File> files(6);
    const qint64 times[] = {9000, 2000, -1, 1700, 1000, 50};
    for (int i = 0; i < 6; ++i) {
        files[i].name = QString::number(i);
        files[i].time = times[i];
    }
    Media::Flight flight;
    flight.firstFile = 1;
    flight.fileCount = 4;
    CHECK(Media::sheetOrder(files, flight) == QList<int>{4, 3, 1, 2});
    flight.fileCount = 10; // past the list: only what exists
    CHECK(Media::sheetOrder(files, flight).size() == 5);
}

TEST_CASE("media: one cell per shot, which stands for both files") {
    // The Orus L saves IMG_EO_<n> and IMG_IR_<n> per shot (field report 2026-10-20); the core
    // marks the EO file primary and names the other as its pair.
    imago_media_file eo{};
    std::strcpy(eo.name, "IMG_EO_205.jpeg");
    std::strcpy(eo.pair, "IMG_IR_205.jpeg");
    eo.primary = true;
    eo.camera = IMAGO_MEDIA_CAMERA_EO;
    eo.capture_time = 1100;
    eo.thumbnail = true;
    eo.thumbnail_from_pair = true;
    imago_media_file ir = eo;
    std::strcpy(ir.name, "IMG_IR_205.jpeg");
    std::strcpy(ir.pair, "IMG_EO_205.jpeg");
    ir.primary = false;
    ir.camera = IMAGO_MEDIA_CAMERA_IR;
    ir.thumbnail_from_pair = false;
    imago_media_file single{};
    std::strcpy(single.name, "IMG_IR_9.jpeg");
    single.primary = true;
    single.capture_time = 1000;

    const std::vector<Media::File> files{Media::File::from(eo), Media::File::from(ir), Media::File::from(single)};
    CHECK(files[0].hasPair());
    CHECK(files[0].thumbnailFromPair);
    CHECK(files[0].camera == IMAGO_MEDIA_CAMERA_EO);
    CHECK(files[0].shotNames() == QStringList{QStringLiteral("IMG_EO_205.jpeg"), QStringLiteral("IMG_IR_205.jpeg")});
    CHECK(files[2].shotNames() == QStringList{QStringLiteral("IMG_IR_9.jpeg")});
    Media::Flight flight;
    flight.fileCount = 3;
    CHECK(Media::sheetOrder(files, flight) == QList<int>{2, 0}); // the IR twin has no cell
}

TEST_CASE("media: filter") {
    Media::File photo;
    photo.kind = IMAGO_MEDIA_PHOTO;
    Media::File tiff;
    tiff.kind = IMAGO_MEDIA_RADIOMETRIC;
    Media::File video;
    video.kind = IMAGO_MEDIA_VIDEO;
    CHECK(Media::matches(tiff, Media::PhotosOnly));
    CHECK_FALSE(Media::matches(video, Media::PhotosOnly));
    CHECK(Media::matches(video, Media::VideosOnly));
    CHECK_FALSE(Media::matches(photo, Media::VideosOnly));
    CHECK(Media::matches(photo, Media::AllFiles));
}

TEST_CASE("media: click, Ctrl-click and Shift-click selection") {
    const QStringList names{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d"),
                            QStringLiteral("e")};
    Media::Selection s;
    s.click(names, 1, false, false);
    CHECK(s.ordered(names) == QStringList{QStringLiteral("b")});
    s.click(names, 3, false, true); // b..d
    CHECK(s.ordered(names) == QStringList{QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")});
    s.click(names, 0, false, true); // the anchor stays at b: a..b
    CHECK(s.ordered(names) == QStringList{QStringLiteral("a"), QStringLiteral("b")});
    s.click(names, 4, true, false); // Ctrl adds e and moves the anchor
    CHECK(s.count() == 3);
    s.click(names, 4, true, false); // and takes it away again
    CHECK(s.count() == 2);
    s.click(names, 2, true, true); // Ctrl + Shift adds the range e..c
    CHECK(s.ordered(names) == QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"),
                                          QStringLiteral("d"), QStringLiteral("e")});
    s.retain({QStringLiteral("a"), QStringLiteral("c")});
    CHECK(s.count() == 2);
    s.selectAll(names);
    CHECK(s.count() == 5);
    s.clear();
    CHECK(s.isEmpty());
    s.click(names, 9, false, false); // out of range: ignored
    CHECK(s.isEmpty());
}

TEST_CASE("media: download batch") {
    Media::Batch batch;
    CHECK_FALSE(batch.running());
    batch.total = 4;
    batch.totalBytes = 400;
    CHECK(batch.running());
    CHECK(batch.runningTitle() == QStringLiteral("Downloading 1 of 4"));
    batch.done = 2;
    batch.finishedBytes = 200;
    CHECK(batch.fraction(50) == doctest::Approx(0.625));
    CHECK(batch.runningTitle() == QStringLiteral("Downloading 3 of 4"));
    batch.failed = 1;
    batch.cancelled = 1;
    CHECK_FALSE(batch.running());
    CHECK(batch.finishedTitle() == QStringLiteral("2 downloaded"));
    CHECK(batch.finishedDetail() == QStringLiteral("1 failed · 1 cancelled"));

    Media::Batch unknownSizes;
    unknownSizes.total = 4;
    unknownSizes.done = 1;
    CHECK(unknownSizes.fraction(0) == doctest::Approx(0.25)); // by count
    unknownSizes.done = 0;
    unknownSizes.failed = 4;
    CHECK(unknownSizes.finishedTitle() == QStringLiteral("Nothing downloaded"));
}
