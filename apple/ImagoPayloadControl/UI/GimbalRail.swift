import SwiftUI

/// Right rail: gimbal mode, recenter, tracking tools and the joystick.
struct GimbalRail: View {
    @Environment(AppModel.self) private var model
    let compact: Bool

    var body: some View {
        VStack(spacing: compact ? 6 : 10) {
            HStack(spacing: compact ? 6 : 8) {
                modeMenu
                RailButton(systemImage: "scope", label: "Center") {
                    model.recenterGimbal()
                }
            }
            HStack(spacing: compact ? 6 : 8) {
                RailButton(systemImage: "sparkles.rectangle.stack", label: "AI",
                           active: model.detectionEnabled) {
                    model.toggleDetection()
                }
                RailButton(systemImage: "hand.point.up.left", label: "Point",
                           active: model.pointMode) {
                    model.pointMode.toggle()
                }
            }
            if model.tracking.state != .idle {
                Button {
                    model.stopTracking()
                } label: {
                    Label(model.tracking.state == .lost ? "Lost · Stop" : "Stop track",
                          systemImage: "xmark.circle.fill")
                        .font(AppFont.display(12, weight: 700))
                        .foregroundStyle(Theme.text)
                        .frame(maxWidth: .infinity, minHeight: 34)
                        .background(Theme.danger.opacity(0.85),
                                    in: RoundedRectangle(cornerRadius: 11, style: .continuous))
                }
                .buttonStyle(PressScaleStyle())
            }
            JoystickView(diameter: compact ? 112 : 136) { x, y in
                model.joystickMoved(x: x, y: y)
            }
            .padding(.top, 2)
            attitude
        }
        .frame(width: compact ? 132 : 148)
        .padding(compact ? 6 : 8)
        .panel()
    }

    private var modeMenu: some View {
        Menu {
            ForEach([GimbalMode.lock, .follow, .mapping, .off]) { mode in
                Button {
                    model.setGimbalMode(mode)
                } label: {
                    if model.gimbal.mode == mode {
                        Label(mode.title, systemImage: "checkmark")
                    } else {
                        Text(mode.title)
                    }
                }
            }
        } label: {
            VStack(spacing: 3) {
                Image(systemName: "gyroscope")
                    .font(.system(size: 17, weight: .semibold))
                Text(model.gimbal.mode?.title ?? "Mode")
                    .font(AppFont.display(10, weight: 600))
                    .textCase(.uppercase)
                    .lineLimit(1)
                    .minimumScaleFactor(0.7)
            }
            .foregroundStyle(Theme.text)
            .frame(width: 56, height: 52)
            .background(Theme.surfaceRaised, in: RoundedRectangle(cornerRadius: 13, style: .continuous))
        }
        .accessibilityLabel("Gimbal mode")
    }

    private var attitude: some View {
        HStack(spacing: 10) {
            Text(model.gimbal.valid ? String(format: "P %+.1f°", model.gimbal.pitch) : "P —")
            Text(model.gimbal.yaw.map { String(format: "Y %+.1f°", $0) } ?? "Y —")
        }
        .font(AppFont.figures(12, weight: 600))
        .foregroundStyle(Theme.textSecondary)
    }
}
