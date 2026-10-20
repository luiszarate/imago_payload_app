import Foundation
import ImageIO
import ImagoCore
import UIKit

/// One photo or video on the payload's storage, as the core lists it.
struct MediaFile: Identifiable, Equatable {
    enum Kind {
        case photo, video, radiometric, other

        /// SF Symbol for the tile while there is no picture.
        var systemImage: String {
            switch self {
            case .photo: "photo"
            case .video: "play.fill"
            case .radiometric: "thermometer.medium"
            case .other: "doc"
            }
        }
    }

    let name: String
    let kind: Kind
    /// An IR camera photo, as far as the name tells (unverified on our Orus L).
    let thermal: Bool
    let size: Int64?
    /// The payload's own clock, which may be off (ours was 8 days behind, 2026-10-16).
    let captureTime: Date?
    let flight: Int
    let hasThumbnail: Bool
    let thumbnailFailed: Bool

    var id: String { name }
    var isPhoto: Bool { kind == .photo || kind == .radiometric }
    /// A picture the preview can show (videos and other files get a tile).
    var previewable: Bool { isPhoto }

    init(_ f: imago_media_file) {
        name = String(cTuple: f.name)
        switch f.kind {
        case IMAGO_MEDIA_PHOTO: kind = .photo
        case IMAGO_MEDIA_VIDEO: kind = .video
        case IMAGO_MEDIA_RADIOMETRIC: kind = .radiometric
        default: kind = .other
        }
        thermal = f.thermal
        size = f.size_bytes >= 0 ? f.size_bytes : nil
        captureTime = f.capture_time >= 0 ? Date(timeIntervalSince1970: TimeInterval(f.capture_time)) : nil
        flight = Int(f.flight)
        hasThumbnail = f.thumbnail
        thumbnailFailed = f.thumbnail_failed
    }
}

/// Files close together in time: a gap of more than 30 minutes starts a new flight (the core
/// groups them). The files without a capture time form the last group.
struct MediaFlight: Identifiable, Equatable {
    let id: String
    let start: Date?
    let end: Date?
    /// Into the file list, which is newest first.
    let files: Range<Int>
    let photos: Int
    let videos: Int
    let size: Int64

    init(_ f: imago_media_flight, fileCount: Int) {
        start = f.start_time >= 0 ? Date(timeIntervalSince1970: TimeInterval(f.start_time)) : nil
        end = f.end_time >= 0 ? Date(timeIntervalSince1970: TimeInterval(f.end_time)) : nil
        id = f.start_time >= 0 ? "flight-\(f.start_time)" : "no-time"
        // Files and flights are two copies: clamp, the next revision brings them in line.
        let lower = min(Int(f.first_file), fileCount)
        files = lower..<min(lower + Int(f.file_count), fileCount)
        photos = Int(f.photos)
        videos = Int(f.videos)
        size = f.size_bytes
    }

    /// Where a flight's downloads go: its local start time, e.g. "2026-10-19 11-02".
    var folderName: String {
        guard let start else { return "No capture time" }
        return MediaFormat.folder.string(from: start)
    }

    var title: String {
        guard let start else { return "No capture time" }
        let sameYear = Calendar.current.isDate(start, equalTo: .now, toGranularity: .year)
        return (sameYear ? MediaFormat.day : MediaFormat.dayYear).string(from: start)
    }

    /// "11:02 - 11:33", or one time for a single capture.
    var timeRange: String? {
        guard let start, let end else { return nil }
        let from = MediaFormat.minutes.string(from: start)
        let to = MediaFormat.minutes.string(from: end)
        return from == to ? from : "\(from) - \(to)"
    }

    /// "22 photos · 2 videos"
    var counts: String {
        var parts: [String] = []
        if photos > 0 { parts.append(photos == 1 ? "1 photo" : "\(photos) photos") }
        if videos > 0 { parts.append(videos == 1 ? "1 video" : "\(videos) videos") }
        if parts.isEmpty { parts.append(files.count == 1 ? "1 file" : "\(files.count) files") }
        return parts.joined(separator: " · ")
    }
}

/// Times in the payload's clock shown in local time; sizes in MB / GB with one decimal.
enum MediaFormat {
    static let folder = formatter("yyyy-MM-dd HH-mm")
    static let day = formatter("MMM d")
    static let dayYear = formatter("MMM d, yyyy")
    static let minutes = formatter("HH:mm")
    static let seconds = formatter("HH:mm:ss")

    private static func formatter(_ format: String) -> DateFormatter {
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.dateFormat = format
        return formatter
    }

    static func size(_ bytes: Int64) -> String {
        let mb = Double(bytes) / 1_048_576
        return mb >= 1024 ? String(format: "%.1f GB", mb / 1024) : String(format: "%.1f MB", mb)
    }

    static func rate(_ bytesPerSecond: Float) -> String {
        String(format: "%.1f MB/s", Double(bytesPerSecond) / 1_048_576)
    }

    static func time(_ file: MediaFile) -> String {
        file.captureTime.map { seconds.string(from: $0) } ?? file.name
    }
}

/// Thumbnails decoded off the main thread at cell size, kept by file name. The core keeps the
/// JPEG bytes; this keeps the small bitmaps, and fetches the bytes again after an eviction.
final class MediaThumbnails: @unchecked Sendable {
    /// Longest side in pixels: a cell is at most ~240 pt wide at 2× (3× phones have smaller cells).
    static let pixelSize = 480

    private let session: PayloadSession
    private let cache: NSCache<NSString, UIImage> = {
        let cache = NSCache<NSString, UIImage>()
        cache.countLimit = 600
        return cache
    }()

    init(session: PayloadSession) {
        self.session = session
    }

    func cached(_ name: String) -> UIImage? { cache.object(forKey: name as NSString) }

    func load(_ name: String) async -> UIImage? {
        if let image = cached(name) {
            return image
        }
        let session = session
        let image = await Task.detached(priority: .utility) { () -> UIImage? in
            guard let data = session.mediaThumbnail(name) else { return nil }
            return Self.decode(data: data, maxPixels: Self.pixelSize)
        }.value
        if let image {
            cache.setObject(image, forKey: name as NSString)
        }
        return image
    }

    func forget(_ name: String) { cache.removeObject(forKey: name as NSString) }

    /// Downscaled while decoding, so a large JPEG never sits in memory at full size.
    static func decode(data: Data, maxPixels: Int) -> UIImage? {
        guard let source = CGImageSourceCreateWithData(data as CFData, nil) else { return nil }
        return decode(source: source, maxPixels: maxPixels)
    }

    static func decode(url: URL, maxPixels: Int) -> UIImage? {
        guard let source = CGImageSourceCreateWithURL(url as CFURL, nil) else { return nil }
        return decode(source: source, maxPixels: maxPixels)
    }

    private static func decode(source: CGImageSource, maxPixels: Int) -> UIImage? {
        let options: [CFString: Any] = [
            kCGImageSourceCreateThumbnailFromImageAlways: true,
            kCGImageSourceCreateThumbnailWithTransform: true,
            kCGImageSourceShouldCacheImmediately: true,
            kCGImageSourceThumbnailMaxPixelSize: maxPixels,
        ]
        guard let image = CGImageSourceCreateThumbnailAtIndex(source, 0, options as CFDictionary) else {
            return nil
        }
        return UIImage(cgImage: image)
    }
}

/// Downloads live in Documents/Media/<flight>/<file name as on the payload>, which the Files
/// app shows (UIFileSharingEnabled). Previews go to Caches.
enum MediaFolders {
    static var downloads: URL {
        URL.documentsDirectory.appending(path: "Media", directoryHint: .isDirectory)
    }

    static var previews: URL {
        URL.cachesDirectory.appending(path: "MediaPreview", directoryHint: .isDirectory)
    }

    static func download(_ name: String, folder: String) -> URL {
        downloads.appending(path: folder, directoryHint: .isDirectory).appending(path: name)
    }

    static func preview(_ name: String) -> URL {
        previews.appending(path: name)
    }

    /// "folder/name" of every whole file already downloaded (never the .part of one in progress).
    static func scanDownloads() -> Set<String> {
        let manager = FileManager.default
        var keys = Set<String>()
        guard let folders = try? manager.contentsOfDirectory(atPath: downloads.path(percentEncoded: false)) else {
            return keys
        }
        for folder in folders {
            let path = downloads.appending(path: folder, directoryHint: .isDirectory).path(percentEncoded: false)
            for name in (try? manager.contentsOfDirectory(atPath: path)) ?? [] where !name.hasSuffix(".part") {
                keys.insert("\(folder)/\(name)")
            }
        }
        return keys
    }
}
