import Foundation
import Observation
import UIKit

/// The map in the picture's corner and its tiles: the settings, the tiles decoded for drawing,
/// and the downloads of Settings › Offline map. The device's, not a profile's.
@MainActor
@Observable
final class OfflineMap {
    var settings: MapSettings {
        didSet {
            guard settings != oldValue else { return }
            shared.set(settings)
            settings.save()
            if settings.streetURL != oldValue.streetURL || settings.satelliteURL != oldValue.satelliteURL {
                clearMemory()
            }
        }
    }

    // MARK: Downloads (Settings › Offline map)

    /// "Downloading satellite: 120 / 840", then how it ended. Empty before the first.
    private(set) var downloadStatus = ""
    private(set) var downloading = false
    /// "On this device: satellite 840 tiles (14 MB) · street 0 tiles (0 MB)"
    private(set) var storedText = ""

    // MARK: Tiles for drawing

    /// Bumped when a tile has been decoded: the map redraws.
    private(set) var revision = 0
    /// Bumped when the decoded tiles are dropped (a download ended, tiles deleted, another
    /// source): the map asks for its tiles again, though it shows the same ones.
    private(set) var generation = 0
    @ObservationIgnored private var images: [String: UIImage] = [:]
    @ObservationIgnored private var order: [String] = []
    @ObservationIgnored private var loading: Set<String> = []
    @ObservationIgnored private var downloadTask: Task<Void, Never>?
    @ObservationIgnored let store: TileStore
    /// The settings as the tile store reads them, off the main thread.
    @ObservationIgnored private let shared: Locked<MapSettings>

    /// Decoded tiles kept in memory: a few screens' worth at each style.
    private static let memoryTiles = 96

    init() {
        let initial = MapSettings.load()
        settings = initial
        let current = Locked(initial)
        store = TileStore(settings: { current.get() })
        shared = current
    }

    /// The tile if it is decoded; otherwise nil (`load` brings it for a later frame).
    func image(_ style: MapStyle, _ id: TileID) -> UIImage? {
        images[Self.key(style, id)]
    }

    /// Decodes the tiles off the main thread, those not in memory yet.
    func load(_ style: MapStyle, _ ids: [TileID]) {
        for id in ids {
            let key = Self.key(style, id)
            guard images[key] == nil, loading.insert(key).inserted else { continue }
            Task { [store] in
                let image = await store.image(style, id)
                self.loading.remove(key)
                guard let image else { return }
                self.remember(key, image)
            }
        }
    }

    /// Where the aircraft was when the session ended: what Offline map offers to download
    /// around next time.
    func rememberPosition(lat: Double, lon: Double) {
        settings.lastLat = lat
        settings.lastLon = lon
    }

    func download(_ style: MapStyle, lat: Double, lon: Double, radiusKm: Double) {
        guard !downloading, lat.isFinite, lon.isFinite, radiusKm.isFinite else { return }
        let km = min(max(radiusKm, MapSettings.radiusRange.lowerBound), MapSettings.radiusRange.upperBound)
        settings.lastLat = lat
        settings.lastLon = lon
        settings.downloadRadiusKm = km
        let area = TileMath.downloadArea(lat: lat, lon: lon, radiusKm: km, detail: settings.downloadDetail)
        let name = style == .satellite ? "satellite" : "street"
        downloading = true
        downloadStatus = "Downloading \(name): 0 / \(area.tiles.count)"
        downloadTask = Task { [store] in
            let done = await store.downloadAll(style, area.tiles) { done, total, failed in
                Task { @MainActor in
                    guard self.downloading else { return }
                    self.downloadStatus = "Downloading \(name): \(done) / \(total)"
                        + (failed > 0 ? " · \(failed) failed" : "")
                }
            }
            let total = area.tiles.count
            if done == total {
                downloadStatus = String(format: "Done: %d %@ tiles around %.5f, %.5f (zoom %d to %d)",
                                        done, name, lat, lon, TileMath.downloadZooms.lowerBound, area.topZoom)
            } else if Task.isCancelled {
                downloadStatus = "Stopped at \(done) of \(total)"
            } else {
                downloadStatus = "Stopped at \(done) of \(total): no internet? Connect this device to a network with internet and try again"
            }
            downloading = false
            downloadTask = nil
            clearMemory() // tiles drawn as missing before may be there now
            refreshStored()
        }
    }

    func stopDownload() {
        downloadTask?.cancel()
    }

    func delete(_ style: MapStyle) {
        Task { [store] in
            await store.clear(style)
            clearMemory()
            refreshStored()
        }
    }

    func refreshStored() {
        Task { [store] in
            let satellite = await store.stored(.satellite)
            let street = await store.stored(.street)
            storedText = "On this device: satellite \(satellite.count) tiles (\(Self.megabytes(satellite.bytes)))"
                + " · street \(street.count) tiles (\(Self.megabytes(street.bytes)))"
        }
    }

    // MARK: Private

    private func remember(_ key: String, _ image: UIImage) {
        images[key] = image
        order.append(key)
        while order.count > Self.memoryTiles {
            images[order.removeFirst()] = nil
        }
        revision &+= 1
    }

    private func clearMemory() {
        images.removeAll()
        order.removeAll()
        generation &+= 1
        revision &+= 1
    }

    private static func key(_ style: MapStyle, _ id: TileID) -> String {
        "\(style.rawValue)/\(id.z)/\(id.x)/\(id.y)"
    }

    private static func megabytes(_ bytes: Int64) -> String {
        String(format: "%.0f MB", Double(bytes) / 1e6)
    }
}
