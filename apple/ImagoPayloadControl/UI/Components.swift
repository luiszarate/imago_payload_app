import SwiftUI
import UIKit

/// Square icon button for the side rails. Targets are at least 52 pt for gloved use in the field.
struct RailButton: View {
    let systemImage: String
    var label: String?
    var active = false
    var tint: Color = Theme.text
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            VStack(spacing: 3) {
                Image(systemName: systemImage)
                    .font(.system(size: 19, weight: .semibold))
                if let label {
                    Text(label)
                        .font(AppFont.display(10, weight: 600))
                        .textCase(.uppercase)
                }
            }
            .foregroundStyle(active ? Theme.text : tint)
            .frame(width: 56, height: 52)
            .background {
                RoundedRectangle(cornerRadius: 13, style: .continuous)
                    .fill(active ? AnyShapeStyle(Theme.accent) : AnyShapeStyle(Theme.surfaceRaised))
            }
        }
        .buttonStyle(PressScaleStyle())
        .accessibilityLabel(label ?? systemImage)
    }
}

/// Button that acts while held: zoom in/out, focus. Fires onPress once and onRelease once.
struct HoldButton: View {
    let systemImage: String
    let accessibilityText: String
    let onPress: () -> Void
    let onRelease: () -> Void
    @State private var pressed = false
    /// Resets even when iOS cancels the gesture, so a zoom can never be left running.
    @GestureState private var touching = false

    var body: some View {
        Image(systemName: systemImage)
            .font(.system(size: 19, weight: .semibold))
            .foregroundStyle(Theme.text)
            .frame(width: 56, height: 52)
            .background {
                RoundedRectangle(cornerRadius: 13, style: .continuous)
                    .fill(pressed ? AnyShapeStyle(Theme.accent) : AnyShapeStyle(Theme.surfaceRaised))
            }
            .scaleEffect(pressed ? 0.94 : 1)
            .gesture(
                DragGesture(minimumDistance: 0)
                    .updating($touching) { _, state, _ in state = true }
                    .onChanged { _ in
                        if !pressed {
                            pressed = true
                            Haptics.tap()
                            onPress()
                        }
                    }
                    .onEnded { _ in release() }
            )
            .onChange(of: touching) { _, isTouching in
                if !isTouching {
                    release()
                }
            }
            .onDisappear { release() }
            .accessibilityLabel(accessibilityText)
            .accessibilityAddTraits(.isButton)
            .accessibilityAction {
                // VoiceOver cannot hold: act for half a second.
                onPress()
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { onRelease() }
            }
    }

    private func release() {
        guard pressed else { return }
        pressed = false
        onRelease()
    }
}

struct PressScaleStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .scaleEffect(configuration.isPressed ? 0.94 : 1)
            .animation(.easeOut(duration: 0.08), value: configuration.isPressed)
    }
}

/// Small rounded label for status values in the top bar and HUD.
struct StatusChip: View {
    let text: String
    var systemImage: String?
    var color: Color = Theme.textSecondary

    var body: some View {
        HStack(spacing: 5) {
            if let systemImage {
                Image(systemName: systemImage)
                    .font(.system(size: 11, weight: .semibold))
            }
            Text(text)
                .font(AppFont.figures(12, weight: 600))
        }
        .foregroundStyle(color)
        .padding(.horizontal, 9)
        .padding(.vertical, 5)
        .background(Theme.surfaceRaised.opacity(0.9), in: Capsule())
    }
}

/// "IMAGO" wordmark in the brand gradient, until the official logo is added.
struct Wordmark: View {
    var size: CGFloat = 17

    var body: some View {
        Text("IMAGO")
            .font(AppFont.display(size, weight: 700))
            .tracking(size * 0.22)
            .foregroundStyle(Theme.accent)
            .accessibilityLabel("Imago Aerospace")
    }
}

enum Haptics {
    private static let light: UIImpactFeedbackGenerator = {
        let generator = UIImpactFeedbackGenerator(style: .light)
        generator.prepare()
        return generator
    }()

    static func tap() {
        light.impactOccurred()
        light.prepare()
    }

    static func confirm() {
        UINotificationFeedbackGenerator().notificationOccurred(.success)
    }

    static func error() {
        UINotificationFeedbackGenerator().notificationOccurred(.error)
    }
}
