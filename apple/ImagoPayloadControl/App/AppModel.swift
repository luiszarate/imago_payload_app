import Observation
import SwiftUI
import UIKit

/// App state and actions. Lives on the main actor; anything latency-critical (gimbal rates from
/// the joystick or a gamepad) goes straight to the session, which sends it on the calling thread.
@MainActor
@Observable
final class AppModel {
    struct Toast: Identifiable, Equatable {
        enum Style { case info, error }
        let id = UUID()
        let text: String
        let style: Style
    }

    // MARK: Connection

    private(set) var profiles: [ConnectionProfile]
    private(set) var selectedProfileID: UUID?
    private(set) var isConnected = false
    private(set) var videoActive = false
    private(set) var videoMessage: String?
    private(set) var connectError: String?

    // MARK: Payload state (each part updated only when it changes)

    private(set) var link = LinkStatus()
    private(set) var gimbal = GimbalStatus()
    private(set) var camera = CameraStatus()
    private(set) var tracking = TrackingStatus()
    private(set) var metrics = VideoMetrics()
    private(set) var info = PayloadInfo()
    /// IR camera state; written by `updateThermal` (AppModel+Thermal).
    var thermal = ThermalStatus()
    private(set) var videoSize = CGSize.zero
    /// Aircraft heading from North, when the gimbal or the autopilot reports it.
    private(set) var vehicleHeading: Float?
    /// How far behind live the picture is (a queue upstream), in 50 ms steps. 0 = live.
    private(set) var videoLagMs = 0
    /// The core is restarting the stream to catch up; the last picture stays on screen.
    private(set) var videoResyncing = false

    // MARK: UI state

    var showHUD = false
    /// Gimbal attitude instrument at the bottom of the video. Remembered across launches.
    var showAttitude: Bool {
        didSet { UserDefaults.standard.set(showAttitude, forKey: Self.showAttitudeKey) }
    }
    /// The picture fits right of the camera rail, so the rail never covers the payload's OSD.
    /// Remembered across launches.
    var videoClearOfCameraRail: Bool {
        didSet { UserDefaults.standard.set(videoClearOfCameraRail, forKey: Self.videoClearOfCameraRailKey) }
    }
    var showConnections = false
    var showSettings = false
    /// When on, a tap on the video re-points the gimbal (Eagle Eye) instead of tracking.
    var pointMode = false
    private(set) var detectionEnabled = false
    /// Photo or video, as chosen on the camera rail. The payload confirms it in `camera.mode`.
    private(set) var captureMode: CameraMode = .photo
    private(set) var gamepadName: String?
    private(set) var toast: Toast?

    // MARK: Settings

    /// Camera and gimbal settings as the payload last reported them (param_id -> value).
    /// Filled when the settings screen asks; cleared on disconnect.
    var cameraValues: [String: UInt32] = [:]
    /// How sticks, the on-screen joystick, D-pads and buttons turn into gimbal rates.
    var gimbalTuning: GimbalInputShaper {
        didSet {
            gimbalTuning.save()
            gamepad.setShaper(gimbalTuning)
        }
    }
    /// Which controller inputs drive what.
    var gamepadMapping: GamepadMapping {
        didSet {
            gamepadMapping.save()
            gamepad.setMapping(gamepadMapping)
        }
    }
    /// The action or axis waiting for a controller input, while the user assigns one.
    var gamepadLearning: GamepadLearnTarget?

    // MARK: Diagnostics

    /// A .tlog of all MAVLink is being written (AppModel+Diagnostics).
    var isRecordingLog = false
    /// The last log written, to share.
    var lastLogURL: URL?
    /// Commands the payload acknowledged to someone else (autopilot, GCS) this session.
    var foreignCommandCount = 0
    private var panLimitWarned = false
    @ObservationIgnored var lastForeignToast: Date?
    /// The gimbal manager was heard this connection and the automatic take is settled.
    @ObservationIgnored var gimbalControlSettled = false
    /// The thermal bar asked the camera for its IR settings since the view became IR.
    @ObservationIgnored var thermalSettingsRead = false

    // MARK: Plumbing

    let video = VideoPipeline()
    let gamepad = GamepadController()
    @ObservationIgnored private(set) var session: PayloadSession?
    /// The session as seen from the gamepad queue.
    private let sharedSession = Locked<PayloadSession?>(nil)
    @ObservationIgnored private var pollTimer: Timer?
    private let store = ProfileStore()
    @ObservationIgnored private var pendingLabels: [UInt32: String] = [:]
    @ObservationIgnored private var resumeOnForeground = false
    @ObservationIgnored private var toastTask: Task<Void, Never>?
    /// Incremented on every disconnect; events from an older session are ignored.
    @ObservationIgnored private var generation = 0
    /// Zoom direction currently commanded, so it can be stopped if the app loses focus.
    /// Observed so the zoom slider lets go of its target when a continuous zoom takes over.
    private(set) var zoomDirection: Int32 = 0
    @ObservationIgnored private var lastResync: Date?
    private static let showAttitudeKey = "ui.showAttitude"
    private static let videoClearOfCameraRailKey = "ui.videoClearOfCameraRail"

    init() {
        showAttitude = UserDefaults.standard.object(forKey: Self.showAttitudeKey) as? Bool ?? true
        videoClearOfCameraRail = UserDefaults.standard.object(forKey: Self.videoClearOfCameraRailKey)
            as? Bool ?? true
        gimbalTuning = GimbalInputShaper.load()
        gamepadMapping = GamepadMapping.load()
        let store = ProfileStore()
        let loaded = store.load()
        profiles = loaded
        // Persist the built-in profiles once, so their IDs (and the selection) survive relaunch.
        store.save(loaded)
        selectedProfileID = store.selectedID.flatMap { id in loaded.first { $0.id == id }?.id }
            ?? loaded.first?.id

        video.onVideoSizeChange = { [weak self] size in
            MainActor.assumeIsolated { self?.videoSize = size }
        }
        let shared = sharedSession
        gamepad.onGimbalRate = { pitch, yaw in
            shared.get()?.setGimbalRate(pitch: pitch, yaw: yaw)
        }
        gamepad.onZoom = { [weak self] direction in
            // Sent from the gamepad queue, so it never waits for the main thread; the
            // bookkeeping (slider target, result label) follows on main like a button's.
            let id = shared.get()?.zoom(direction)
            DispatchQueue.main.async {
                MainActor.assumeIsolated { self?.noteZoom(direction, requestID: id) }
            }
        }
        gamepad.onAction = { [weak self] action in
            MainActor.assumeIsolated { self?.handleGamepadAction(action) }
        }
        gamepad.onLearned = { [weak self] target, learned in
            MainActor.assumeIsolated { self?.gamepadLearned(target, learned) }
        }
        gamepad.setMapping(gamepadMapping)
        gamepad.setShaper(gimbalTuning)
        gamepad.onConnectionChange = { [weak self] name in
            MainActor.assumeIsolated {
                self?.gamepadName = name
                if let name { self?.show("\(name) connected", .info) }
            }
        }
        gamepad.start()
    }

    var selectedProfile: ConnectionProfile? {
        profiles.first { $0.id == selectedProfileID }
    }

    /// The profile the running session was opened with (profiles can be edited meanwhile).
    var connectedProfile: ConnectionProfile? {
        isConnected ? session?.profile : nil
    }

    // MARK: Connect / disconnect

    func connect() {
        guard let profile = selectedProfile else {
            showConnections = true
            return
        }
        if let problem = profile.validationError {
            connectError = problem
            showConnections = true
            return
        }
        disconnect()
        connectError = nil
        let currentGeneration = generation
        video.setSmoothing(min(max(profile.videoSmoothingMs, 0), 500) > 0)
        let created = PayloadSession(profile: profile, videoSink: video) { [weak self] event in
            MainActor.assumeIsolated {
                guard let self, self.generation == currentGeneration else { return }
                self.handle(event)
            }
        }
        guard let created else {
            connectError = "The payload address is not valid."
            return
        }
        video.setTimingReporter { [weak created] receive, decoded, presented in
            created?.reportFrameTimes(receive: receive, decoded: decoded, presented: presented)
        }
        guard created.start() else {
            video.setTimingReporter(nil)
            connectError = "Cannot open the MAVLink port. Another app may be using it."
            return
        }
        session = created
        sharedSession.set(created)
        isConnected = true
        videoMessage = profile.videoURL.isEmpty ? "Waiting for the payload's video address…" : "Connecting video…"
        startPolling()
        UIApplication.shared.isIdleTimerDisabled = true
    }

    func disconnect() {
        guard let current = session else { return }
        generation += 1
        current.setGimbalRate(pitch: 0, yaw: 0)
        if zoomDirection != 0 {
            current.zoom(0)
            zoomDirection = 0
        }
        sharedSession.set(nil)
        session = nil
        video.setTimingReporter(nil)
        current.stop() // joins the core threads: no more frames after this
        stopPolling()
        video.reset()
        isConnected = false
        videoActive = false
        videoMessage = nil
        link = LinkStatus()
        gimbal = GimbalStatus()
        panLimitWarned = false
        camera = CameraStatus()
        thermal = ThermalStatus()
        thermalSettingsRead = false
        tracking = TrackingStatus()
        metrics = VideoMetrics()
        vehicleHeading = nil
        videoLagMs = 0
        videoResyncing = false
        lastResync = nil
        detectionEnabled = false
        cameraValues.removeAll()
        gimbalControlSettled = false
        isRecordingLog = false // the core closes the file with the session
        foreignCommandCount = 0
        pendingLabels.removeAll()
        UIApplication.shared.isIdleTimerDisabled = false
    }

    func scenePhaseChanged(_ phase: ScenePhase) {
        switch phase {
        case .inactive:
            // Control Center, a call or an alert took over: never leave anything moving.
            session?.setGimbalRate(pitch: 0, yaw: 0)
            if zoomDirection != 0 {
                zoom(0)
            }
        case .background:
            // iOS suspends the sockets soon after; stop cleanly and come back on return.
            resumeOnForeground = isConnected
            disconnect()
        case .active:
            if resumeOnForeground {
                resumeOnForeground = false
                connect()
            }
        default:
            break
        }
    }

    // MARK: State polling

    private func startPolling() {
        stopPolling()
        let timer = Timer(timeInterval: 1.0 / 30.0, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.poll() }
        }
        RunLoop.main.add(timer, forMode: .common)
        pollTimer = timer
    }

    private func stopPolling() {
        pollTimer?.invalidate()
        pollTimer = nil
    }

    private func poll() {
        guard let session else { return }
        let state = session.state()
        assign(state.linkStatus, to: \.link)
        assign(state.gimbalStatus, to: \.gimbal)
        warnNearPanLimit()
        takeGimbalControlOnce()
        assign(state.cameraStatus, to: \.camera)
        updateThermal(state.thermalStatus)
        assign(state.trackingStatus, to: \.tracking)
        assign(state.payloadInfo, to: \.info)
        assign(state.vehicleHeading, to: \.vehicleHeading)
        assign(state.videoLagSteps, to: \.videoLagMs)
        if showHUD {
            var metrics = state.videoMetrics
            let display = video.displayCounters()
            metrics.decodeFailures = display.decodeFailures
            metrics.framesSkipped = display.skipped
            assign(metrics, to: \.metrics)
        }
        gamepad.zoomLevel.set(max(1, camera.zoom))
    }

    /// Once each time the pan gets within 30° of its ±320° stop: the yaw readouts stay amber
    /// meanwhile. Recentre, or turn the other way, before it hits the stop.
    private func warnNearPanLimit() {
        guard gimbal.panNearLimit != panLimitWarned else { return }
        panLimitWarned = gimbal.panNearLimit
        if panLimitWarned, let pan = gimbal.pan {
            show(String(format: "Gimbal pan %+.0f°: near its ±%.0f° limit", pan, GimbalStatus.panLimit),
                 .error)
        }
    }

    /// Writes only real changes, so SwiftUI re-renders only the views that read that part.
    private func assign<T: Equatable>(_ value: T, to keyPath: ReferenceWritableKeyPath<AppModel, T>) {
        if self[keyPath: keyPath] != value {
            self[keyPath: keyPath] = value
        }
    }

    // MARK: Events

    private func handle(_ event: PayloadEvent) {
        switch event {
        case .linkUp:
            show("Payload connected", .info)
            if session?.profile.setStandardRCMode == true {
                session?.setCameraParam("RC_MODE", 1)
            }
            // Keep the payload's camera mode in line with the rail.
            register(session?.setCameraMode(captureMode), "Camera mode")

        case .linkDown:
            if isConnected {
                show("Payload link lost", .error)
            }
        case let .videoStarted(_, tcp):
            videoActive = true
            videoResyncing = false
            videoMessage = nil
            if tcp, session?.profile.videoTransport == .auto {
                show("Video over TCP: UDP did not get through", .info)
            }
        case .videoStopped:
            videoActive = false
        case let .videoError(message):
            videoActive = false
            videoResyncing = false
            videoMessage = "Video: \(message)"
        case let .videoResync(lagMs):
            videoResyncing = true
            let now = Date()
            if let last = lastResync, now.timeIntervalSince(last) < 30 {
                // Falling behind again right away: the link cannot carry this stream.
                show("Video keeps falling behind. Lower the stream bitrate or resolution.", .error)
            } else {
                show(String(format: "Video was %.1f s behind live. Catching up…", Double(lagMs) / 1000), .info)
            }
            lastResync = now
        case let .commandResult(requestID, command, result):
            finish(requestID, result: result, fallback: Self.commandName(command))
        case let .paramResult(requestID, paramID, result):
            finish(requestID, result: result, fallback: Self.paramName(paramID))
        case .streamURI:
            break
        case let .cameraParam(id, value):
            cameraValues[id] = value
        case let .foreignCommand(command, result, from):
            foreignCommandSeen(command: command, result: result, from: from)
        case let .zoomChangedExternally(change):
            show("Zoom moved without this app: \(change.replacingOccurrences(of: "x", with: "×"))", .error)
        case let .trackingIgnored(command, state):
            // Field test 2026-10-15: accepted for minutes, never applied, until the view changed.
            let still = state == .tracked ? "still tracking" : (state == .lost ? "target lost" : "idle")
            show(command == "stop" ? "Payload accepted Stop but is \(still). Try changing the view."
                                   : "Payload accepted the point but did not track it (\(still)). Try changing the view.",
                 .error)
        case let .statusText(text):
            if !text.isEmpty {
                show(text, .info)
            }
        }
    }

    func register(_ requestID: UInt32?, _ label: String) {
        guard let requestID, requestID != 0 else { return }
        pendingLabels[requestID] = label
    }

    private func finish(_ requestID: UInt32, result: Int32, fallback: String) {
        let label = pendingLabels.removeValue(forKey: requestID) ?? fallback
        guard result != 0 else { return }
        Haptics.error()
        if result == -1 {
            show("\(label): no response from the payload", .error)
        } else {
            show("\(label) was rejected (code \(result))", .error)
        }
    }

    func show(_ text: String, _ style: Toast.Style) {
        let toast = Toast(text: text, style: style)
        self.toast = toast
        toastTask?.cancel()
        toastTask = Task { [weak self] in
            try? await Task.sleep(for: .seconds(style == .error ? 4 : 2.5))
            guard !Task.isCancelled else { return }
            if self?.toast == toast {
                self?.toast = nil
            }
        }
    }

    // MARK: Gimbal

    /// Joystick deflection: x right, y up, both -1...1.
    func joystickMoved(x: Float, y: Float) {
        let rates = gimbalTuning.rates(x: x, y: y, zoom: max(1, camera.zoom))
        session?.setGimbalRate(pitch: rates.pitch, yaw: rates.yaw)
    }

    func setGimbalMode(_ mode: GimbalMode) {
        register(session?.setGimbalMode(mode), "Gimbal mode")
    }

    /// With the autopilot's gimbal manager: take control, or give it back to the pilot / GCS.
    func toggleGimbalControl() {
        Haptics.tap()
        let take = gimbal.control != .thisApp
        register(session?.takeGimbalControl(take), take ? "Take gimbal control" : "Release gimbal control")
    }

    var usesGimbalManager: Bool { connectedProfile?.gimbalProtocol == .manager }

    /// Once per connection, as soon as the gimbal manager answers: take control, unless a ground
    /// station holds it (then the rail button decides). Never again after a link blip, so a
    /// release, or a GCS that took over meanwhile, stays as it is.
    private func takeGimbalControlOnce() {
        guard usesGimbalManager, !gimbalControlSettled, gimbal.control != .unknown else { return }
        gimbalControlSettled = true
        switch gimbal.control {
        case .thisApp:
            break
        case let .other(system, component) where system == 255 || component == 190:
            show("\(gimbal.control.holder) holds the gimbal. Tap TAKE CONTROL to drive it from here.", .info)
        default:
            register(session?.takeGimbalControl(true), "Take gimbal control")
        }
    }

    func recenterGimbal() {
        Haptics.tap()
        let id = session?.setGimbalMode(.reset)
        // Through the gimbal manager it is a streamed setpoint: nothing acknowledges it.
        if !usesGimbalManager {
            register(id, "Recenter")
        }
    }

    // MARK: Camera

    func setCaptureMode(_ mode: CameraMode) {
        guard mode != captureMode else { return }
        captureMode = mode
        register(session?.setCameraMode(mode), "Camera mode")
    }

    /// Shutter: a photo in photo mode, start/stop recording in video mode.
    func shutter() {
        Haptics.tap()
        if captureMode == .video {
            toggleRecording()
        } else {
            register(session?.takePhoto(), "Photo")
        }
    }

    func toggleRecording() {
        if captureMode != .video {
            captureMode = .video
        }
        let start = !camera.recording
        register(session?.setRecording(start), start ? "Start recording" : "Stop recording")
    }

    func zoom(_ direction: Int32) {
        noteZoom(direction, requestID: session?.zoom(direction))
    }

    /// A zoom command went out (button, pinch or gamepad): the slider drops its target and a
    /// failed command gets its label.
    private func noteZoom(_ direction: Int32, requestID: UInt32?) {
        zoomDirection = direction
        if direction != 0 {
            register(requestID, "Zoom")
        }
    }

    /// Exact level from the zoom slider, on every change while it is dragged. The core paces
    /// the commands and sends the final level once more.
    func zoomTo(_ level: Float) {
        session?.zoomTarget(level)
    }

    func setView(_ source: ViewSource) {
        register(session?.setView(source), "View \(source.title)")
    }

    func cycleView(by step: Int) {
        let all = ViewSource.allCases
        let current = camera.view.flatMap { all.firstIndex(of: $0) } ?? 0
        let next = (current + step + all.count) % all.count
        setView(all[next])
    }

    // MARK: Tracking

    /// Tap on the video at a normalized point.
    func videoTapped(at point: CGPoint) {
        Haptics.tap()
        if pointMode {
            register(session?.pointGimbal(at: point), "Point gimbal")
        } else if let detection = tracking.detections.hit(at: point) {
            // The detection's own box, not a fixed square around the tap: the tracker then
            // starts on the whole object the AI found.
            register(session?.track(box: detection.rect), "Track \(detection.label)")
        } else {
            register(session?.track(at: point), "Track")
        }
    }

    func stopTracking() {
        register(session?.stopTracking(), "Stop tracking")
    }

    func toggleDetection() {
        detectionEnabled.toggle()
        register(session?.setDetection(detectionEnabled), "AI detection")
    }

    // MARK: Gamepad

    private func handleGamepadAction(_ action: GamepadAction) {
        switch action {
        case .shutter: shutter()
        case .record: toggleRecording()
        case .stopTracking: stopTracking()
        case .toggleDetection: toggleDetection()
        case .previousView: cycleView(by: -1)
        case .nextView: cycleView(by: 1)
        case .recenter: recenterGimbal()
        case .togglePointMode: pointMode.toggle()
        case .toggleLockFollow: setGimbalMode(gimbal.mode == .lock ? .follow : .lock)
        case .zoomIn, .zoomOut: break // held: sent from the gamepad queue
        }
    }

    // MARK: Profiles

    func selectProfile(_ id: UUID) {
        selectedProfileID = id
        store.selectedID = id
    }

    func saveProfile(_ profile: ConnectionProfile) {
        if let index = profiles.firstIndex(where: { $0.id == profile.id }) {
            profiles[index] = profile
        } else {
            profiles.append(profile)
        }
        store.save(profiles)
        if selectedProfileID == nil {
            selectProfile(profile.id)
        }
    }

    func deleteProfiles(at offsets: IndexSet) {
        let removed = offsets.map { profiles[$0].id }
        profiles.remove(atOffsets: offsets)
        store.save(profiles)
        if let selected = selectedProfileID, removed.contains(selected) {
            if let first = profiles.first {
                selectProfile(first.id)
            } else {
                selectedProfileID = nil
                store.selectedID = nil
            }
        }
    }

    // MARK: Names for messages

    static func commandName(_ command: UInt32) -> String {
        switch command {
        case 530: return "Camera mode"
        case 531: return "Zoom"
        case 532: return "Focus"
        case 2000: return "Photo"
        case 2500: return "Start recording"
        case 2501: return "Stop recording"
        case 31013: return "Tracking"
        default: return "Command \(command)"
        }
    }

    private static func paramName(_ id: String) -> String {
        switch id {
        case "GB_MODE": return "Gimbal mode"
        case "C_SOURCE": return "View"
        case "TRACK_MODE": return "AI detection"
        case "RC_MODE": return "RC mode"
        case "C_T_G": return "IR gain"
        default: return id.hasPrefix("C_T_ISO") || id.hasPrefix("C_T_RG") ? "Isotherm" : id
        }
    }
}
