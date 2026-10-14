import Foundation
import ImagoCore

// Swift views of the core's state snapshot, split by topic so each SwiftUI view only
// re-renders when its own part changes.

enum GimbalMode: Int32, CaseIterable, Identifiable {
    case off = 0, lock = 1, follow = 2, mapping = 3, reset = 4
    var id: Int32 { rawValue }
    var title: String {
        switch self {
        case .off: return "Off"
        case .lock: return "Lock"
        case .follow: return "Follow"
        case .mapping: return "Mapping"
        case .reset: return "Reset"
        }
    }
}

enum ViewSource: Int32, CaseIterable, Identifiable {
    case eoIR = 0, eo = 1, ir = 2, irEO = 3, sync = 4, sideBySide = 6
    var id: Int32 { rawValue }
    var title: String {
        switch self {
        case .eo: return "EO"
        case .ir: return "IR"
        case .eoIR: return "EO + IR"
        case .irEO: return "IR + EO"
        case .sync: return "Sync"
        case .sideBySide: return "Side by side"
        }
    }
}

enum CameraMode: Int32 {
    case photo = 0, video = 1
}

enum TrackingState: Int32 {
    case idle = 0, tracked = 1, lost = 2
}

struct LinkStatus: Equatable {
    var up = false
    var peer = ""
    var roundTripMs = 0
    var cameraComponent: UInt8 = 0
    var gimbalComponent: UInt8 = 0
}

struct GimbalStatus: Equatable {
    var valid = false
    /// Degrees, rounded to 0.1 so tiny sensor noise does not re-render the UI.
    /// Pitch is relative to the horizon, positive up.
    var pitch: Float = 0
    /// Relative to the aircraft's nose, positive to the right. Nil when the gimbal reports yaw
    /// against North and the aircraft heading is unknown.
    var yaw: Float?
    /// Relative to North, 0..<360. Nil without a heading (or a North-referenced gimbal).
    var yawNorth: Float?
    var mode: GimbalMode?
}

struct CameraStatus: Equatable {
    var view: ViewSource?
    var mode: CameraMode?
    var zoom: Float = 0
    var recording = false
    var recordingSeconds = 0
    var storageAvailableGB: Float?
    var laserRangeMeters: Float?
    /// Horizontal field of view of the EO camera, degrees.
    var horizontalFOV: Float?
}

struct Detection: Equatable, Identifiable {
    var id: Int
    var rect: CGRect // normalized, top-left origin
    var classID: UInt8
    var trackID: UInt16
    var confidence: Float

    var label: String { DetectionLabels.name(for: classID) }
}

struct TrackingStatus: Equatable {
    var state: TrackingState = .idle
    var box: CGRect? // normalized, top-left origin
    var detections: [Detection] = []
}

struct VideoMetrics: Equatable {
    var active = false
    var codec = "—"
    var transport = "—"
    var fps: Float = 0
    var kbps: Float = 0
    var packetsLost: UInt32 = 0
    var packetsLate: UInt32 = 0
    var jitterMs: Float = 0
    var transferMs: Float = 0
    var assemblyMs: Float = 0
    var decodeMs: Float = 0
    var mavlinkGaps: UInt32 = 0
    var mavlinkReceived: UInt32 = 0
    var roundTripLastMs: Float = 0
    var lagMs: Float = 0
    var resyncs: UInt32 = 0
}

struct PayloadInfo: Equatable {
    var model = ""
    var softwareVersion = ""
    var serialNumber = ""
}

extension imago_state {
    var linkStatus: LinkStatus {
        LinkStatus(up: link_up,
                   peer: String(cTuple: mavlink_peer),
                   roundTripMs: Int(command_rtt_ms_avg.rounded()),
                   cameraComponent: camera_component_id,
                   gimbalComponent: gimbal_component_id)
    }

    var gimbalStatus: GimbalStatus {
        GimbalStatus(valid: gimbal_valid,
                     pitch: Self.tenths(gimbal_pitch_deg),
                     yaw: gimbal_valid && gimbal_yaw_valid ? Self.tenths(gimbal_yaw_deg) : nil,
                     yawNorth: gimbal_valid && gimbal_yaw_north_valid ? Self.tenths(gimbal_yaw_north_deg) : nil,
                     mode: GimbalMode(rawValue: gimbal_mode.rawValue))
    }

    /// Aircraft heading from North, whole degrees (it changes constantly in flight).
    var vehicleHeading: Float? {
        vehicle_heading_valid ? vehicle_heading_deg.rounded() : nil
    }

    /// Standing video lag in 50 ms steps, so the warning only re-renders on real changes.
    var videoLagSteps: Int {
        video_active ? Int((video_lag_ms / 50).rounded()) * 50 : 0
    }

    private static func tenths(_ value: Float) -> Float { (value * 10).rounded() / 10 }

    var cameraStatus: CameraStatus {
        CameraStatus(view: ViewSource(rawValue: view_source),
                     mode: CameraMode(rawValue: camera_mode),
                     zoom: (eo_zoom_level * 10).rounded() / 10,
                     recording: recording,
                     recordingSeconds: Int(recording_time_ms / 1000),
                     storageAvailableGB: storage_valid ? storage_available_mib / 1024 : nil,
                     laserRangeMeters: lrf_valid ? lrf_range_m : nil,
                     horizontalFOV: eo_hfov_deg > 0 ? eo_hfov_deg.rounded() : nil)
    }

    var trackingStatus: TrackingStatus {
        var status = TrackingStatus()
        status.state = TrackingState(rawValue: tracking_state) ?? .idle
        if tracking_box_valid {
            status.box = CGRect(x: CGFloat(tracking_box.x), y: CGFloat(tracking_box.y),
                                width: CGFloat(tracking_box.w), height: CGFloat(tracking_box.h))
        }
        let count = Int(detection_count)
        if count > 0 {
            status.detections = withUnsafeBytes(of: detections) { raw in
                let items = raw.bindMemory(to: imago_detection.self)
                return (0..<min(count, items.count)).map { i in
                    let d = items[i]
                    return Detection(id: i,
                                     rect: CGRect(x: CGFloat(d.box.x), y: CGFloat(d.box.y),
                                                  width: CGFloat(d.box.w), height: CGFloat(d.box.h)),
                                     classID: d.class_id,
                                     trackID: d.track_id,
                                     confidence: d.confidence)
                }
            }
        }
        return status
    }

    var videoMetrics: VideoMetrics {
        VideoMetrics(active: video_active,
                     codec: video_codec == IMAGO_CODEC_H265 ? "H.265" : (video_codec == IMAGO_CODEC_H264 ? "H.264" : "—"),
                     transport: video_transport == Int32(IMAGO_RTSP_TCP.rawValue) ? "TCP" : (video_active ? "UDP" : "—"),
                     fps: video_fps,
                     kbps: video_bitrate_kbps,
                     packetsLost: video_packets_lost,
                     packetsLate: video_packets_late,
                     jitterMs: video_jitter_ms,
                     transferMs: video_frame_transfer_ms,
                     assemblyMs: latency_assembly_ms,
                     decodeMs: latency_decode_ms,
                     mavlinkGaps: mavlink_rx_dropped,
                     mavlinkReceived: mavlink_rx_messages,
                     roundTripLastMs: command_rtt_ms_last,
                     lagMs: video_lag_ms,
                     resyncs: video_resyncs)
    }

    var payloadInfo: PayloadInfo {
        PayloadInfo(model: String(cTuple: model_name),
                    softwareVersion: String(cTuple: software_version),
                    serialNumber: String(cTuple: serial_number))
    }
}

extension String {
    /// Reads a NUL-terminated C char array imported into Swift as a tuple.
    init<T>(cTuple: T) {
        self = withUnsafeBytes(of: cTuple) { raw in
            String(decoding: raw.prefix { $0 != 0 }, as: UTF8.self)
        }
    }
}
