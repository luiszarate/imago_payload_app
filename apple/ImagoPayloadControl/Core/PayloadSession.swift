import Foundation
import ImagoCore

/// Something the payload or the video link reported. Delivered on the main queue.
enum PayloadEvent {
    case linkUp
    case linkDown
    case videoStarted(url: String, tcp: Bool)
    case videoStopped
    case videoError(String)
    /// The video fell behind live by this much and the core restarted the stream.
    case videoResync(lagMs: UInt32)
    case commandResult(requestID: UInt32, command: UInt32, result: Int32)
    case paramResult(requestID: UInt32, paramID: String, result: Int32)
    case streamURI(String)
    case statusText(String)
    /// A camera setting's value: read on request, or just accepted.
    case cameraParam(id: String, value: UInt32)
    /// The payload acknowledged a command someone else sent (the autopilot, a GCS).
    /// from: "system/component" of whoever sent it.
    case foreignCommand(command: UInt32, result: Int32, from: String)
    /// The zoom moved without this app asking, e.g. "12.0x -> 1.0x".
    case zoomChangedExternally(String)

    init(_ e: imago_event) {
        let text = e.text.map { String(cString: $0) } ?? ""
        switch e.type {
        case IMAGO_EVENT_LINK_UP: self = .linkUp
        case IMAGO_EVENT_LINK_DOWN: self = .linkDown
        case IMAGO_EVENT_VIDEO_STARTED:
            self = .videoStarted(url: text, tcp: e.code == IMAGO_RTSP_TCP.rawValue)
        case IMAGO_EVENT_VIDEO_STOPPED: self = .videoStopped
        case IMAGO_EVENT_VIDEO_ERROR: self = .videoError(text)
        case IMAGO_EVENT_VIDEO_RESYNC: self = .videoResync(lagMs: e.code)
        case IMAGO_EVENT_COMMAND_RESULT:
            self = .commandResult(requestID: e.request_id, command: e.code, result: e.result)
        case IMAGO_EVENT_PARAM_RESULT:
            self = .paramResult(requestID: e.request_id, paramID: text, result: e.result)
        case IMAGO_EVENT_STREAM_URI: self = .streamURI(text)
        case IMAGO_EVENT_CAMERA_PARAM: self = .cameraParam(id: text, value: e.code)
        case IMAGO_EVENT_FOREIGN_COMMAND:
            self = .foreignCommand(command: e.code, result: e.result, from: text)
        case IMAGO_EVENT_ZOOM_CHANGED_EXTERNALLY: self = .zoomChangedExternally(text)
        default: self = .statusText(text)
        }
    }
}

/// Receives compressed video frames on the core's video thread.
protocol VideoFrameSink: AnyObject {
    func decode(_ frame: UnsafePointer<imago_video_frame>)
}

/// One connection to a payload, wrapping an `imago_session`.
///
/// Every command may be called from any thread. The core encodes and sends it on the calling
/// thread immediately, so the gimbal joystick and the gamepad never wait for the main thread.
final class PayloadSession {
    let profile: ConnectionProfile
    private let handle: OpaquePointer
    private let context: Unmanaged<CallbackContext>

    /// - Parameters:
    ///   - videoSink: receives frames synchronously on the core's video thread.
    ///   - onEvent: called on the main queue.
    init?(profile: ConnectionProfile, videoSink: VideoFrameSink,
          onEvent: @escaping (PayloadEvent) -> Void) {
        self.profile = profile
        var config = imago_config()
        imago_config_init(&config)
        config.payload_port = profile.mavlinkPort
        config.local_port = profile.localPort
        config.nal_format = IMAGO_NAL_AVCC
        config.auto_start_video = true
        config.rtsp_use_payload_host = true
        switch profile.videoTransport {
        case .auto: config.rtsp_transport = IMAGO_RTSP_AUTO
        case .udp: config.rtsp_transport = IMAGO_RTSP_UDP
        case .tcp: config.rtsp_transport = IMAGO_RTSP_TCP
        }
        config.gimbal_protocol = switch profile.gimbalProtocol {
        case .attitude: IMAGO_GIMBAL_PROTOCOL_V2
        case .mountControl: IMAGO_GIMBAL_PROTOCOL_V1
        case .manager: IMAGO_GIMBAL_PROTOCOL_MANAGER
        }
        config.wifi_keep_awake = profile.wifiKeepAwake
        config.video_resync_on_lag = profile.maxVideoLagMs > 0
        if profile.maxVideoLagMs > 0 {
            config.video_max_lag_ms = UInt32(profile.maxVideoLagMs)
        }

        let context = Unmanaged.passRetained(CallbackContext(videoSink: videoSink, onEvent: onEvent))
        let ip = profile.mavlinkMode == .direct ? profile.payloadIP.trimmingCharacters(in: .whitespaces) : ""
        let url = profile.videoURL.trimmingCharacters(in: .whitespaces)
        let created: OpaquePointer? = ip.withCString { ipPointer in
            url.withCString { urlPointer in
                config.payload_ip = ip.isEmpty ? nil : ipPointer
                config.rtsp_url = url.isEmpty ? nil : urlPointer
                // The core copies both strings, so the pointers only need to live this long.
                return imago_session_create(&config, eventTrampoline, frameTrampoline,
                                            context.toOpaque())
            }
        }
        guard let created else {
            context.release()
            return nil
        }
        handle = created
        self.context = context
    }

    deinit {
        // Stops the core threads first, so no callback can run after the context is released.
        imago_session_destroy(handle)
        context.release()
    }

    /// Opens the sockets and starts the core. Returns false if the local port is taken.
    func start() -> Bool { imago_session_start(handle) == 0 }

    func stop() { imago_session_stop(handle) }

    func state() -> imago_state {
        var state = imago_state()
        imago_session_get_state(handle, &state)
        return state
    }

    // MARK: Video

    func reportFrameTimes(receive: Int64, decoded: Int64, presented: Int64) {
        imago_report_frame_presented(handle, receive, decoded, presented)
    }

    // MARK: Gimbal

    func setGimbalRate(pitch: Float, yaw: Float) { imago_gimbal_set_rate(handle, pitch, yaw) }

    /// Takes or gives back primary control of the autopilot's gimbal manager.
    @discardableResult
    func takeGimbalControl(_ take: Bool) -> UInt32 { imago_gimbal_take_control(handle, take) }

    @discardableResult
    func setGimbalMode(_ mode: GimbalMode) -> UInt32 {
        imago_gimbal_set_mode(handle, imago_gimbal_mode(rawValue: mode.rawValue))
    }

    @discardableResult
    func setGimbalAngle(pitch: Float, yaw: Float) -> UInt32 {
        imago_gimbal_set_angle(handle, pitch, yaw)
    }

    // MARK: Camera

    @discardableResult
    func setCameraMode(_ mode: CameraMode) -> UInt32 {
        imago_camera_set_mode(handle, imago_camera_mode(rawValue: UInt32(mode.rawValue)))
    }

    @discardableResult
    func takePhoto() -> UInt32 { imago_camera_take_photo(handle) }

    @discardableResult
    func setRecording(_ on: Bool) -> UInt32 { imago_camera_set_recording(handle, on) }

    /// -1 zoom out, 0 stop, 1 zoom in.
    @discardableResult
    func zoom(_ direction: Int32) -> UInt32 { imago_camera_zoom(handle, direction) }

    /// Exact level from a dragged control: paced and not tracked (see imago_camera_zoom_target).
    func zoomTarget(_ level: Float) { imago_camera_zoom_target(handle, level) }

    @discardableResult
    func setView(_ source: ViewSource) -> UInt32 {
        imago_camera_set_view(handle, imago_view_source(rawValue: UInt32(source.rawValue)))
    }

    @discardableResult
    func setCameraParam(_ id: String, _ value: UInt32) -> UInt32 {
        id.withCString { imago_camera_set_param(handle, $0, value) }
    }

    /// Records all MAVLink in and out to a .tlog file until `stopRecording()` or the session ends.
    func startRecording(to url: URL) -> Bool {
        url.path.withCString { imago_session_start_recording(handle, $0) } == 0
    }

    /// false if part of the log could not be written (full disk).
    @discardableResult
    func stopRecording() -> Bool { imago_session_stop_recording(handle) == 0 }

    /// The levels the camera steps through in a zoom mode (C_V_ZM_CB_LV / C_V_ZM_SR_LV).
    static func zoomLevels(superResolution: Bool) -> [Float] {
        let mode = superResolution ? IMAGO_ZOOM_MODE_SUPER_RESOLUTION : IMAGO_ZOOM_MODE_COMBINED
        var levels = [Float](repeating: 0, count: 32)
        let count = levels.withUnsafeMutableBufferPointer {
            imago_zoom_levels(Int32(mode.rawValue), $0.baseAddress, $0.count)
        }
        return Array(levels.prefix(min(count, levels.count)))
    }

    /// Asks for a setting's current value; it arrives as `.cameraParam`. Reads are paced by
    /// the core, so a whole settings page can ask at once.
    func readCameraParam(_ id: String) {
        _ = id.withCString { imago_camera_read_param(handle, $0) }
    }

    /// Bitrate of a camera's stream, bit/s. (Unverified on the Orus L: which `cameraID` is EO.)
    @discardableResult
    func setStreamBitrate(cameraID: UInt32, bitsPerSecond: UInt32) -> UInt32 {
        imago_stream_set_bitrate(handle, cameraID, bitsPerSecond)
    }

    // MARK: Tracking

    /// Normalized point, (0, 0) top-left.
    @discardableResult
    func track(at point: CGPoint) -> UInt32 {
        imago_tracking_start_at(handle, Float(point.x), Float(point.y), 0, 0)
    }

    /// Locks onto an object with its own box (normalized), e.g. an AI detection.
    @discardableResult
    func track(box: CGRect) -> UInt32 {
        imago_tracking_start_at(handle, Float(box.midX), Float(box.midY),
                                Float(box.width), Float(box.height))
    }

    @discardableResult
    func pointGimbal(at point: CGPoint) -> UInt32 {
        imago_tracking_point_at(handle, Float(point.x), Float(point.y))
    }

    @discardableResult
    func stopTracking() -> UInt32 { imago_tracking_stop(handle) }

    @discardableResult
    func setDetection(_ enabled: Bool) -> UInt32 {
        imago_tracker_set_function(handle, enabled ? IMAGO_TRACKER_OBJECT_DETECTION
                                                   : IMAGO_TRACKER_OBJECT_TRACKING)
    }
}

/// Retained by the session for as long as the core may call back.
private final class CallbackContext {
    weak var videoSink: VideoFrameSink?
    let onEvent: (PayloadEvent) -> Void

    init(videoSink: VideoFrameSink, onEvent: @escaping (PayloadEvent) -> Void) {
        self.videoSink = videoSink
        self.onEvent = onEvent
    }
}

// C callbacks. They capture nothing; the context arrives through the user pointer.

private let eventTrampoline: imago_event_fn = { event, user in
    guard let event, let user else { return }
    let context = Unmanaged<CallbackContext>.fromOpaque(user).takeUnretainedValue()
    let payloadEvent = PayloadEvent(event.pointee)
    DispatchQueue.main.async { context.onEvent(payloadEvent) }
}

private let frameTrampoline: imago_video_frame_fn = { frame, user in
    guard let frame, let user else { return }
    let context = Unmanaged<CallbackContext>.fromOpaque(user).takeUnretainedValue()
    context.videoSink?.decode(frame)
}
