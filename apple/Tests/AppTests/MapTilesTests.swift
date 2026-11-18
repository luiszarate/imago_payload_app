import UIKit
import XCTest

/// The map's tile arithmetic, its settings and its tile store (the cases of the Quest's
/// MapTilesTest.kt).
final class MapTilesTests: XCTestCase {
    func testTileArithmetic() {
        // The whole world is one tile at zoom 0, and Greenwich on the equator is its centre.
        XCTAssertEqual(TileMath.x(0, 0), 0.5, accuracy: 1e-9)
        XCTAssertEqual(TileMath.y(0, 0), 0.5, accuracy: 1e-9)
        // Mexico City at zoom 15 (worked out separately with the standard slippy-map formula).
        XCTAssertEqual(Int(TileMath.x(-99.1332, 15)), 7360)
        XCTAssertEqual(Int(TileMath.y(19.4326, 15)), 14580)
        // There and back.
        XCTAssertEqual(TileMath.lat(TileMath.y(19.4326, 16), 16), 19.4326, accuracy: 1e-6)
        XCTAssertEqual(TileMath.lon(TileMath.x(-99.1332, 16), 16), -99.1332, accuracy: 1e-6)
        // A pixel at zoom 16 there is a little over two metres.
        XCTAssertEqual(TileMath.metresPerPixel(19.4326, 16), 2.25, accuracy: 0.05)
    }

    func testAreaAroundAPoint() {
        // 3 km around it: a handful of tiles far out, a hundred or so close in, the centre's among them.
        let far = TileMath.around(lat: 19.4326, lon: -99.1332, radiusKm: 3, zooms: 11...11)
        let near = TileMath.around(lat: 19.4326, lon: -99.1332, radiusKm: 3, zooms: 16...16)
        XCTAssertTrue((1...4).contains(far.count))
        XCTAssertTrue((100...200).contains(near.count), "\(near.count)")
        XCTAssertTrue(near.contains(TileID(z: 16, x: Int(TileMath.x(-99.1332, 16)), y: Int(TileMath.y(19.4326, 16)))))
    }

    func testALargeAreaDropsTheClosestZooms() {
        let small = TileMath.downloadArea(lat: 19.4326, lon: -99.1332, radiusKm: 1)
        XCTAssertEqual(small.topZoom, 16)
        let detail = TileMath.downloadArea(lat: 19.4326, lon: -99.1332, radiusKm: 1, detail: true)
        XCTAssertEqual(detail.topZoom, 17)
        XCTAssertGreaterThan(detail.tiles.count, 2 * small.tiles.count) // zoom 17 is most of it
        let large = TileMath.downloadArea(lat: 19.4326, lon: -99.1332, radiusKm: 15, detail: true)
        XCTAssertLessThan(large.topZoom, 17)
        XCTAssertGreaterThanOrEqual(large.topZoom, 13)
        XCTAssertLessThanOrEqual(large.tiles.count, TileMath.maxDownload)
        XCTAssertEqual(large.tiles.map(\.z).min(), 11)
    }

    func testCoverWrapsColumnsAndDropsRowsPastThePoles() {
        // Half a tile each way around the corner of the world at zoom 2 (4 x 4 tiles).
        let cells = TileMath.cover(centerX: 0.2, centerY: 0.2, halfSpan: 0.5, z: 2)
        XCTAssertEqual(Set(cells.map(\.column)), [-1, 0])
        XCTAssertEqual(Set(cells.map(\.row)), [0]) // row -1 is past the pole
        XCTAssertEqual(cells.first { $0.column == -1 }?.id, TileID(z: 2, x: 3, y: 0))
    }

    func testNumbersThatAreNotNumbersMakeNoTiles() {
        // "nan" typed as the radius parsed, and Int(floor(.nan)) stopped the app.
        XCTAssertNil(MapSettings.typedNumber("nan"))
        XCTAssertNil(MapSettings.typedNumber("inf"))
        XCTAssertNil(MapSettings.typedNumber("-Infinity"))
        XCTAssertNil(MapSettings.typedNumber("three"))
        XCTAssertEqual(MapSettings.typedNumber(" 2,5 "), 2.5)
        XCTAssertEqual(MapSettings.typedNumber("-99.1332"), -99.1332)
        XCTAssertTrue(TileMath.around(lat: 19.4, lon: -99.1, radiusKm: .nan, zooms: 11...16).isEmpty)
        XCTAssertTrue(TileMath.around(lat: .nan, lon: -99.1, radiusKm: 3, zooms: 11...16).isEmpty)
        XCTAssertTrue(TileMath.around(lat: 19.4, lon: .infinity, radiusKm: 3, zooms: 11...16).isEmpty)
        XCTAssertTrue(TileMath.downloadArea(lat: 19.4, lon: -99.1, radiusKm: .nan).tiles.isEmpty)
        XCTAssertTrue(TileMath.cover(centerX: .nan, centerY: 3, halfSpan: 1, z: 12).isEmpty)
    }

    func testTileURLTemplatesNeedAllThreePlaceholders() {
        XCTAssertNil(MapSettings.templateProblem(MapSettings.defaultStreetURL))
        XCTAssertNil(MapSettings.templateProblem(MapSettings.defaultSatelliteURL))
        XCTAssertNil(MapSettings.templateProblem("http://tiles.local:8080/{z}/{x}/{y}.jpg"))
        // One address for every tile: its one answer would be stored under thousands of names.
        XCTAssertNotNil(MapSettings.templateProblem("https://tile.openstreetmap.org/logo.png"))
        XCTAssertNotNil(MapSettings.templateProblem("https://tiles.example/{z}/{x}.png"))
        XCTAssertNotNil(MapSettings.templateProblem("ftp://tiles.example/{z}/{x}/{y}.png"))
        XCTAssertNotNil(MapSettings.templateProblem("{z}/{x}/{y}"))
        XCTAssertNotNil(MapSettings.templateProblem("https://tile.openstr"))
        // A bad template already saved makes no URL at all, so nothing is fetched with it.
        var settings = MapSettings()
        settings.streetURL = "https://tiles.example/map.png"
        XCTAssertNil(settings.url(.street, TileID(z: 15, x: 1, y: 2)))
    }

    func testTileURLs() {
        let settings = MapSettings()
        let id = TileID(z: 15, x: 7360, y: 14580)
        XCTAssertEqual(settings.url(.street, id)?.absoluteString, "https://tile.openstreetmap.org/15/7360/14580.png")
        XCTAssertEqual(settings.url(.satellite, id)?.absoluteString,
                       "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/15/14580/7360")
        var broken = settings
        broken.streetURL = "not a url {z}"
        XCTAssertNil(broken.url(.street, id))
    }

    func testTheSettingsSurviveAndBadZoomsAreClamped() throws {
        let defaults = try XCTUnwrap(UserDefaults(suiteName: "MapTilesTests"))
        defaults.removePersistentDomain(forName: "MapTilesTests")
        XCTAssertEqual(MapSettings.load(from: defaults), MapSettings()) // nothing saved yet
        var settings = MapSettings()
        settings.style = .street
        settings.zoom = 14
        settings.visible = false
        settings.lastLat = 19.4
        settings.lastLon = -99.1
        settings.downloadDetail = true
        settings.save(to: defaults)
        let back = MapSettings.load(from: defaults)
        XCTAssertEqual(back, settings)
        XCTAssertEqual(back.lastPosition?.lat ?? 0, 19.4, accuracy: 1e-9)
        // Older or hand-edited settings: missing fields get their defaults, the zoom its range.
        defaults.set(Data(#"{"zoom": 40, "style": "terrain"}"#.utf8), forKey: "map.settings")
        let odd = MapSettings.load(from: defaults)
        XCTAssertEqual(odd.zoom, TileMath.maxZoom)
        XCTAssertEqual(odd.style, .satellite)
        XCTAssertTrue(odd.visible)
        XCTAssertEqual(odd.streetURL, MapSettings.defaultStreetURL)
        XCTAssertNil(odd.lastPosition)
        XCTAssertFalse(odd.downloadDetail) // 11 to 16 unless asked
        defaults.removePersistentDomain(forName: "MapTilesTests")
    }

    // MARK: Tile store

    private func png(_ color: UIColor) -> Data {
        UIGraphicsImageRenderer(size: CGSize(width: 8, height: 8)).pngData { context in
            color.setFill()
            context.fill(CGRect(x: 0, y: 0, width: 8, height: 8))
        }
    }

    private func temporaryRoot() throws -> URL {
        let url = FileManager.default.temporaryDirectory.appending(path: "tiles-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        addTeardownBlock { try? FileManager.default.removeItem(at: url) }
        return url
    }

    /// Records what was asked and answers with a tile, or nothing when offline.
    private final class FakeServer: @unchecked Sendable {
        private let lock = NSLock()
        private var _asked: [String] = []
        private var _online = true
        let tile: Data

        init(tile: Data) { self.tile = tile }

        var asked: [String] { lock.withLock { _asked } }
        var online: Bool {
            get { lock.withLock { _online } }
            set { lock.withLock { _online = newValue } }
        }

        func fetch(_ url: URL) -> Data? {
            lock.withLock {
                _asked.append(url.absoluteString)
                return _online ? tile : nil
            }
        }
    }

    func testATileIsFetchedOnceKeptOnDiskAndThereWithNoInternet() async throws {
        let root = try temporaryRoot()
        let server = FakeServer(tile: png(.green))
        var settings = MapSettings()
        settings.satelliteURL = "https://tiles.example/{z}/{y}/{x}"
        let fixed = settings
        let store = TileStore(root: root, settings: { fixed }, fetch: { server.fetch($0) })
        let id = TileID(z: 15, x: 7360, y: 14580)

        let first = await store.image(.satellite, id)
        XCTAssertNotNil(first)
        XCTAssertEqual(server.asked, ["https://tiles.example/15/14580/7360"])
        XCTAssertTrue(store.has(.satellite, id))
        XCTAssertTrue(FileManager.default.fileExists(atPath: root.appending(path: "satellite/15/7360/14580").path))

        // Another session, no internet: the tile comes from the disk and nothing is asked.
        server.online = false
        let later = TileStore(root: root, settings: { fixed }, fetch: { server.fetch($0) })
        let again = await later.image(.satellite, id)
        XCTAssertNotNil(again)
        XCTAssertEqual(server.asked.count, 1)

        // A tile that is not there, offline: asked once, then left alone for a while, and no
        // other tile is fetched meanwhile either.
        let other = TileID(z: 15, x: 7361, y: 14580)
        let missing = await later.image(.satellite, other)
        XCTAssertNil(missing)
        _ = await later.image(.satellite, other)
        _ = await later.image(.satellite, TileID(z: 15, x: 7362, y: 14580))
        XCTAssertEqual(server.asked.count, 2)
        XCTAssertFalse(later.has(.satellite, other))
    }

    func testAnAreaIsDownloadedOnceAndStopsWhenThereIsNoInternet() async throws {
        let root = try temporaryRoot()
        let server = FakeServer(tile: png(.blue))
        let store = TileStore(root: root, settings: { MapSettings() }, fetch: { server.fetch($0) })
        let tiles = TileMath.around(lat: 19.4326, lon: -99.1332, radiusKm: 0.4, zooms: 14...15)
        let done = await store.downloadAll(.street, tiles) { _, _, _ in }
        XCTAssertEqual(done, tiles.count)
        let stored = await store.stored(.street)
        XCTAssertEqual(stored.count, tiles.count)
        XCTAssertGreaterThan(stored.bytes, 0)

        // Again: everything is there, nothing is fetched.
        let before = server.asked.count
        let again = await store.downloadAll(.street, tiles) { _, _, _ in }
        XCTAssertEqual(again, tiles.count)
        XCTAssertEqual(server.asked.count, before)

        // A larger area with no internet gives up after a few failures instead of trying them all.
        server.online = false
        let more = TileMath.around(lat: 19.4326, lon: -99.1332, radiusKm: 3, zooms: 16...16)
        _ = await store.downloadAll(.street, more) { _, _, _ in }
        XCTAssertLessThanOrEqual(server.asked.count - before, TileStore.giveUpAfter)

        await store.clear(.street)
        let cleared = await store.stored(.street)
        XCTAssertEqual(cleared.count, 0)
    }

    func testAnswersTooLargeForATileAreNotKept() async throws {
        let root = try temporaryRoot()
        var huge = png(.red)
        huge.append(Data(count: TileStore.maxTileBytes)) // a picture, padded past the limit
        let body = huge
        let store = TileStore(root: root, settings: { MapSettings() }, fetch: { _ in body })
        let id = TileID(z: 15, x: 1, y: 1)
        let image = await store.image(.street, id)
        XCTAssertNil(image)
        XCTAssertFalse(store.has(.street, id))
    }

    func testAnswersThatAreNoPictureAreNotKept() async throws {
        let root = try temporaryRoot()
        let store = TileStore(root: root, settings: { MapSettings() },
                              fetch: { _ in Data("<html>rate limited</html>".utf8) })
        let id = TileID(z: 15, x: 1, y: 1)
        let image = await store.image(.street, id)
        XCTAssertNil(image)
        XCTAssertFalse(store.has(.street, id))
    }
}
