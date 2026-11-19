import SwiftUI

/// The payload settings an operator changes in flight, one tap from the top bar (the range
/// finder's chip): how often the laser range finder measures, the stream the Orus L serves
/// itself, and its OSD. Each choice goes to the payload at once. Until 2026-11-19 they were
/// only in Settings, the range finder under eight other sections.
struct PayloadPanel: View {
    @Environment(AppModel.self) private var model

    /// The settings this panel shows that the payload reports back (range finder, OSD).
    static let sections = CameraSettingsCatalog.image.filter { $0.id == "lrf" || $0.id == "osd" }

    var body: some View {
        @Bindable var model = model
        let camera = model.streamRequests.camera
        VStack(alignment: .leading, spacing: 14) {
            Text("PAYLOAD")
                .font(AppFont.display(13, weight: 700))
                .tracking(1.2)
                .foregroundStyle(Theme.text)

            ForEach(Self.sections.flatMap(\.settings)) { setting in
                choices(setting.id == CameraSettingsCatalog.rangeFinderID ? "LASER RANGE FINDER" : "PAYLOAD OSD",
                        setting.options.map(\.label),
                        selected: setting.options.firstIndex { $0.value == model.cameraValues[setting.id] }) {
                    model.set(setting, to: setting.options[$0].value)
                }
            }

            VStack(alignment: .leading, spacing: 6) {
                caption("THE ORUS L'S OWN STREAM")
                Picker("Camera", selection: $model.streamRequests.camera) {
                    ForEach(StreamCamera.allCases) { Text($0.title).tag($0) }
                }
                .pickerStyle(.segmented)
            }
            // The measured rate is filled while the HUD is open; without it, no "now".
            choices("BITRATE, Mb/s" + (model.metrics.kbps > 0 ? String(format: " · NOW %.1f", model.metrics.kbps / 1000) : ""),
                    StreamRequests.bitrateChoices.map { "\($0 / 1_000_000)" },
                    selected: model.streamRequests.bitrates[camera].flatMap { StreamRequests.bitrateChoices.firstIndex(of: $0) }) {
                model.setStreamBitrate(StreamRequests.bitrateChoices[$0])
            }
            choices("PICTURE SIZE", camera.sizes,
                    selected: model.streamRequests.levels[camera].map(Int.init)) {
                model.setStreamResolution(UInt32($0))
            }
            Text("Stream: for video from the Orus L itself, not a Herelink's. It reports these back to nobody: the lit choice is what was asked in this session.")
                .font(AppFont.body(11))
                .foregroundStyle(Theme.textSecondary)
                .fixedSize(horizontal: false, vertical: true)
        }
        .padding(16)
        .frame(width: 360)
        .background(Theme.surface)
        .preferredColorScheme(.dark)
        .task {
            model.readSettings(Self.sections)
            try? await Task.sleep(for: .seconds(2))
            model.readSettings(Self.sections, onlyMissing: true)
        }
    }

    private func caption(_ text: String) -> some View {
        Text(text)
            .font(AppFont.display(10, weight: 700))
            .tracking(1)
            .foregroundStyle(Theme.textTertiary)
    }

    /// A row of choices, the current one lit; a tap sends at once.
    private func choices(_ title: String, _ labels: [String], selected: Int?,
                         pick: @escaping (Int) -> Void) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            caption(title)
            HStack(spacing: 6) {
                ForEach(Array(labels.enumerated()), id: \.offset) { index, label in
                    let lit = index == selected
                    Button {
                        pick(index)
                    } label: {
                        Text(label)
                            .font(AppFont.display(12, weight: 600))
                            .foregroundStyle(Theme.text)
                            .lineLimit(1)
                            .minimumScaleFactor(0.7)
                            .frame(maxWidth: .infinity, minHeight: 38)
                            .background {
                                RoundedRectangle(cornerRadius: 10, style: .continuous)
                                    .fill(lit ? AnyShapeStyle(Theme.accent) : AnyShapeStyle(Theme.surfaceRaised))
                            }
                    }
                    .buttonStyle(PressScaleStyle())
                    .disabled(!model.link.up)
                    .accessibilityAddTraits(lit ? .isSelected : [])
                }
            }
        }
    }
}
