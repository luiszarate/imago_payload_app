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

                switch page {
                case .controller:
                    GamepadSettingsSections()
                default:
                    if model.isConnected {
                        ForEach(page.sections) { PayloadSettingSection(section: $0) }
                        if page == .thermal {
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
            VStack(alignment: .leading, spacing: 6) {
                HStack {
                    Text("Joystick speed")
                    Spacer()
                    Text("\(Int(model.gimbalMaxRateDps.rounded())) °/s")
                        .font(AppFont.figures(14, weight: 600))
                        .foregroundStyle(Theme.textSecondary)
                }
                Slider(value: $model.gimbalMaxRateDps, in: AppModel.gimbalMaxRateRange, step: 5)
            }
        } header: {
            Text("Controls")
        } footer: {
            Text("Gimbal rate at full deflection of the on-screen joystick or a controller, at 1× zoom. It slows down as you zoom in, so the picture moves at a similar speed.")
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
