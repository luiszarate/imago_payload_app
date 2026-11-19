import SwiftUI

/// The aircraft's own figures, when its telemetry reaches the app: altitude above home, ground
/// and air speed, battery. One small panel: on a tablet left of the map's card, on its baseline;
/// on a phone over the map in the picture's bottom right corner (the Quest puts the same figures
/// at the right end of its bottom bar). Hidden when the aircraft reports none of them.
struct AircraftFiguresView: View {
    @Environment(AppModel.self) private var model
    /// On a phone as wide as the map under it, so the corner stays one column.
    let width: CGFloat

    var body: some View {
        let figures = model.aircraft
        if !figures.isEmpty {
            VStack(alignment: .leading, spacing: 3) {
                if let altitude = figures.altitude {
                    row("ALT", String(format: "%.0f m", altitude))
                }
                if let ground = figures.groundSpeed {
                    row("GS", String(format: "%.0f m/s", ground))
                }
                if let air = figures.airSpeed {
                    row("AS", String(format: "%.0f m/s", air))
                }
                if let volts = figures.batteryVolts {
                    row("BAT", batteryText(volts, figures.batteryPercent),
                        color: batteryColor(figures.batteryPercent))
                }
            }
            .padding(.horizontal, 10)
            .padding(.vertical, 7)
            .frame(width: width, alignment: .leading)
            .panel(cornerRadius: 12)
            .allowsHitTesting(false)
            .accessibilityElement(children: .combine)
            .accessibilityLabel("Aircraft")
        }
    }

    private func row(_ caption: String, _ figure: String, color: Color = Theme.text) -> some View {
        HStack(spacing: 6) {
            Text(caption)
                .font(AppFont.display(10, weight: 700))
                .tracking(0.8)
                .foregroundStyle(Theme.textTertiary)
                .frame(width: 26, alignment: .leading)
            Text(figure)
                .font(AppFont.figures(12, weight: 600))
                .foregroundStyle(color)
                .lineLimit(1)
                .minimumScaleFactor(0.8)
        }
    }

    private func batteryText(_ volts: Float, _ percent: Int?) -> String {
        String(format: "%.1f V", volts) + (percent.map { "  \($0) %" } ?? "")
    }

    private func batteryColor(_ percent: Int?) -> Color {
        guard let percent else { return Theme.text }
        if percent < AircraftFigures.batteryLowPercent { return Theme.danger }
        if percent < AircraftFigures.batteryWarnPercent { return Theme.warning }
        return Theme.text
    }
}
