import Foundation
import ImagoCore

/// The cameras of the stream the Orus L serves itself, numbered as Gremsy's examples do
/// (imago_stream_camera). A Herelink's HDMI stream has its own encoder and ignores these.
enum StreamCamera: UInt32, CaseIterable, Identifiable {
    case eo = 1, ir = 2
    var id: UInt32 { rawValue }
    var title: String { self == .eo ? "EO" : "IR" }

    /// The picture sizes of its resolution levels, level 0 first ("1920×1080").
    var sizes: [String] {
        (0..<imago_stream_resolution_count(rawValue)).compactMap { level in
            var width: UInt32 = 0
            var height: UInt32 = 0
            guard imago_stream_resolution_size(rawValue, level, &width, &height) else { return nil }
            return "\(width)×\(height)"
        }
    }
}

/// What this session asked of the payload's own stream. The payload reports neither setting
/// back, so this is all there is to show; it starts empty with each connection.
struct StreamRequests: Equatable {
    /// The camera the Stream settings apply to.
    var camera: StreamCamera = .eo
    /// bit/s asked, per camera.
    var bitrates: [StreamCamera: UInt32] = [:]
    /// Resolution level asked, per camera.
    var levels: [StreamCamera: UInt32] = [:]

    /// Stream bitrates offered, bit/s (the payload takes IMAGO_STREAM_BITRATE_MIN...MAX).
    static let bitrateChoices: [UInt32] = [1, 2, 3, 4, 6, 8].map { $0 * 1_000_000 }
}
