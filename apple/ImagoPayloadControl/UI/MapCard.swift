import SwiftUI

/// The map on a tablet: a twin of the attitude indicator's card beside it, of one size, on one
/// baseline, with the same padding, corners and readout row (north up and the aircraft's heading,
/// which the phone's map draws on the map itself). A map of its own size next to the indicator
/// looked out of line (operator, 2026-11-19). Put away, only the map's faint button is left.
struct MapCard: View {
    @Environment(AppModel.self) private var model
    /// The attitude indicator's card, to match; nil while it is hidden (the card then takes the
    /// dial's measures).
    let twin: CGSize?

    var body: some View {
        if model.offlineMap.settings.visible {
            card
        } else {
            MapOverlay(side: 40) // its "show the map" button
        }
    }

    private var card: some View {
        let padding = AttitudeIndicator.padding(compact: false)
        let side = AttitudeIndicator.dialSide(compact: false)
        let width = (twin?.width).map { max($0 - padding.width * 2, side) } ?? side
        return VStack(spacing: AttitudeIndicator.rowSpacing(compact: false)) {
            MapOverlay(size: CGSize(width: width, height: side), legend: false)
            HStack(spacing: 14) {
                Text("N ↑")
                    .font(AppFont.display(10, weight: 700))
                    .tracking(1)
                    .foregroundStyle(Theme.magenta)
                InstrumentReadout(label: "HDG", value: heading, compact: false)
            }
        }
        .padding(.horizontal, padding.width)
        .padding(.vertical, padding.height)
        .frame(width: twin?.width, height: twin?.height)
        .panel(cornerRadius: AttitudeIndicator.cornerRadius)
    }

    private var heading: String {
        guard let heading = model.vehicleHeading else { return "—" }
        return String(format: "%03d°", (Int(heading.rounded()) % 360 + 360) % 360)
    }

    /// The card's width while the attitude indicator is hidden: the dial's square and padding.
    static var width: CGFloat {
        AttitudeIndicator.dialSide(compact: false) + AttitudeIndicator.padding(compact: false).width * 2
    }
}
