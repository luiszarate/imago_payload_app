import SwiftUI

/// Network and latency figures, to tell app delays from datalink delays in the field.
struct HUDView: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        let m = model.metrics
        VStack(alignment: .leading, spacing: 6) {
            HStack(spacing: 8) {
                Text("LINK")
                    .font(AppFont.display(10, weight: 700))
                    .tracking(1.5)
                    .foregroundStyle(Theme.magenta)
                Spacer()
            }
            .frame(height: 22)
            Grid(alignment: .leading, horizontalSpacing: 14, verticalSpacing: 3) {
                row("Video", m.active ? "\(m.codec) · \(m.transport)" : "off")
                row("Frame rate", String(format: "%.1f fps", m.fps))
                row("Bitrate", String(format: "%.1f Mb/s", m.kbps / 1000))
                row("Lost / late", "\(m.packetsLost) / \(m.packetsLate)")
                row("Jitter", String(format: "%.1f ms", m.jitterMs))
                row("Frame transfer", String(format: "%.1f ms", m.transferMs))
                // About one frame period on a smooth link; bursts show as gaps of several.
                row("Frame gaps p95 / max", String(format: "%.0f / %.0f ms", m.frameGapP95Ms, m.frameGapMaxMs))
                row("Behind live", "\(Int(m.lagMs.rounded())) ms · \(m.resyncs) resync\(m.resyncs == 1 ? "" : "s")")
                Divider().overlay(Theme.stroke).gridCellColumns(2)
                row("App: assemble", String(format: "%.2f ms", m.assemblyMs))
                row("App: decode", String(format: "%.1f ms", m.decodeMs))
                if m.presentMs > 0 {
                    row("App: shown (smoothing)", String(format: "%.0f ms", m.presentMs))
                }
                // Where frozen pictures come from: lost packets, the decoder, or the display.
                row("Corrupt / failed / skipped",
                    "\(m.framesCorrupt) / \(m.decodeFailures) / \(m.framesSkipped)")
                Divider().overlay(Theme.stroke).gridCellColumns(2)
                row("Command RTT", "\(model.link.roundTripMs) ms (last \(Int(m.roundTripLastMs)))")
                row("MAVLink rx / gaps", "\(m.mavlinkReceived) / \(m.mavlinkGaps)")
                // Acknowledgements the payload sent to the autopilot or a GCS: they command it too.
                row("Others' commands", "\(model.foreignCommandCount)")
                if !model.info.model.isEmpty {
                    row("Payload", "\(model.info.model) \(model.info.softwareVersion)")
                }
                // Which value means which thermal core (palette names) is still to be confirmed.
                if model.thermal.cameraType >= 0 {
                    row("IR sensor type", "\(model.thermal.cameraType)")
                }
            }
        }
        .padding(12)
        .frame(width: 270, alignment: .leading)
        .panel(cornerRadius: 14)
        // Taps on the figures reach the video below (tap to track); only the log buttons take them.
        .allowsHitTesting(false)
        .overlay(alignment: .topTrailing) {
            HStack(spacing: 8) { logControls }
                .padding(.top, 10)
                .padding(.trailing, 12)
        }
    }

    /// Record all MAVLink to a .tlog, and share the last one.
    @ViewBuilder
    private var logControls: some View {
        if let url = model.lastLogURL, !model.isRecordingLog {
            ShareLink(item: url) {
                Image(systemName: "square.and.arrow.up")
                    .font(.system(size: 13, weight: .semibold))
                    .foregroundStyle(Theme.text)
            }
            .accessibilityLabel("Share MAVLink log")
        }
        #if targetEnvironment(macCatalyst)
        Button {
            MacSupport.showInFinder(AppModel.logsDirectory) {
                model.show("Cannot open the log folder in Finder", .error)
            }
        } label: {
            Image(systemName: "folder")
                .font(.system(size: 13, weight: .semibold))
                .foregroundStyle(Theme.text)
        }
        .accessibilityLabel("Show MAVLink logs in Finder")
        #endif
        Button {
            model.toggleLogRecording()
        } label: {
            HStack(spacing: 5) {
                Circle()
                    .fill(model.isRecordingLog ? Theme.danger : Theme.textTertiary)
                    .frame(width: 8, height: 8)
                Text(model.isRecordingLog ? "Stop log" : "Record log")
                    .font(AppFont.body(11, weight: 600))
                    .foregroundStyle(Theme.text)
            }
            .padding(.horizontal, 8)
            .padding(.vertical, 4)
            .background(Capsule().fill(Theme.surfaceRaised))
        }
        .buttonStyle(.plain)
        .accessibilityLabel(model.isRecordingLog ? "Stop MAVLink log" : "Record MAVLink log")
    }

    @ViewBuilder
    private func row(_ label: String, _ value: String) -> some View {
        GridRow {
            Text(label)
                .font(AppFont.body(12, weight: 500))
                .foregroundStyle(Theme.textSecondary)
            Text(value)
                .font(AppFont.figures(12, weight: 600))
                .foregroundStyle(Theme.text)
                .lineLimit(1)
        }
    }
}
