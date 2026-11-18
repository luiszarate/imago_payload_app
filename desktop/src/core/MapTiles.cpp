#include "core/MapTiles.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

const char* kVisibleKey = "map/visible";
const char* kStyleKey = "map/style";
const char* kZoomKey = "map/zoom";
const char* kStreetUrlKey = "map/streetUrl";
const char* kStreetCreditKey = "map/streetCredit";
const char* kSatelliteUrlKey = "map/satelliteUrl";
const char* kSatelliteCreditKey = "map/satelliteCredit";
const char* kLastLatKey = "map/lastLat";
const char* kLastLonKey = "map/lastLon";
const char* kRadiusKey = "map/downloadRadiusKm";
const char* kDetailKey = "map/downloadDetail";

constexpr double kPi = std::numbers::pi;
/// Mercator stops here: the map is square.
constexpr double kMaxLatitude = 85.05;

double tiles(int z) { return std::ldexp(1.0, z); }

double radians(double degrees) { return degrees * kPi / 180.0; }

QString text(const QSettings& settings, const char* key, const QString& fallback) {
    const QString value = settings.value(QLatin1String(key), fallback).toString().trimmed();
    return value.isEmpty() ? fallback : value;
}

/// A stored template that cannot be used (edited by hand) gives way to the default.
QString tileUrl(const QSettings& settings, const char* key, const QString& fallback) {
    const QString value = text(settings, key, fallback);
    return TileMath::templateProblem(value).isEmpty() ? value : fallback;
}

double number(const QSettings& settings, const char* key, double fallback) {
    bool ok = false;
    const double value = settings.value(QLatin1String(key), fallback).toDouble(&ok);
    return ok && std::isfinite(value) ? value : fallback;
}

} // namespace

double TileMath::x(double lonDeg, int z) { return (lonDeg + 180.0) / 360.0 * tiles(z); }

double TileMath::y(double latDeg, int z) {
    const double lat = radians(std::clamp(latDeg, -kMaxLatitude, kMaxLatitude));
    return (1.0 - std::log(std::tan(lat) + 1.0 / std::cos(lat)) / kPi) / 2.0 * tiles(z);
}

double TileMath::lon(double x, int z) { return x / tiles(z) * 360.0 - 180.0; }

double TileMath::lat(double y, int z) {
    return std::atan(std::sinh(kPi * (1.0 - 2.0 * y / tiles(z)))) * 180.0 / kPi;
}

double TileMath::metresPerPixel(double latDeg, int z) {
    return 156543.03392 * std::cos(radians(latDeg)) / tiles(z);
}

int TileMath::wrapColumn(int x, int z) {
    const int n = 1 << z;
    return ((x % n) + n) % n;
}

QList<TileId> TileMath::around(double latDeg, double lonDeg, double radiusKm, int minZoom, int maxZoom) {
    const double dLat = radiusKm / 111.32;
    const double dLon = radiusKm / (111.32 * std::max(std::cos(radians(latDeg)), 0.05));
    QList<TileId> result;
    for (int z = minZoom; z <= maxZoom; ++z) {
        const int last = (1 << z) - 1;
        const auto column = [&](double lon) { return std::clamp(static_cast<int>(std::floor(x(lon, z))), 0, last); };
        const auto row = [&](double lat) { return std::clamp(static_cast<int>(std::floor(y(lat, z))), 0, last); };
        const int x0 = column(lonDeg - dLon);
        const int x1 = column(lonDeg + dLon);
        const int y0 = row(latDeg + dLat);
        const int y1 = row(latDeg - dLat);
        for (int tx = x0; tx <= x1; ++tx) {
            for (int ty = y0; ty <= y1; ++ty) {
                result.append({z, tx, ty});
            }
        }
    }
    return result;
}

TileMath::DownloadPlan TileMath::downloadPlan(double latDeg, double lonDeg, double radiusKm, bool detail) {
    DownloadPlan plan;
    plan.topZoom = detail ? kDownloadDetailZoom : kDownloadMaxZoom;
    plan.tiles = around(latDeg, lonDeg, radiusKm, kDownloadMinZoom, plan.topZoom);
    while (plan.tiles.size() > kMaxDownload && plan.topZoom > kDownloadLowestTop) {
        plan.topZoom -= 1;
        plan.tiles = around(latDeg, lonDeg, radiusKm, kDownloadMinZoom, plan.topZoom);
    }
    return plan;
}

QString TileMath::url(const QString& urlTemplate, const TileId& id) {
    QString result = urlTemplate;
    result.replace(QLatin1String("{z}"), QString::number(id.z));
    result.replace(QLatin1String("{x}"), QString::number(id.x));
    result.replace(QLatin1String("{y}"), QString::number(id.y));
    return result;
}

QString TileMath::templateProblem(const QString& urlTemplate) {
    const QString text = urlTemplate.trimmed();
    const QString lower = text.toLower();
    if (!lower.startsWith(QLatin1String("https://")) && !lower.startsWith(QLatin1String("http://"))) {
        return QStringLiteral("A tile URL must start with https:// or http://.");
    }
    for (const char* placeholder : {"{z}", "{x}", "{y}"}) {
        if (!text.contains(QLatin1String(placeholder))) {
            return QStringLiteral("A tile URL needs {z}, {x} and {y}.");
        }
    }
    return {};
}

bool TileMath::isTileImage(const QByteArray& bytes) {
    if (bytes.isEmpty() || bytes.size() > kMaxTileBytes) {
        return false;
    }
    static const QByteArray png("\x89PNG\r\n\x1a\n", 8);
    static const QByteArray jpeg("\xff\xd8\xff", 3);
    return bytes.startsWith(png) || bytes.startsWith(jpeg) ||
           (bytes.size() >= 12 && bytes.startsWith("RIFF") && bytes.mid(8, 4) == "WEBP");
}

QString TileMath::relativePath(const QString& style, const TileId& id) {
    return QStringLiteral("%1/%2/%3/%4").arg(style).arg(id.z).arg(id.x).arg(id.y);
}

MapSettings MapSettings::load(const QSettings& settings) {
    MapSettings m;
    m.visible = settings.value(QLatin1String(kVisibleKey), m.visible).toBool();
    m.style = settings.value(QLatin1String(kStyleKey)).toString() == kStreet ? kStreet : kSatellite;
    m.zoom = std::clamp(settings.value(QLatin1String(kZoomKey), m.zoom).toInt(), TileMath::kMinZoom,
                        TileMath::kMaxZoom);
    m.streetUrl = tileUrl(settings, kStreetUrlKey, m.streetUrl);
    m.streetCredit = text(settings, kStreetCreditKey, m.streetCredit);
    m.satelliteUrl = tileUrl(settings, kSatelliteUrlKey, m.satelliteUrl);
    m.satelliteCredit = text(settings, kSatelliteCreditKey, m.satelliteCredit);
    m.lastLat = std::clamp(number(settings, kLastLatKey, m.lastLat), -90.0, 90.0);
    m.lastLon = std::clamp(number(settings, kLastLonKey, m.lastLon), -180.0, 180.0);
    m.downloadRadiusKm = number(settings, kRadiusKey, m.downloadRadiusKm);
    m.downloadDetail = settings.value(QLatin1String(kDetailKey), m.downloadDetail).toBool();
    return m;
}

void MapSettings::save(QSettings& settings) const {
    settings.setValue(QLatin1String(kVisibleKey), visible);
    settings.setValue(QLatin1String(kStyleKey), style);
    settings.setValue(QLatin1String(kZoomKey), zoom);
    settings.setValue(QLatin1String(kStreetUrlKey), streetUrl);
    settings.setValue(QLatin1String(kStreetCreditKey), streetCredit);
    settings.setValue(QLatin1String(kSatelliteUrlKey), satelliteUrl);
    settings.setValue(QLatin1String(kSatelliteCreditKey), satelliteCredit);
    settings.setValue(QLatin1String(kLastLatKey), lastLat);
    settings.setValue(QLatin1String(kLastLonKey), lastLon);
    settings.setValue(QLatin1String(kRadiusKey), downloadRadiusKm);
    settings.setValue(QLatin1String(kDetailKey), downloadDetail);
}
