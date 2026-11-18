import Foundation
import UIKit

/// Map tiles kept on this device: `tiles/<style>/<z>/<x>/<y>` under Application Support. On
/// the payload's Wi-Fi there is no internet, so what is on disk is what there is: Settings ›
/// Offline map downloads the flight area beforehand, and while flying a missing tile is asked
/// for at most once a minute, in case the device does have a way out (as the Quest app does).
actor TileStore {
    /// Fetches a URL's bytes, nil on any failure. Replaced in tests.
    typealias Fetch = @Sendable (URL) async -> Data?

    static let retryInterval: TimeInterval = 60   // a tile that could not be had, asked again after
    static let offlineInterval: TimeInterval = 60 // after a failed fetch, nothing is fetched for this long
    static let giveUpAfter = 8                    // failures in a row that end a download
    static let politeDelay: Duration = .milliseconds(120)
    /// A 256 px tile is tens of kB; anything this big is not one and is not kept.
    static let maxTileBytes = 2_000_000

    let root: URL
    private let settings: @Sendable () -> MapSettings
    private let fetch: Fetch
    private var missing: [String: Date] = [:] // key -> when it may be tried again
    private var offlineUntil = Date.distantPast

    init(root: URL = TileStore.defaultRoot, settings: @escaping @Sendable () -> MapSettings,
         fetch: @escaping Fetch = TileStore.httpGet) {
        self.root = root
        self.settings = settings
        self.fetch = fetch
    }

    static var defaultRoot: URL {
        URL.applicationSupportDirectory.appending(path: "tiles", directoryHint: .isDirectory)
    }

    nonisolated func file(_ style: MapStyle, _ id: TileID) -> URL {
        root.appending(path: "\(style.rawValue)/\(id.z)/\(id.x)/\(id.y)", directoryHint: .notDirectory)
    }

    nonisolated func has(_ style: MapStyle, _ id: TileID) -> Bool {
        let size = (try? file(style, id).resourceValues(forKeys: [.fileSizeKey]))?.fileSize ?? 0
        return size > 0
    }

    /// The tile decoded and ready to draw: from the disk, or fetched once in a while when it is
    /// not there. nil when there is none (the map draws a grid).
    func image(_ style: MapStyle, _ id: TileID) async -> UIImage? {
        if let image = decode(file(style, id)) {
            return image
        }
        let key = "\(style.rawValue)/\(id.z)/\(id.x)/\(id.y)"
        let now = Date()
        if let retryAt = missing[key], now < retryAt {
            return nil
        }
        missing[key] = now.addingTimeInterval(Self.retryInterval)
        guard await download(style, id) else { return nil }
        missing[key] = nil
        return decode(file(style, id))
    }

    /// Downloads what is not on disk yet, one tile at a time with a pause between them (a
    /// courtesy to the tile server). `progress(done, total, failed)` after each tile; stops when
    /// the task is cancelled or after a run of failures (no internet). Returns how many are on
    /// disk at the end.
    func downloadAll(_ style: MapStyle, _ tiles: [TileID],
                     progress: @escaping @Sendable (Int, Int, Int) -> Void) async -> Int {
        var done = 0
        var failed = 0
        var failedInARow = 0
        for id in tiles {
            if Task.isCancelled || failedInARow >= Self.giveUpAfter {
                break
            }
            if has(style, id) {
                done += 1
            } else {
                offlineUntil = .distantPast // the operator asked: always try
                if await download(style, id) {
                    done += 1
                    failedInARow = 0
                } else {
                    failed += 1
                    failedInARow += 1
                }
                try? await Task.sleep(for: Self.politeDelay)
            }
            progress(done, tiles.count, failed)
        }
        return done
    }

    /// How many tiles of a style are on disk, and their size in bytes. Walked off the actor:
    /// thousands of files would hold up the map's tile loads meanwhile.
    nonisolated func stored(_ style: MapStyle) async -> (count: Int, bytes: Int64) {
        let folder = root.appending(path: style.rawValue, directoryHint: .isDirectory)
        return await Task.detached(priority: .utility) { Self.count(folder) }.value
    }

    func clear(_ style: MapStyle) async {
        let folder = root.appending(path: style.rawValue, directoryHint: .isDirectory)
        await Task.detached(priority: .utility) { try? FileManager.default.removeItem(at: folder) }.value
        missing.removeAll()
    }

    private static func count(_ folder: URL) -> (count: Int, bytes: Int64) {
        guard let walker = FileManager.default.enumerator(
            at: folder, includingPropertiesForKeys: [.fileSizeKey, .isRegularFileKey]) else {
            return (0, 0)
        }
        var count = 0
        var bytes: Int64 = 0
        for case let url as URL in walker where url.pathExtension != "part" {
            let values = try? url.resourceValues(forKeys: [.fileSizeKey, .isRegularFileKey])
            guard values?.isRegularFile == true else { continue }
            count += 1
            bytes += Int64(values?.fileSize ?? 0)
        }
        return (count, bytes)
    }

    // MARK: Private

    /// Decoded here, off the main thread, so drawing the map never waits for a PNG.
    private nonisolated func decode(_ url: URL) -> UIImage? {
        guard let data = try? Data(contentsOf: url), !data.isEmpty else { return nil }
        return UIImage(data: data)?.preparingForDisplay()
    }

    /// Fetches one tile to disk. false when there is no way out, which is then not tried for a
    /// while.
    private func download(_ style: MapStyle, _ id: TileID) async -> Bool {
        guard Date() >= offlineUntil, let url = settings().url(style, id) else { return false }
        guard let data = await fetch(url), !data.isEmpty, data.count <= Self.maxTileBytes,
              UIImage(data: data) != nil else {
            offlineUntil = Date().addingTimeInterval(Self.offlineInterval)
            return false
        }
        let target = file(style, id)
        let part = target.appendingPathExtension("part")
        do {
            try FileManager.default.createDirectory(at: target.deletingLastPathComponent(),
                                                    withIntermediateDirectories: true)
            excludeFromBackup()
            try data.write(to: part, options: .atomic)
            try? FileManager.default.removeItem(at: target)
            try FileManager.default.moveItem(at: part, to: target)
            return true
        } catch {
            try? FileManager.default.removeItem(at: part)
            return false
        }
    }

    /// The tiles can always be downloaded again: they stay out of the device's backups.
    private func excludeFromBackup() {
        var url = root
        var values = URLResourceValues()
        values.isExcludedFromBackup = true
        try? url.setResourceValues(values)
    }

    /// A plain GET naming the app (tile servers ask clients to say who they are); nil on any
    /// failure, and for a body over `maxTileBytes`, which is not read past that.
    static let httpGet: Fetch = { url in
        var request = URLRequest(url: url, timeoutInterval: 8)
        request.setValue(userAgent, forHTTPHeaderField: "User-Agent")
        guard let (bytes, response) = try? await session.bytes(for: request),
              (response as? HTTPURLResponse)?.statusCode == 200,
              response.expectedContentLength <= Int64(maxTileBytes) else { return nil }
        var data = Data()
        do {
            for try await byte in bytes {
                data.append(byte)
                if data.count > maxTileBytes {
                    bytes.task.cancel()
                    return nil
                }
            }
        } catch {
            return nil
        }
        return data
    }

    private static let session: URLSession = {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.timeoutIntervalForRequest = 8
        configuration.waitsForConnectivity = false
        configuration.httpMaximumConnectionsPerHost = 2
        return URLSession(configuration: configuration)
    }()

    static let userAgent: String = {
        let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "0"
        return "ImagoPayloadControl/\(version) (offline map cache)"
    }()
}
