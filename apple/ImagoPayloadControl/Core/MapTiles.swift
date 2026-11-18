import Foundation

/// One Web-Mercator tile (256 px): zoom, column, row.
struct TileID: Hashable, Sendable {
    let z: Int
    let x: Int
    let y: Int
}

/// The slippy-map arithmetic every tile server shares.
enum TileMath {
    static let minZoom = 10
    static let maxZoom = 18
    /// What "Download" fetches: every zoom the map is likely to be used at. The closest ones are
    /// dropped when the area would need more than `maxDownload` tiles. Zoom 17 alone is some 70 %
    /// of the tiles (and of the waiting), so it is fetched only on request (operator,
    /// 2026-11-18: the Quest's download took too long).
    static let downloadZooms = 11...16
    static let detailZoom = 17
    static let maxDownload = 3000

    /// Column of a longitude at a zoom, with its fraction.
    static func x(_ lonDeg: Double, _ z: Int) -> Double {
        (lonDeg + 180) / 360 * Double(1 << z)
    }

    /// Row of a latitude at a zoom, with its fraction (0 at the top, 85.05° N).
    static func y(_ latDeg: Double, _ z: Int) -> Double {
        let lat = min(max(latDeg, -85.05), 85.05) * .pi / 180
        return (1 - log(tan(lat) + 1 / cos(lat)) / .pi) / 2 * Double(1 << z)
    }

    static func lon(_ x: Double, _ z: Int) -> Double { x / Double(1 << z) * 360 - 180 }

    static func lat(_ y: Double, _ z: Int) -> Double {
        atan(sinh(.pi * (1 - 2 * y / Double(1 << z)))) * 180 / .pi
    }

    /// Ground metres per tile pixel.
    static func metresPerPixel(_ latDeg: Double, _ z: Int) -> Double {
        156_543.03392 * cos(latDeg * .pi / 180) / Double(1 << z)
    }

    /// Every tile that touches the square of `radiusKm` around a point, for each zoom.
    /// None for a point or radius that is not a number ("nan" typed in a field parses as one,
    /// and Int(floor(.nan)) would stop the app).
    static func around(lat: Double, lon: Double, radiusKm: Double, zooms: ClosedRange<Int>) -> [TileID] {
        guard lat.isFinite, lon.isFinite, radiusKm.isFinite, radiusKm >= 0 else { return [] }
        let dLat = radiusKm / 111.32
        let dLon = radiusKm / (111.32 * max(cos(lat * .pi / 180), 0.05))
        var tiles: [TileID] = []
        for z in zooms {
            let n = 1 << z
            let x0 = clamp(Int(floor(x(lon - dLon, z))), n)
            let x1 = clamp(Int(floor(x(lon + dLon, z))), n)
            let y0 = clamp(Int(floor(y(lat + dLat, z))), n)
            let y1 = clamp(Int(floor(y(lat - dLat, z))), n)
            for tx in x0...x1 {
                for ty in y0...y1 {
                    tiles.append(TileID(z: z, x: tx, y: ty))
                }
            }
        }
        return tiles
    }

    /// The flight area to download: zoom 11 to 16 (to 17 with `detail`), or to the closest zoom
    /// that keeps it under `maxDownload` tiles (not below 13).
    static func downloadArea(lat: Double, lon: Double, radiusKm: Double,
                             detail: Bool = false) -> (tiles: [TileID], topZoom: Int) {
        var top = detail ? detailZoom : downloadZooms.upperBound
        var tiles = around(lat: lat, lon: lon, radiusKm: radiusKm, zooms: downloadZooms.lowerBound...top)
        while tiles.count > maxDownload && top > 13 {
            top -= 1
            tiles = around(lat: lat, lon: lon, radiusKm: radiusKm, zooms: downloadZooms.lowerBound...top)
        }
        return (tiles, top)
    }

    /// A tile on screen: which one, and its column and row before wrapping (where it is drawn).
    struct Cell: Hashable {
        let id: TileID
        let column: Int
        let row: Int
    }

    /// The tiles that cover a square `halfSpan` tiles each way around a point (tile coordinates,
    /// fractions included). Columns wrap around the antimeridian; rows past the poles are left
    /// out.
    static func cover(centerX: Double, centerY: Double, halfSpan: Double, z: Int) -> [Cell] {
        guard centerX.isFinite, centerY.isFinite, halfSpan.isFinite, halfSpan >= 0 else { return [] }
        let n = 1 << z
        var cells: [Cell] = []
        for column in Int(floor(centerX - halfSpan))...Int(floor(centerX + halfSpan)) {
            for row in Int(floor(centerY - halfSpan))...Int(floor(centerY + halfSpan)) where (0..<n).contains(row) {
                let wrapped = ((column % n) + n) % n
                cells.append(Cell(id: TileID(z: z, x: wrapped, y: row), column: column, row: row))
            }
        }
        return cells
    }

    private static func clamp(_ value: Int, _ n: Int) -> Int { min(max(value, 0), n - 1) }
}

/// Satellite imagery or a street map.
enum MapStyle: String, Codable, CaseIterable, Identifiable, Sendable {
    case satellite, street
    var id: String { rawValue }
    var title: String { self == .satellite ? "Satellite" : "Street map" }
}

/// The map's settings: the device's, not a profile's, kept in UserDefaults like the app's other
/// settings. The tile URLs take {z}, {x} and {y}; point them at a provider you have the right to
/// cache from (the defaults are public services meant for online viewing).
struct MapSettings: Codable, Equatable {
    var visible = true
    var style: MapStyle = .satellite
    var zoom = 16
    var streetURL = Self.defaultStreetURL
    var streetCredit = Self.defaultStreetCredit
    var satelliteURL = Self.defaultSatelliteURL
    var satelliteCredit = Self.defaultSatelliteCredit
    /// The flight area last downloaded, or where the aircraft was last seen.
    var lastLat: Double = 0
    var lastLon: Double = 0
    var downloadRadiusKm: Double = 3
    /// Downloads also fetch zoom 17, the closest: about three times the tiles.
    var downloadDetail = false

    // The same providers as the Quest app (quest/README.md, section Map).
    static let defaultStreetURL = "https://tile.openstreetmap.org/{z}/{x}/{y}.png"
    static let defaultStreetCredit = "© OpenStreetMap contributors"
    static let defaultSatelliteURL =
        "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}"
    static let defaultSatelliteCredit = "Esri, Maxar, Earthstar Geographics"
    static let radiusRange = 0.5...15.0

    private static let key = "map.settings"

    init() {}

    func urlTemplate(_ style: MapStyle) -> String { style == .street ? streetURL : satelliteURL }
    func credit(_ style: MapStyle) -> String { style == .street ? streetCredit : satelliteCredit }

    /// The tile's address, nil when the template is not a usable one (`templateProblem`).
    func url(_ style: MapStyle, _ id: TileID) -> URL? {
        let template = urlTemplate(style)
        guard Self.templateProblem(template) == nil else { return nil }
        return Self.fill(template, id)
    }

    /// What is wrong with a tile URL template, nil when it can be used: an http or https URL
    /// with {z}, {x} and {y}. Without all three every tile would be the same address, and its
    /// one answer stored under thousands of names.
    static func templateProblem(_ template: String) -> String? {
        let missing = ["{z}", "{x}", "{y}"].filter { !template.contains($0) }
        if !missing.isEmpty {
            return "The URL needs \(missing.joined(separator: ", ")), e.g. https://tile.example.org/{z}/{x}/{y}.png"
        }
        guard let url = fill(template, TileID(z: 1, x: 0, y: 0)),
              url.scheme == "https" || url.scheme == "http", url.host() != nil else {
            return "The URL must start with https:// and name a server"
        }
        return nil
    }

    private static func fill(_ template: String, _ id: TileID) -> URL? {
        let text = template
            .replacingOccurrences(of: "{z}", with: String(id.z))
            .replacingOccurrences(of: "{x}", with: String(id.x))
            .replacingOccurrences(of: "{y}", with: String(id.y))
        return URL(string: text.trimmingCharacters(in: .whitespaces))
    }

    /// A number typed in Offline map, a comma taken as the decimal point. Only finite ones:
    /// "nan" and "inf" parse as numbers too.
    static func typedNumber(_ text: String) -> Double? {
        let cleaned = text.trimmingCharacters(in: .whitespaces).replacingOccurrences(of: ",", with: ".")
        guard let value = Double(cleaned), value.isFinite else { return nil }
        return value
    }

    /// Where the aircraft was last seen, if ever.
    var lastPosition: (lat: Double, lon: Double)? {
        lastLat == 0 && lastLon == 0 ? nil : (lastLat, lastLon)
    }

    static func load(from defaults: UserDefaults = .standard) -> MapSettings {
        guard let data = defaults.data(forKey: key),
              let settings = try? JSONDecoder().decode(MapSettings.self, from: data) else {
            return MapSettings()
        }
        return settings
    }

    func save(to defaults: UserDefaults = .standard) {
        if let data = try? JSONEncoder().encode(self) {
            defaults.set(data, forKey: Self.key)
        }
    }

    // Tolerant decoding: fields added later get their defaults, and a zoom out of range is
    // brought back into it.
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        visible = try c.decodeIfPresent(Bool.self, forKey: .visible) ?? true
        style = (try? c.decodeIfPresent(MapStyle.self, forKey: .style)) ?? .satellite
        let storedZoom = try c.decodeIfPresent(Int.self, forKey: .zoom) ?? 16
        zoom = min(max(storedZoom, TileMath.minZoom), TileMath.maxZoom)
        streetURL = try c.decodeIfPresent(String.self, forKey: .streetURL) ?? Self.defaultStreetURL
        streetCredit = try c.decodeIfPresent(String.self, forKey: .streetCredit) ?? Self.defaultStreetCredit
        satelliteURL = try c.decodeIfPresent(String.self, forKey: .satelliteURL) ?? Self.defaultSatelliteURL
        satelliteCredit = try c.decodeIfPresent(String.self, forKey: .satelliteCredit)
            ?? Self.defaultSatelliteCredit
        lastLat = try c.decodeIfPresent(Double.self, forKey: .lastLat) ?? 0
        lastLon = try c.decodeIfPresent(Double.self, forKey: .lastLon) ?? 0
        let radius = try c.decodeIfPresent(Double.self, forKey: .downloadRadiusKm) ?? 3
        downloadRadiusKm = min(max(radius, Self.radiusRange.lowerBound), Self.radiusRange.upperBound)
        downloadDetail = try c.decodeIfPresent(Bool.self, forKey: .downloadDetail) ?? false
    }
}
