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
        } header: {
            Text("Display")
        } footer: {
            Text("The payload draws its OSD down the left edge of the picture. With this on, the picture starts right of the camera controls: a little smaller on a tablet, only moved over on a phone.")
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

    private var streamSection: some View {
        Section {
            Menu {
                ForEach(CameraSettingsCatalog.bitrates, id: \.self) { bps in
                    Button("\(bps / 1_000_000) Mb/s") { model.setStreamBitrate(bps) }
                }
            } label: {
                HStack {
                    Text("Video bitrate")
                        .foregroundStyle(Theme.text)
                    Spacer()
                    Text(String(format: "now %.1f Mb/s", model.metrics.kbps / 1000))
                        .font(AppFont.figures(13))
                        .foregroundStyle(Theme.textSecondary)
                }
            }
        } header: {
            Text("Stream")
        } footer: {
            Text("Through Herelink, a lower bitrate loses fewer packets, and lost packets freeze the picture until the next key frame. The payload does not report its setting, so the figure is the bitrate measured now. (Unverified on the Orus L: that camera 0 is the EO stream.)")
        }
    }

    @ViewBuilder
    private var joystickSpeedSection: some View {
        @Bindable var model = model
        Section {
            speedRow("Pan speed", $model.gimbalTuning.maxYawDps)
            speedRow("Tilt speed", $model.gimbalTuning.maxPitchDps)
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
            Text("Speed at full deflection and 1× zoom; it slows down as you zoom in. A finer curve leaves more of the stick's travel for slow, precise moves.")
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
