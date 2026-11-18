import SwiftUI

/// Camera, thermal, stream and gimbal settings of the payload, plus the controls of this app.
/// Payload settings are read when a page opens and written the moment they change.
struct SettingsView: View {
    enum Page: String, CaseIterable, Identifiable {
        case camera, thermal, gimbal, controller
        var id: String { rawValue }
        var title: String {
            switch self {
            case .camera: "Camera"
            case .thermal: "Thermal & stream"
            case .gimbal: "Gimbal"
            case .controller: "Controller"
            }
        }

        var sections: [CameraSettingSection] {
            switch self {
            case .camera: CameraSettingsCatalog.image
            case .thermal: CameraSettingsCatalog.infrared
            case .gimbal: CameraSettingsCatalog.gimbal
            case .controller: []
            }
        }
    }

    @Environment(AppModel.self) private var model
    @Environment(\.dismiss) private var dismiss
    @State private var page: Page = .camera

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    Picker("Page", selection: $page) {
                        ForEach(Page.allCases) { Text($0.title).tag($0) }
                    }
                    .pickerStyle(.segmented)
                    .listRowBackground(Color.clear)
                    .listRowInsets(EdgeInsets())
                }

                if page == .camera {
                    displaySection
                    mapSection
                    markedPointsSection
                    if model.isConnected {
                        PayloadServiceSection()
                    }
                }
                if page == .thermal {
                    logSection
                }

                switch page {
                case .controller:
                    GamepadSettingsSections()
                default:
                    if model.isConnected {
                        ForEach(page.sections) { PayloadSettingSection(section: $0) }
                        if page == .thermal {
                            WebIsothermSection()
                            streamSection
                        }
                        if page == .gimbal {
                            joystickSpeedSection
                        }
                    } else {
                        Section {
                            Text("Connect to the payload to see and change its settings.")
                                .foregroundStyle(Theme.textSecondary)
                        }
                        if page == .gimbal {
                            joystickSpeedSection
                        }
                    }
                }
            }
            .navigationTitle("Settings")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button("Done") { dismiss() }
                        .fontWeight(.semibold)
                }
            }
            .task(id: "\(page.id)-\(model.link.up)") {
                // Again when the link comes (back) up; then once more for what stayed silent.
                guard model.link.up else { return }
                model.readSettings(page.sections)
                try? await Task.sleep(for: .seconds(2))
                model.readSettings(page.sections, onlyMissing: true)
            }
            .onChange(of: page) {
                model.learnGamepadInput(nil)
            }
            .onDisappear {
                model.learnGamepadInput(nil)
            }
        }
        .tint(Theme.magenta)
    }

    private var displaySection: some View {
        @Bindable var model = model
        return Section {
            Toggle("Keep the picture clear of the camera rail", isOn: $model.videoClearOfCameraRail)
            noticeToggle("Show notices", isOn: $model.noticesShown,
                         note: "The short messages in the top right corner of the picture. Off: none at all; the badges over the picture (frozen, behind live, reconnecting) stay.")
            if model.noticesShown {
                noticeToggle("Warn when others command the camera", isOn: $model.noticesOthers,
                             note: "Off: no notice when the zoom moves without this app, or when the autopilot or a ground station commands the camera.")
                noticeToggle("Show video and payload diagnostics", isOn: $model.noticesDiagnostics,
                             note: "Off: no notice when the video switches to TCP or catches up with live video, and no messages from the payload.")
            }
        } header: {
            Text("Display")
        } footer: {
            Text("The payload draws its OSD down the left edge of the picture. Kept clear of the camera rail, the picture starts right of the camera controls: a little smaller on a tablet, only moved over on a phone.")
        }
    }

    /// A switch for one kind of notice, with what turning it off hides.
    private func noticeToggle(_ title: String, isOn: Binding<Bool>, note: String) -> some View {
        VStack(alignment: .leading, spacing: 3) {
            Toggle(title, isOn: isOn)
            Text(note)
                .font(AppFont.body(12))
                .foregroundStyle(Theme.textTertiary)
        }
    }

    private var markedPointsSection: some View {
        Section {
            NavigationLink {
                MarkedPointsView()
            } label: {
                HStack {
                    Text("Marked points")
                    Spacer()
                    Text("\(model.markedPoints.count)")
                        .font(AppFont.figures(13))
                        .foregroundStyle(Theme.textSecondary)
                }
            }
        } footer: {
            Text("Mark the target from its coordinates in the top bar. The list is kept on this device and exports as CSV or KML.")
        }
    }

    private var logSection: some View {
        @Bindable var model = model
        return Section {
            Toggle("Detailed log (link debugging)", isOn: $model.detailedLog)
        } header: {
            Text("MAVLink log")
        } footer: {
            Text("Off: the log keeps what the payload says and is told, its streams once a second, and stays small. On: every message on the link and the app's video and link measurements, for chasing link problems; it grows quickly. Record a log from the Network HUD.")
        }
    }

    /// The stream the Orus L serves itself, per camera. The payload reports neither setting
    /// back, so a tick marks what was asked in this session and nothing is ticked before.
    private var streamSection: some View {
        @Bindable var model = model
        let camera = model.streamRequests.camera
        let askedBitrate = model.streamRequests.bitrates[camera]
        let askedLevel = model.streamRequests.levels[camera].map(Int.init)
        let sizes = camera.sizes
        let askedSize = askedLevel.flatMap { sizes.indices.contains($0) ? sizes[$0] : nil }
        return Section {
            Picker("Camera", selection: $model.streamRequests.camera) {
                ForEach(StreamCamera.allCases) { Text($0.title).tag($0) }
            }
            .pickerStyle(.segmented)
            Menu {
                ForEach(StreamRequests.bitrateChoices, id: \.self) { bps in
                    menuChoice("\(bps / 1_000_000) Mb/s", ticked: askedBitrate == bps) {
                        model.setStreamBitrate(bps)
                    }
                }
            } label: {
                streamRow("Video bitrate",
                          (askedBitrate.map { "\($0 / 1_000_000) Mb/s · " } ?? "")
                              + String(format: "now %.1f Mb/s", model.metrics.kbps / 1000))
            }
            Menu {
                ForEach(Array(sizes.enumerated()), id: \.offset) { level, size in
                    menuChoice(size, ticked: askedLevel == level) {
                        model.setStreamResolution(UInt32(level))
                    }
                }
            } label: {
                streamRow("Picture size", askedSize ?? "—")
            }
        } header: {
            Text("Stream")
        } footer: {
            Text("For the video the Orus L serves itself, not a Herelink's, whose stream has its own encoder. Each camera has its own settings. The payload does not report them back: a tick marks what was asked in this session, and \"now\" is the bitrate measured on the video this app receives. On a weak link a lower bitrate loses fewer packets, and lost packets freeze the picture until the next key frame. (Unverified on the Orus L.)")
        }
    }

    private func streamRow(_ title: String, _ value: String) -> some View {
        HStack {
            Text(title)
                .foregroundStyle(Theme.text)
            Spacer()
            Text(value)
                .font(AppFont.figures(13))
                .foregroundStyle(Theme.textSecondary)
        }
    }

    private func menuChoice(_ title: String, ticked: Bool, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            if ticked {
                Label(title, systemImage: "checkmark")
            } else {
                Text(title)
            }
        }
    }

    /// The map in the picture's corner; its tiles are downloaded on the Offline map page.
    private var mapSection: some View {
        @Bindable var map = model.offlineMap
        return Section {
            Toggle("Show the map", isOn: $map.settings.visible)
            NavigationLink("Offline map") {
                OfflineMapView()
            }
        } header: {
            Text("Map")
        } footer: {
            Text("A small north-up map in the picture's bottom right corner: the aircraft turned to its heading, where the camera looks and the payload's target point. It needs the aircraft's position (with a Payload IP connection, Aircraft telemetry on) and map tiles stored on this device: the payload's Wi-Fi has no internet, so download the flight area in Offline map beforehand.")
        }
    }

    @ViewBuilder
    private var joystickSpeedSection: some View {
        @Bindable var model = model
        Section {
            speedRow("Pan speed", $model.gimbalTuning.maxYawDps)
            speedRow("Tilt speed", $model.gimbalTuning.maxPitchDps)
            Toggle("Fine aim", isOn: $model.gimbalTuning.fineAim)
            VStack(alignment: .leading, spacing: 6) {
                HStack {
                    Text("Zoom compensation")
                    Spacer()
                    Text(zoomCompensationLabel(model.gimbalTuning))
                        .font(AppFont.figures(14, weight: 600))
                        .foregroundStyle(Theme.textSecondary)
                }
                Slider(value: $model.gimbalTuning.zoomCompensation,
                       in: GimbalInputShaper.zoomCompensationRange, step: 0.05) {
                    Text("Zoom compensation")
                } minimumValueLabel: {
                    Text("None").font(AppFont.body(11))
                } maximumValueLabel: {
                    Text("Full").font(AppFont.body(11))
                }
                .disabled(model.gimbal.autoSpeed == true)
                if model.gimbal.autoSpeed == true {
                    Text("Off while the payload's Auto Speed is on: the gimbal sets its own speed.")
                        .font(AppFont.body(12))
                        .foregroundStyle(Theme.textSecondary)
                }
            }
            VStack(alignment: .leading, spacing: 6) {
                HStack {
                    Text("Curve")
                    Spacer()
                    Text(curveLabel(model.gimbalTuning.expo))
                        .font(AppFont.figures(14, weight: 600))
                        .foregroundStyle(Theme.textSecondary)
                }
                Slider(value: $model.gimbalTuning.expo, in: 0...1, step: 0.05) {
                    Text("Curve")
                } minimumValueLabel: {
                    Text("Linear").font(AppFont.body(11))
                } maximumValueLabel: {
                    Text("Fine").font(AppFont.body(11))
                }
            }
            VStack(alignment: .leading, spacing: 6) {
                HStack {
                    Text("Dead zone")
                    Spacer()
                    Text("\(Int((model.gimbalTuning.deadzone * 100).rounded())) %")
                        .font(AppFont.figures(14, weight: 600))
                        .foregroundStyle(Theme.textSecondary)
                }
                Slider(value: $model.gimbalTuning.deadzone, in: GimbalInputShaper.deadzoneRange, step: 0.01)
            }
        } header: {
            Text("Sticks and on-screen joystick")
        } footer: {
            Text("Speed at full deflection and 1× zoom. Zoom compensation sets how much it slows down as you zoom in: None keeps the same speed at any zoom, Full keeps the picture moving at the same pace on screen. A finer curve leaves more of the stick's travel for slow, precise moves. The gimbal ignores speeds under 2 °/s; with Fine aim on, slower stick moves steer it by angle instead, so the first bit of stick moves the camera even when zoomed in.")
        }

        Section {
            speedRow("Pan speed", $model.gimbalTuning.digitalYawDps)
            speedRow("Tilt speed", $model.gimbalTuning.digitalPitchDps)
            Button("Restore defaults") {
                model.gimbalTuning = GimbalInputShaper()
            }
        } header: {
            Text("D-pad and buttons")
        } footer: {
            Text("A D-pad or button is either off or fully on, so it moves at this fixed speed. The gimbal smooths its own start and stop, and the link adds a delay, so it keeps turning briefly after release: a lower speed stops closer to where you let go.")
        }
    }

    private func speedRow(_ title: String, _ value: Binding<Float>) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Text(title)
                Spacer()
                Text("\(Int(value.wrappedValue.rounded())) °/s")
                    .font(AppFont.figures(14, weight: 600))
                    .foregroundStyle(Theme.textSecondary)
            }
            Slider(value: value, in: GimbalInputShaper.speedRange, step: 1)
        }
    }

    /// The setting as a percent and what it means: full-stick pan speed at 10× zoom.
    private func zoomCompensationLabel(_ tuning: GimbalInputShaper) -> String {
        let percent = Int((tuning.zoomCompensation * 100).rounded())
        let panAt10x = Int(tuning.rates(x: 1, y: 0, zoom: 10).yaw.rounded())
        return "\(percent) % · \(panAt10x) °/s at 10×"
    }

    private func curveLabel(_ expo: Float) -> String {
        switch expo {
        case ..<0.15: "Linear"
        case ..<0.45: "Soft"
        case ..<0.75: "Medium"
        default: "Fine"
        }
    }
}

/// One group of payload settings.
private struct PayloadSettingSection: View {
    let section: CameraSettingSection

    var body: some View {
        Section {
            ForEach(section.settings) { PayloadSettingRow(setting: $0) }
        } header: {
            Text(section.title)
        } footer: {
            if let footer = section.footer {
                Text(footer)
            }
        }
    }
}

/// A setting as a switch or a menu. Until the payload answers, it shows a dash.
private struct PayloadSettingRow: View {
    @Environment(AppModel.self) private var model
    let setting: CameraSetting

    var body: some View {
        let value = model.cameraValues[setting.id]
        VStack(alignment: .leading, spacing: 3) {
            switch setting.kind {
            case let .toggle(on, off):
                Toggle(isOn: Binding(get: { value == on },
                                     set: { model.set(setting, to: $0 ? on : off) })) {
                    title(known: value != nil)
                }
            case .choice:
                Picker(selection: Binding(get: { value },
                                          set: { if let new = $0 { model.set(setting, to: new) } })) {
                    if value == nil {
                        Text("—").tag(UInt32?.none)
                    }
                    ForEach(setting.options, id: \.self) { option in
                        Text(option.label).tag(Optional(option.value))
                    }
                } label: {
                    title(known: value != nil)
                }
            }
            if let note = setting.note {
                Text(note)
                    .font(AppFont.body(12))
                    .foregroundStyle(Theme.textTertiary)
            }
        }
    }

    private func title(known: Bool) -> some View {
        HStack(spacing: 6) {
            Text(setting.title)
            if !known {
                ProgressView()
                    .controlSize(.mini)
                    .accessibilityLabel("Reading from the payload")
            }
        }
    }
}

/// The marked target points: newest first, swipe to delete, share as CSV or KML.
private struct MarkedPointsView: View {
    @Environment(AppModel.self) private var model
    @State private var files: (csv: URL, kml: URL)?

    var body: some View {
        List {
            if model.markedPoints.isEmpty {
                Text("No points yet. Tap the target's coordinates in the top bar, then Mark point.")
                    .foregroundStyle(Theme.textSecondary)
            } else {
                Section {
                    let newestFirst = Array(model.markedPoints.reversed())
                    ForEach(newestFirst) { point in
                        VStack(alignment: .leading, spacing: 3) {
                            HStack {
                                Text(point.name)
                                    .font(AppFont.display(14, weight: 600))
                                Spacer()
                                Text(point.time.formatted(date: .abbreviated, time: .standard))
                                    .font(AppFont.figures(12))
                                    .foregroundStyle(Theme.textSecondary)
                            }
                            Text(String(format: "%@ · alt %.0f m", point.position.coordinateText,
                                        point.position.altitude))
                                .font(AppFont.figures(13))
                                .textSelection(.enabled)
                        }
                    }
                    .onDelete { offsets in
                        let ids = offsets.map { newestFirst[$0].id }
                        model.markedPoints.removeAll { ids.contains($0.id) }
                    }
                }
                if let files {
                    Section {
                        ShareLink(item: files.kml) {
                            Label("Share as KML (maps)", systemImage: "map")
                        }
                        ShareLink(item: files.csv) {
                            Label("Share as CSV (spreadsheet)", systemImage: "tablecells")
                        }
                        #if targetEnvironment(macCatalyst)
                        Button {
                            model.showMarkedPointsInFinder(files)
                        } label: {
                            Label("Show KML and CSV in Finder", systemImage: "folder")
                        }
                        #endif
                    }
                }
            }
        }
        .navigationTitle("Marked points")
        .navigationBarTitleDisplayMode(.inline)
        .task(id: model.markedPoints) {
            files = model.markedPoints.isEmpty ? nil : model.exportMarkedPoints()
        }
    }
}

/// The isotherm on the payload's web settings page (our Orus L has it only there). Although the
/// page speaks of a restart, enabling it and changing the threshold show at once on saving
/// (field test 2026-10-19), so there is no restart here.
private struct WebIsothermSection: View {
    @Environment(AppModel.self) private var model
    @State private var draft = WebIsotherm(enabled: false, thresholdC: 35, measurementRange: 0)

    var body: some View {
        Section {
            if model.webIsotherm == nil {
                HStack {
                    Text(model.webBusy ? "Reading the payload's web page…" : "Not read")
                        .foregroundStyle(Theme.textSecondary)
                    Spacer()
                    if model.webBusy {
                        ProgressView()
                    } else {
                        Button("Read") { model.readWebIsotherm() }
                    }
                }
            } else {
                Toggle("Isotherm", isOn: $draft.enabled)
                Stepper(value: $draft.thresholdC, in: WebIsotherm.thresholdLimits, step: 1) {
                    HStack {
                        Text("Threshold")
                        Spacer()
                        Text("\(draft.thresholdC) °C")
                            .font(AppFont.figures(14, weight: 600))
                            .foregroundStyle(Theme.text)
                    }
                }
                Picker("Measurement range", selection: $draft.measurementRange) {
                    Text("-20 to 150 °C").tag(0)
                    Text("-20 to 550 °C").tag(1)
                }
                Button {
                    model.saveWebIsotherm(draft)
                } label: {
                    HStack {
                        Text("Save to payload")
                        if model.webBusy {
                            Spacer()
                            ProgressView()
                        }
                    }
                }
                .disabled(model.webBusy || draft == model.webIsotherm)
            }
        } header: {
            Text("Isotherm (payload web settings)")
        } footer: {
            Text("The payload's own isotherm: one threshold in °C, e.g. 30 for people. Saved on the payload's web page; it shows in the picture right away.")
        }
        .onAppear {
            if let current = model.webIsotherm {
                draft = current
            } else {
                model.readWebIsotherm()
            }
        }
        .onChange(of: model.webIsotherm) { _, current in
            if let current {
                draft = current
            }
        }
    }
}

/// Restarts the payload's camera software through its web page, behind a confirmation: for a
/// tracker that stops obeying in long sessions.
private struct PayloadServiceSection: View {
    @Environment(AppModel.self) private var model
    @State private var confirm = false

    var body: some View {
        Section {
            Button("Restart camera service…", role: .destructive) { confirm = true }
                .disabled(model.webBusy)
        } header: {
            Text("Payload")
        } footer: {
            Text("When tracking stops obeying after a long session. Video, tracking and control drop for a while and come back by themselves.")
        }
        .confirmationDialog("Restart the camera service?", isPresented: $confirm, titleVisibility: .visible) {
            Button("Restart now", role: .destructive) { model.restartPayloadService() }
        } message: {
            Text("Video and control drop while it restarts. Better with the aircraft in a safe state.")
        }
    }
}
