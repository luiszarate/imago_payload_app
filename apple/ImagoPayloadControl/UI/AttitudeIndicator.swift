import SwiftUI

/// Gimbal attitude against the aircraft, seen from above.
///
/// The aircraft always points up. The cone shows where the camera looks, as wide as its field
/// of view. The ring is a compass when the aircraft heading is known (it turns as the aircraft
/// turns); otherwise its ticks are relative to the nose. The arc on the right is the camera's
/// pitch against the horizon, from 30° up to straight down.
struct AttitudeIndicator: View {
    @Environment(AppModel.self) private var model
    let compact: Bool

    var body: some View {
        let gimbal = model.gimbal
        let dial = AttitudeDial(pitch: gimbal.valid ? gimbal.pitch : nil,
                                yaw: gimbal.yaw,
                                yawNorth: gimbal.yawNorth,
                                heading: model.vehicleHeading,
                                fieldOfView: model.camera.horizontalFOV)
        let diameter: CGFloat = compact ? 92 : 136
        VStack(spacing: compact ? 4 : 6) {
            dial
                .equatable()
                .frame(width: diameter + AttitudeDial.margin * 2,
                       height: diameter + AttitudeDial.margin * 2)
            HStack(spacing: compact ? 10 : 14) {
                readout("PITCH", gimbal.valid ? Self.signed(gimbal.pitch) : "—")
                // The pan runs to ±320°, so the operator sees the gimbal winding towards its
                // stop; the wrapped yaw only until the core has seen it turn.
                readout("YAW", gimbal.pan.map(Self.signed) ?? gimbal.yaw.map(Self.signed) ?? "—",
                        warning: gimbal.panNearLimit)
                if let north = gimbal.yawNorth {
                    readout("BRG", String(format: "%03.0f°", north.rounded()))
                }
            }
        }
        .padding(.horizontal, compact ? 8 : 10)
        .padding(.vertical, compact ? 6 : 8)
        .panel(cornerRadius: 18)
        .allowsHitTesting(false)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel("Gimbal attitude")
        .accessibilityValue(accessibilityText(gimbal))
    }

    private func readout(_ label: String, _ value: String, warning: Bool = false) -> some View {
        HStack(spacing: 4) {
            Text(label)
                .font(AppFont.display(compact ? 9 : 10, weight: 700))
                .tracking(1)
                .foregroundStyle(Theme.textTertiary)
            Text(value)
                .font(AppFont.figures(compact ? 12 : 13, weight: 600))
                .foregroundStyle(warning ? Theme.warning : Theme.text)
        }
    }

    private func accessibilityText(_ gimbal: GimbalStatus) -> String {
        guard gimbal.valid else { return "No data" }
        var parts = ["pitch \(Int(gimbal.pitch.rounded())) degrees"]
        if let yaw = gimbal.pan ?? gimbal.yaw {
            let degrees = Int(yaw.rounded())
            parts.append(degrees == 0 ? "looking forward"
                         : "yaw \(abs(degrees)) degrees \(degrees > 0 ? "right" : "left")")
        }
        if gimbal.panNearLimit {
            parts.append("near the pan limit")
        }
        if let north = gimbal.yawNorth {
            parts.append("bearing \(Int(north.rounded()))")
        }
        return parts.joined(separator: ", ")
    }

    static func signed(_ value: Float) -> String {
        let degrees = Int(value.rounded())
        return degrees == 0 ? "0°" : String(format: "%+ld°", degrees)
    }
}

/// The drawing itself, redrawn only when one of its inputs changes.
struct AttitudeDial: View, Equatable {
    var pitch: Float?
    var yaw: Float?
    var yawNorth: Float?
    var heading: Float?
    var fieldOfView: Float?

    /// Room around the compass for the pitch arc and its marker.
    static let margin: CGFloat = 16

    private static let pitchTop: Double = 30
    private static let pitchBottom: Double = -90

    var body: some View {
        Canvas { context, size in
            draw(in: &context, size: size)
        }
    }

    private func draw(in context: inout GraphicsContext, size: CGSize) {
        let center = CGPoint(x: size.width / 2, y: size.height / 2)
        let radius = min(size.width, size.height) / 2 - Self.margin

        // Heading up when the camera's direction against the nose is known, North up when only
        // its bearing is (the aircraft's orientation is then unknown and it is not drawn).
        let showAircraft = yaw != nil || yawNorth == nil
        let ringRotation = showAircraft ? Double(heading ?? 0) : 0
        let compass = showAircraft ? heading != nil : true
        let coneAngle = yaw.map(Double.init) ?? yawNorth.map(Double.init)

        // Face
        let face = Path(ellipseIn: CGRect(x: center.x - radius, y: center.y - radius,
                                          width: radius * 2, height: radius * 2))
        context.fill(face, with: .color(Theme.background.opacity(0.55)))
        context.stroke(face, with: .color(Theme.stroke), lineWidth: 1)

        drawRing(in: &context, center: center, radius: radius, rotation: ringRotation, compass: compass)

        if let coneAngle {
            drawCone(in: &context, center: center, radius: radius, angle: coneAngle)
        }
        if showAircraft {
            drawAircraft(in: &context, center: center, size: radius * 0.42,
                         dimmed: pitch == nil)
        }
        // Lubber line: the aircraft's nose (or North) on the ring.
        var lubber = Path()
        lubber.move(to: CGPoint(x: center.x, y: center.y - radius + 7))
        lubber.addLine(to: CGPoint(x: center.x - 4, y: center.y - radius - 1))
        lubber.addLine(to: CGPoint(x: center.x + 4, y: center.y - radius - 1))
        lubber.closeSubpath()
        context.fill(lubber, with: .color(Theme.text))

        drawPitchArc(in: &context, center: center, radius: radius + 9)
    }

    // MARK: Compass ring

    private func drawRing(in context: inout GraphicsContext, center: CGPoint, radius: CGFloat,
                          rotation: Double, compass: Bool) {
        var minor = Path()
        var major = Path()
        for bearing in stride(from: 0, to: 360, by: 10) {
            let cardinal = bearing % 90 == 0
            if compass && cardinal {
                continue // letters instead
            }
            let isMajor = bearing % 30 == 0
            let screen = Double(bearing) - rotation
            let outer = Self.point(center, radius - 2, screen)
            let inner = Self.point(center, radius - (isMajor ? 9 : 5), screen)
            if isMajor {
                major.move(to: outer)
                major.addLine(to: inner)
            } else {
                minor.move(to: outer)
                minor.addLine(to: inner)
            }
        }
        context.stroke(minor, with: .color(Theme.textTertiary), lineWidth: 1)
        context.stroke(major, with: .color(Theme.textSecondary), lineWidth: 1.5)

        guard compass else { return }
        let fontSize = max(9, radius * 0.15)
        for (bearing, letter) in [(0, "N"), (90, "E"), (180, "S"), (270, "W")] {
            let screen = Double(bearing) - rotation
            let label = Text(letter)
                .font(AppFont.display(fontSize, weight: 700))
                .foregroundStyle(bearing == 0 ? Theme.magenta : Theme.textSecondary)
            context.draw(label, at: Self.point(center, radius - fontSize * 0.8, screen), anchor: .center)
        }
    }

    // MARK: Camera cone

    private func drawCone(in context: inout GraphicsContext, center: CGPoint, radius: CGFloat,
                          angle: Double) {
        let half = min(max(Double(fieldOfView ?? 30) / 2, 3), 40)
        let length = radius * 0.86
        var wedge = Path()
        wedge.move(to: center)
        wedge.addLine(to: Self.point(center, length, angle - half))
        wedge.addArc(center: center, radius: length,
                     startAngle: .degrees(angle - half - 90), endAngle: .degrees(angle + half - 90),
                     clockwise: false)
        wedge.closeSubpath()
        context.fill(wedge, with: .radialGradient(
            Gradient(colors: [Theme.magenta.opacity(0.6), Theme.fuchsia.opacity(0.12)]),
            center: center, startRadius: 0, endRadius: length))
        context.stroke(wedge, with: .color(Theme.magenta.opacity(0.9)), lineWidth: 1)

        var axis = Path()
        axis.move(to: center)
        axis.addLine(to: Self.point(center, radius - 3, angle))
        context.stroke(axis, with: .color(Theme.magenta), style: StrokeStyle(lineWidth: 2, lineCap: .round))
    }

    // MARK: Aircraft

    /// Fixed-wing / VTOL silhouette seen from above, nose up. Right half; x is mirrored.
    private static let aircraftOutline: [CGPoint] = [
        CGPoint(x: 0, y: 1),          // nose
        CGPoint(x: 0.07, y: 0.86),
        CGPoint(x: 0.08, y: 0.32),    // wing root, leading edge
        CGPoint(x: 1, y: 0.07),       // wing tip
        CGPoint(x: 1, y: -0.07),
        CGPoint(x: 0.08, y: 0.02),    // wing root, trailing edge
        CGPoint(x: 0.06, y: -0.6),    // tail root, leading edge
        CGPoint(x: 0.4, y: -0.76),    // tailplane tip
        CGPoint(x: 0.4, y: -0.86),
        CGPoint(x: 0.05, y: -0.84),
        CGPoint(x: 0, y: -0.92),      // tail
    ]

    private func drawAircraft(in context: inout GraphicsContext, center: CGPoint, size: CGFloat,
                              dimmed: Bool) {
        let right = Self.aircraftOutline
        let left = right.reversed().dropFirst().dropLast().map { CGPoint(x: -$0.x, y: $0.y) }
        var shape = Path()
        for (index, p) in (right + left).enumerated() {
            let point = CGPoint(x: center.x + p.x * size, y: center.y - p.y * size)
            if index == 0 {
                shape.move(to: point)
            } else {
                shape.addLine(to: point)
            }
        }
        shape.closeSubpath()
        context.fill(shape, with: .color(Theme.text.opacity(dimmed ? 0.35 : 0.92)))
        context.stroke(shape, with: .color(Theme.background.opacity(0.7)), lineWidth: 1)
    }

    // MARK: Pitch arc

    private func drawPitchArc(in context: inout GraphicsContext, center: CGPoint, radius: CGFloat) {
        // Pitch θ (up positive) sits at θ above the 3 o'clock direction, like a side view.
        func arcPoint(_ degrees: Double, _ r: CGFloat) -> CGPoint {
            let a = CGFloat(degrees * .pi / 180)
            return CGPoint(x: center.x + r * cos(a), y: center.y - r * sin(a))
        }
        var track = Path()
        track.addArc(center: center, radius: radius, startAngle: .degrees(-Self.pitchTop),
                     endAngle: .degrees(-Self.pitchBottom), clockwise: false)
        context.stroke(track, with: .color(Color.white.opacity(0.18)),
                       style: StrokeStyle(lineWidth: 3, lineCap: .round))

        var ticks = Path()
        for degrees in [0.0, -45, -90] {
            ticks.move(to: arcPoint(degrees, radius + 3))
            ticks.addLine(to: arcPoint(degrees, radius + (degrees == 0 ? 8 : 6)))
        }
        context.stroke(ticks, with: .color(Theme.textSecondary), lineWidth: 1.5)

        guard let pitch else { return }
        let value = min(max(Double(pitch), Self.pitchBottom), Self.pitchTop)
        var filled = Path()
        filled.addArc(center: center, radius: radius, startAngle: .degrees(-max(value, 0)),
                      endAngle: .degrees(-min(value, 0)), clockwise: false)
        context.stroke(filled, with: .linearGradient(
            Gradient(colors: [Theme.magenta, Theme.purple]),
            startPoint: arcPoint(0, radius), endPoint: arcPoint(value, radius)),
                       style: StrokeStyle(lineWidth: 3, lineCap: .round))

        let marker = arcPoint(value, radius)
        let dot = Path(ellipseIn: CGRect(x: marker.x - 5, y: marker.y - 5, width: 10, height: 10))
        context.fill(dot, with: .color(Theme.magenta))
        context.stroke(dot, with: .color(Theme.text), lineWidth: 1.5)
    }

    /// Point at `radius` from `center`, `degrees` clockwise from straight up.
    private static func point(_ center: CGPoint, _ radius: CGFloat, _ degrees: Double) -> CGPoint {
        let a = CGFloat(degrees * .pi / 180)
        return CGPoint(x: center.x + radius * sin(a), y: center.y - radius * cos(a))
    }
}
