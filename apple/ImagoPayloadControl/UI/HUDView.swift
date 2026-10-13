import SwiftUI

/// Network and latency figures, to tell app delays from datalink delays in the field.
struct HUDView: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        let m = model.metrics
        VStack(alignment: .leading, spacing: 6) {
            Text("LINK")
                .font(AppFont.display(10, weight: 700))
                .tracking(1.5)
                .foregroundStyle(Theme.magenta)
            Grid(alignment: .leading, horizontalSpacing: 14, verticalSpacing: 3) {
                row("Video", m.active ? "\(m.codec) · \(m.transport)" : "off")
                row("Frame rate", String(format: "%.1f fps", m.fps))
                row("Bitrate", String(format: "%.1f Mb/s", m.kbps / 1000))
                row("Lost / late", "\(m.packetsLost) / \(m.packetsLate)")
                row("Jitter", String(format: "%.1f ms", m.jitterMs))
                row("Frame transfer", String(format: "%.1f ms", m.transferMs))
                Divider().overlay(Theme.stroke).gridCellColumns(2)
                row("App: assemble", String(format: "%.2f ms", m.assemblyMs))
                row("App: decode", String(format: "%.1f ms", m.decodeMs))
                Divider().overlay(Theme.stroke).gridCellColumns(2)
                row("Command RTT", "\(model.link.roundTripMs) ms (last \(Int(m.roundTripLastMs)))")
                row("MAVLink rx / gaps", "\(m.mavlinkReceived) / \(m.mavlinkGaps)")
                if !model.info.model.isEmpty {
                    row("Payload", "\(model.info.model) \(model.info.softwareVersion)")
                }
            }
        }
        .padding(12)
        .frame(width: 270, alignment: .leading)
        .panel(cornerRadius: 14)
        .allowsHitTesting(false)
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
