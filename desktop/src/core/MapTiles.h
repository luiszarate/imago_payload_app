#pragma once

#include <QByteArray>
#include <QList>
#include <QSettings>
#include <QString>

/// The small map over the picture, as on the Quest (MapTiles.kt): Web-Mercator tiles kept on
/// this computer, the arithmetic every slippy-map tile server shares, and the map's settings.

/// One 256 px tile: zoom, column, row.
struct TileId {
    int z = 0;
    int x = 0;
    int y = 0;
    bool operator==(const TileId&) const = default;
};

namespace TileMath {

/// What the map's own + and − buttons go through.
constexpr int kMinZoom = 10;
constexpr int kMaxZoom = 18;
/// What an area download fetches; the closest zooms are dropped for a large area. Zoom 17
/// alone is some 70 % of the tiles (and of the waiting): 11 to 16 by default, 17 on request
/// (operator, 2026-11-18: the Quest's download took too long).
constexpr int kDownloadMinZoom = 11;
constexpr int kDownloadMaxZoom = 16;
constexpr int kDownloadDetailZoom = 17;
constexpr int kDownloadLowestTop = 13;
/// At most this many tiles in one download: a courtesy to the tile server.
constexpr int kMaxDownload = 3000;
/// A tile's side in pixels, as the servers make them.
constexpr int kTilePixels = 256;
/// A tile is some 5 to 50 KB; anything past this is not one.
constexpr qint64 kMaxTileBytes = 2 * 1024 * 1024;

/// Column of a longitude at a zoom, with its fraction.
double x(double lonDeg, int z);
/// Row of a latitude at a zoom, with its fraction (0 at the top, 85.05 N).
double y(double latDeg, int z);
double lon(double x, int z);
double lat(double y, int z);
/// Ground metres per tile pixel.
double metresPerPixel(double latDeg, int z);
/// A column on the map, wrapped around the antimeridian.
int wrapColumn(int x, int z);

/// Every tile that touches the square of `radiusKm` around a point, for each zoom.
QList<TileId> around(double latDeg, double lonDeg, double radiusKm, int minZoom, int maxZoom);

/// What an area download fetches: every zoom from kDownloadMinZoom up to `topZoom`, which comes
/// down from kDownloadMaxZoom (kDownloadDetailZoom with `detail`), not below kDownloadLowestTop,
/// while the area is over kMaxDownload.
struct DownloadPlan {
    QList<TileId> tiles;
    int topZoom = kDownloadMaxZoom;
};
DownloadPlan downloadPlan(double latDeg, double lonDeg, double radiusKm, bool detail = false);

/// A URL template with {z}, {x} and {y} filled in.
QString url(const QString& urlTemplate, const TileId& id);
/// Why a tile URL template cannot be used, or an empty string: http(s) with {z}, {x} and {y}.
/// Without all three every tile would be the same request, stored under thousands of paths.
QString templateProblem(const QString& urlTemplate);
/// A fetched body is a tile: a PNG, JPEG or WebP picture of at most kMaxTileBytes. A captive
/// portal's page or a server's error text answered with 200 is not, and is never stored.
bool isTileImage(const QByteArray& bytes);
/// "<style>/<z>/<x>/<y>" under the tile store.
QString relativePath(const QString& style, const TileId& id);

} // namespace TileMath

/// The map's settings, in the app's settings file beside its other settings (they are this
/// computer's, not a profile's). The URLs take {z}, {x} and {y}; point them at a provider you
/// have the right to cache from (desktop/README.md).
struct MapSettings {
    static inline const QString kSatellite = QStringLiteral("satellite");
    static inline const QString kStreet = QStringLiteral("street");

    bool visible = true;
    /// kSatellite or kStreet.
    QString style = kSatellite;
    int zoom = 16;
    QString streetUrl = QStringLiteral("https://tile.openstreetmap.org/{z}/{x}/{y}.png");
    QString streetCredit = QStringLiteral("© OpenStreetMap contributors");
    QString satelliteUrl =
        QStringLiteral("https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}");
    QString satelliteCredit = QStringLiteral("Esri, Maxar, Earthstar Geographics");
    /// Where the aircraft was last seen: the area the Offline map section offers to download.
    double lastLat = 0.0;
    double lastLon = 0.0;
    double downloadRadiusKm = 3.0;
    /// Downloads also fetch zoom 17, the closest: about three times the tiles.
    bool downloadDetail = false;

    bool satellite() const { return style != kStreet; }
    QString url(const QString& forStyle) const { return forStyle == kStreet ? streetUrl : satelliteUrl; }
    QString credit() const { return satellite() ? satelliteCredit : streetCredit; }

    /// Missing values keep their defaults, and so does a tile URL template that cannot be used;
    /// the zoom is kept within the map's range.
    static MapSettings load(const QSettings& settings);
    void save(QSettings& settings) const;
    bool operator==(const MapSettings&) const = default;
};
