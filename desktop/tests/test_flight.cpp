// What the desktop app took over from the Quest: the map's tile math and settings, the AI
// button's timing, the aircraft's figures, the payload's own stream and the profile's aircraft
// telemetry switch.

#include "core/AiDetection.h"
#include "core/Aircraft.h"
#include "core/ConnectionProfile.h"
#include "core/MapTiles.h"
#include "core/StreamSettings.h"

#include <doctest.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>

// MARK: Map tiles (the cases of the Quest's MapTilesTest.kt)

TEST_CASE("tiles: the origin and the corners of the world") {
    CHECK(TileMath::x(-180.0, 0) == doctest::Approx(0.0));
    CHECK(TileMath::x(0.0, 1) == doctest::Approx(1.0));
    CHECK(TileMath::x(180.0, 2) == doctest::Approx(4.0));
    CHECK(TileMath::y(0.0, 1) == doctest::Approx(1.0));
    CHECK(TileMath::y(85.05, 3) == doctest::Approx(0.0).epsilon(0.001));
    // Beyond Mercator's edge it stays on the map.
    CHECK(TileMath::y(89.9, 3) == doctest::Approx(TileMath::y(85.05, 3)));
}

TEST_CASE("tiles: a known tile, and back from tile coordinates") {
    // Mexico City at zoom 15, as on the Quest (the standard slippy-map formula).
    const double lat = 19.4326;
    const double lon = -99.1332;
    CHECK(static_cast<int>(std::floor(TileMath::x(lon, 15))) == 7360);
    CHECK(static_cast<int>(std::floor(TileMath::y(lat, 15))) == 14580);
    // The whole world is one tile at zoom 0, and Greenwich on the equator is its centre.
    CHECK(TileMath::x(0.0, 0) == doctest::Approx(0.5));
    CHECK(TileMath::y(0.0, 0) == doctest::Approx(0.5));
    // A pixel at zoom 16 there is a little over two metres.
    CHECK(TileMath::metresPerPixel(lat, 16) == doctest::Approx(2.25).epsilon(0.03));
    CHECK(TileMath::lon(TileMath::x(lon, 16), 16) == doctest::Approx(lon).epsilon(1e-9));
    CHECK(TileMath::lat(TileMath::y(lat, 16), 16) == doctest::Approx(lat).epsilon(1e-9));
}

TEST_CASE("tiles: ground resolution halves with each zoom") {
    CHECK(TileMath::metresPerPixel(0.0, 0) == doctest::Approx(156543.03392));
    CHECK(TileMath::metresPerPixel(0.0, 16) == doctest::Approx(2.3887).epsilon(0.001));
    CHECK(TileMath::metresPerPixel(60.0, 16) == doctest::Approx(TileMath::metresPerPixel(0.0, 16) / 2).epsilon(0.001));
}

TEST_CASE("tiles: columns wrap around the antimeridian") {
    CHECK(TileMath::wrapColumn(-1, 2) == 3);
    CHECK(TileMath::wrapColumn(4, 2) == 0);
    CHECK(TileMath::wrapColumn(2, 2) == 2);
}

TEST_CASE("tiles: an area covers every zoom, without repeats") {
    const QList<TileId> tiles = TileMath::around(19.4326, -99.1332, 1.0, 11, 13);
    REQUIRE_FALSE(tiles.isEmpty());
    std::set<std::tuple<int, int, int>> unique;
    for (const TileId& t : tiles) {
        unique.insert({t.z, t.x, t.y});
        CHECK(t.z >= 11);
        CHECK(t.z <= 13);
    }
    CHECK(unique.size() == static_cast<size_t>(tiles.size()));
    // The centre tile of each zoom is in.
    for (int z = 11; z <= 13; ++z) {
        const TileId centre{z, static_cast<int>(std::floor(TileMath::x(-99.1332, z))),
                            static_cast<int>(std::floor(TileMath::y(19.4326, z)))};
        CHECK(std::find(tiles.begin(), tiles.end(), centre) != tiles.end());
    }
    // A larger radius covers more. 3 km: a handful of tiles far out, a hundred or so close in.
    CHECK(TileMath::around(19.4326, -99.1332, 3.0, 16, 16).size() > TileMath::around(19.4326, -99.1332, 1.0, 16, 16).size());
    CHECK(TileMath::around(19.4326, -99.1332, 3.0, 11, 11).size() <= 4);
    const auto near = TileMath::around(19.4326, -99.1332, 3.0, 16, 16).size();
    CHECK(near >= 100);
    CHECK(near <= 200);
}

TEST_CASE("tiles: a large area drops the closest zooms") {
    const TileMath::DownloadPlan small = TileMath::downloadPlan(19.4326, -99.1332, 1.0);
    CHECK(small.topZoom == 16);
    const TileMath::DownloadPlan detail = TileMath::downloadPlan(19.4326, -99.1332, 1.0, true);
    CHECK(detail.topZoom == 17);
    CHECK(detail.tiles.size() > 2 * small.tiles.size()); // zoom 17 is most of a download
    CHECK(small.tiles.size() <= TileMath::kMaxDownload);
    const TileMath::DownloadPlan large = TileMath::downloadPlan(19.4326, -99.1332, 15.0);
    CHECK(large.topZoom < TileMath::kDownloadMaxZoom);
    CHECK(large.topZoom >= TileMath::kDownloadLowestTop);
    CHECK(large.tiles.size() <= TileMath::kMaxDownload);
}

TEST_CASE("tiles: URL templates and the store's paths") {
    const TileId id{15, 7360, 14580};
    CHECK(TileMath::url(QStringLiteral("https://t/{z}/{x}/{y}.png"), id) == QStringLiteral("https://t/15/7360/14580.png"));
    // Esri's order is row then column.
    CHECK(TileMath::url(MapSettings().satelliteUrl, id).endsWith(QStringLiteral("/tile/15/14580/7360")));
    CHECK(TileMath::relativePath(QStringLiteral("street"), id) == QStringLiteral("street/15/7360/14580"));
}

TEST_CASE("map settings: Quest's defaults, kept and clamped") {
    QTemporaryDir dir;
    QSettings settings(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
    const MapSettings defaults = MapSettings::load(settings);
    CHECK(defaults == MapSettings());
    CHECK_FALSE(defaults.downloadDetail); // 11 to 16 unless asked
    CHECK(defaults.visible);
    CHECK(defaults.satellite());
    CHECK(defaults.zoom == 16);
    CHECK(defaults.streetUrl == QStringLiteral("https://tile.openstreetmap.org/{z}/{x}/{y}.png"));
    CHECK(defaults.streetCredit == QStringLiteral("© OpenStreetMap contributors"));
    CHECK(defaults.satelliteCredit == QStringLiteral("Esri, Maxar, Earthstar Geographics"));

    MapSettings changed;
    changed.visible = false;
    changed.style = MapSettings::kStreet;
    changed.zoom = 12;
    changed.lastLat = 19.5;
    changed.lastLon = -99.25;
    changed.downloadDetail = true;
    changed.save(settings);
    CHECK(MapSettings::load(settings) == changed);
    CHECK(MapSettings::load(settings).credit() == changed.streetCredit);

    settings.setValue(QStringLiteral("map/zoom"), 25);
    settings.setValue(QStringLiteral("map/style"), QStringLiteral("moon"));
    settings.setValue(QStringLiteral("map/streetUrl"), QStringLiteral("  "));
    const MapSettings odd = MapSettings::load(settings);
    CHECK(odd.zoom == TileMath::kMaxZoom);
    CHECK(odd.satellite());
    CHECK(odd.streetUrl == MapSettings().streetUrl);
}

TEST_CASE("tiles: URL templates need http(s) and all three placeholders") {
    CHECK(TileMath::templateProblem(MapSettings().streetUrl).isEmpty());
    CHECK(TileMath::templateProblem(MapSettings().satelliteUrl).isEmpty());
    CHECK(TileMath::templateProblem(QStringLiteral(" http://127.0.0.1:8765/{z}/{x}/{y}.png ")).isEmpty());
    CHECK(TileMath::templateProblem(QStringLiteral("https://t/{z}/{x}.png")) ==
          QStringLiteral("A tile URL needs {z}, {x} and {y}."));
    CHECK(TileMath::templateProblem(QStringLiteral("https://t/tile.png")) == QStringLiteral("A tile URL needs {z}, {x} and {y}."));
    CHECK(TileMath::templateProblem(QStringLiteral("file:///tmp/{z}/{x}/{y}")) ==
          QStringLiteral("A tile URL must start with https:// or http://."));
    CHECK_FALSE(TileMath::templateProblem(QStringLiteral("tile.openstreetmap.org/{z}/{x}/{y}.png")).isEmpty());
}

TEST_CASE("map settings: a stored template that cannot be used gives way to the default") {
    QTemporaryDir dir;
    QSettings settings(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
    settings.setValue(QStringLiteral("map/streetUrl"), QStringLiteral("https://t/tile.png"));
    settings.setValue(QStringLiteral("map/satelliteUrl"), QStringLiteral("https://own.example/{z}/{y}/{x}.jpg"));
    const MapSettings m = MapSettings::load(settings);
    CHECK(m.streetUrl == MapSettings().streetUrl);
    CHECK(m.satelliteUrl == QStringLiteral("https://own.example/{z}/{y}/{x}.jpg"));
}

TEST_CASE("tiles: only a picture of tile size is stored") {
    const QByteArray png = QByteArray("\x89PNG\r\n\x1a\n", 8) + QByteArray(100, 'a');
    const QByteArray jpeg = QByteArray("\xff\xd8\xff\xe0", 4) + QByteArray(100, 'a');
    const QByteArray webp = QByteArray("RIFF\x10\x00\x00\x00WEBPVP8 ", 16) + QByteArray(100, 'a');
    CHECK(TileMath::isTileImage(png));
    CHECK(TileMath::isTileImage(jpeg));
    CHECK(TileMath::isTileImage(webp));
    // A captive portal's page, a server's error text, nothing, or far too much.
    CHECK_FALSE(TileMath::isTileImage(QByteArray("<!DOCTYPE html><html><body>Sign in to the Wi-Fi</body></html>")));
    CHECK_FALSE(TileMath::isTileImage(QByteArray("Tile not available")));
    CHECK_FALSE(TileMath::isTileImage(QByteArray()));
    CHECK_FALSE(TileMath::isTileImage(png + QByteArray(TileMath::kMaxTileBytes, 'a')));
}

// MARK: AI button

TEST_CASE("ai: a second press within a second is the same press") {
    AiDetectionButton ai;
    CHECK(ai.press(10'000, 0) == std::optional<bool>(true));
    CHECK_FALSE(ai.press(10'400, 0).has_value());
    CHECK(ai.enabled());
    CHECK_FALSE(ai.press(11'000, 0).has_value()); // exactly a second after: still the same press
    CHECK(ai.press(11'001, 0) == std::optional<bool>(false));
    CHECK_FALSE(ai.enabled());
}

TEST_CASE("ai: no boxes 6 s after the first poll with the link up, said once") {
    AiDetectionButton ai;
    REQUIRE(ai.press(0, 7));
    CHECK_FALSE(ai.update(1'000, true, 7, 0)); // the wait starts here, not at the press
    CHECK_FALSE(ai.update(6'999, true, 7, 0));
    CHECK(ai.update(7'000, true, 7, 0));
    CHECK_FALSE(ai.update(9'000, true, 7, 0));

    // Switched on with the link down: nothing counts until it is up.
    AiDetectionButton offline;
    REQUIRE(offline.press(0, 0));
    CHECK_FALSE(offline.update(10'000, false, 0, 0));
    CHECK_FALSE(offline.update(20'000, true, 0, 0));
    CHECK_FALSE(offline.update(25'999, true, 0, 0));
    CHECK(offline.update(26'000, true, 0, 0));
}

TEST_CASE("ai: boxes that arrive keep it quiet") {
    AiDetectionButton ai;
    REQUIRE(ai.press(0, 7));
    CHECK_FALSE(ai.update(2'000, true, 8, 0)); // a detection packet came, even if now cleared
    CHECK_FALSE(ai.update(7'000, true, 8, 0));
    AiDetectionButton other;
    REQUIRE(other.press(0, 0));
    CHECK_FALSE(other.update(7'000, true, 0, 2));
}

TEST_CASE("ai: switched off, link lost or a new session start over") {
    AiDetectionButton ai;
    REQUIRE(ai.press(0, 0));
    REQUIRE(ai.press(2'000, 0) == std::optional<bool>(false));
    CHECK_FALSE(ai.update(9'000, true, 0, 0)); // off: nothing to wait for

    REQUIRE(ai.press(10'000, 0) == std::optional<bool>(true));
    CHECK_FALSE(ai.update(10'033, true, 0, 0));
    CHECK_FALSE(ai.update(14'000, false, 0, 0)); // link lost
    CHECK_FALSE(ai.update(17'000, true, 0, 0));  // back: the 6 s count from here
    CHECK_FALSE(ai.update(22'000, true, 0, 0));
    CHECK(ai.update(23'000, true, 0, 0));

    ai.reset();
    CHECK_FALSE(ai.enabled());
    CHECK(ai.press(23'100, 0) == std::optional<bool>(true)); // not taken for a repeat
}

TEST_CASE("ai: after a link loss the notice comes again") {
    AiDetectionButton ai;
    REQUIRE(ai.press(0, 3));
    CHECK_FALSE(ai.update(100, true, 3, 0));
    CHECK(ai.update(6'100, true, 3, 0)); // said once
    CHECK_FALSE(ai.update(8'000, true, 3, 0));
    CHECK_FALSE(ai.update(9'000, false, 3, 0)); // link lost: re-armed
    CHECK_FALSE(ai.update(10'000, true, 3, 0));
    CHECK(ai.update(16'000, true, 3, 0));

    // Boxes had come before the loss: the wait after it is a new one too.
    AiDetectionButton boxes;
    REQUIRE(boxes.press(0, 3));
    CHECK_FALSE(boxes.update(100, true, 3, 0));
    CHECK_FALSE(boxes.update(2'000, true, 4, 1));
    CHECK_FALSE(boxes.update(3'000, false, 4, 0));
    CHECK_FALSE(boxes.update(4'000, true, 4, 0));
    CHECK(boxes.update(10'000, true, 4, 0));
}

// MARK: Aircraft figures

TEST_CASE("aircraft: nothing valid, nothing shown") {
    imago_state s{};
    CHECK(Aircraft::figures(s).isEmpty());
}

TEST_CASE("aircraft: altitude, speeds and battery as the Quest shows them") {
    imago_state s{};
    s.vehicle_altitude_valid = true;
    s.vehicle_altitude_m = 119.6f;
    s.vehicle_speed_valid = true;
    s.vehicle_groundspeed_mps = 14.2f;
    s.vehicle_airspeed_mps = 15.0f;
    s.vehicle_battery_valid = true;
    s.vehicle_battery_v = 22.84f;
    s.vehicle_battery_percent = 87;
    const QList<Aircraft::Figure> f = Aircraft::figures(s);
    REQUIRE(f.size() == 4);
    CHECK(f[0] == Aircraft::Figure{QStringLiteral("ALT"), QStringLiteral("120 m")});
    CHECK(f[1] == Aircraft::Figure{QStringLiteral("GS"), QStringLiteral("14 m/s")});
    CHECK(f[2] == Aircraft::Figure{QStringLiteral("AS"), QStringLiteral("15 m/s")});
    CHECK(f[3] == Aircraft::Figure{QStringLiteral("BAT"), QStringLiteral("22.8 V  87 %")});

    // No airspeed sensor: no AS. Battery without an estimate: volts only.
    s.vehicle_airspeed_mps = 0.0f;
    s.vehicle_battery_percent = -1;
    const QList<Aircraft::Figure> g = Aircraft::figures(s);
    REQUIRE(g.size() == 3);
    CHECK(g[2].value == QStringLiteral("22.8 V"));
    CHECK(g[2].level == Aircraft::Figure::Level::Normal);
}

TEST_CASE("aircraft: the battery turns amber, then red") {
    imago_state s{};
    s.vehicle_battery_valid = true;
    s.vehicle_battery_v = 21.0f;
    s.vehicle_battery_percent = 29;
    CHECK(Aircraft::figures(s)[0].level == Aircraft::Figure::Level::Warning);
    s.vehicle_battery_percent = 14;
    CHECK(Aircraft::figures(s)[0].level == Aircraft::Figure::Level::Danger);
    s.vehicle_battery_percent = 30;
    CHECK(Aircraft::figures(s)[0].level == Aircraft::Figure::Level::Normal);
}

// MARK: The Orus L's own stream, the range finder

TEST_CASE("stream: cameras and picture sizes from the core") {
    CHECK(StreamSettings::cameras() == QList<int>{1, 2});
    CHECK(StreamSettings::cameraTitle(1) == QStringLiteral("EO"));
    CHECK(StreamSettings::cameraTitle(2) == QStringLiteral("IR"));
    CHECK_FALSE(StreamSettings::isCamera(0));
    CHECK(StreamSettings::sizeTitles(1) ==
          QStringList{QStringLiteral("1920×1080"), QStringLiteral("1280×720"), QStringLiteral("960×540")});
    CHECK(StreamSettings::sizeTitles(2).size() == 5);
    CHECK(StreamSettings::sizeTitles(2).first() == QStringLiteral("1280×1024"));
    CHECK(StreamSettings::sizeTitles(0).isEmpty());
    CHECK(StreamSettings::rangeFinderRates() ==
          QStringList{QStringLiteral("1 Hz"), QStringLiteral("4 Hz"), QStringLiteral("10 Hz"), QStringLiteral("Off")});
}

// MARK: Profiles

TEST_CASE("profile: aircraft telemetry is off on the desktop, also for old profiles") {
    CHECK_FALSE(ConnectionProfile::make(QStringLiteral("New")).aircraftTelemetry);
    const QByteArray old = R"({"id":"C","name":"Saved before","payloadIP":"192.168.144.240"})";
    const ConnectionProfile p = ConnectionProfile::fromJson(QJsonDocument::fromJson(old).object());
    CHECK_FALSE(p.aircraftTelemetry);
    CHECK(p.telemetryPort() == 0);

    const QByteArray on = R"({"id":"D","name":"Field","payloadIP":"192.168.144.240","aircraftTelemetry":true})";
    ConnectionProfile q = ConnectionProfile::fromJson(QJsonDocument::fromJson(on).object());
    CHECK(q.aircraftTelemetry);
    CHECK(q.telemetryPort() == 14550);
    CHECK(ConnectionProfile::fromJson(q.toJson()) == q);
    // In listen mode the link is that stream: no second listener.
    q.mavlinkMode = ConnectionProfile::MavlinkMode::Listen;
    CHECK(q.telemetryPort() == 0);
}
