import SwiftUI

/// On-screen joystick for gimbal rate control. Reports deflection with x right and y up,
/// both -1...1, on every touch move, and (0, 0) on release.
struct JoystickView: View {
    let diameter: CGFloat
    let onChange: (Float, Float) -> Void
    @State private var knobOffset: CGSize = .zero
    @State private var active = false
    /// True while a finger is down. Unlike onEnded, it also resets when iOS cancels the gesture
    /// (Control Center, a call, an alert), so the gimbal can never be left turning.
    @GestureState private var touching = false

    var body: some View {
        let radius = diameter / 2
        let knob = diameter * 0.36
        ZStack {
            Circle()
                .fill(Theme.surfaceRaised.opacity(0.85))
            Circle()
                .stroke(active ? AnyShapeStyle(Theme.accent) : AnyShapeStyle(Theme.stroke), lineWidth: active ? 2 : 1)
            // Axis guides
            Rectangle().fill(Theme.stroke).frame(width: 1, height: diameter * 0.7)
            Rectangle().fill(Theme.stroke).frame(width: diameter * 0.7, height: 1)
            Circle()
                .fill(Theme.accent)
                .frame(width: knob, height: knob)
                .shadow(color: Theme.magenta.opacity(active ? 0.5 : 0), radius: 10)
                .offset(knobOffset)
        }
        .frame(width: diameter, height: diameter)
        .contentShape(Circle())
        .gesture(
            DragGesture(minimumDistance: 0)
                .updating($touching) { _, state, _ in state = true }
                .onChanged { value in
                    var dx = value.location.x - radius
                    var dy = value.location.y - radius
                    let length = (dx * dx + dy * dy).squareRoot()
                    let limit = radius - knob / 4
                    if length > limit {
                        dx *= limit / length
                        dy *= limit / length
                    }
                    if !active {
                        active = true
                        Haptics.tap()
                    }
                    knobOffset = CGSize(width: dx, height: dy)
                    onChange(Float(dx / limit), Float(-dy / limit))
                }
                .onEnded { _ in release() }
        )
        .onChange(of: touching) { _, isTouching in
            if !isTouching && active {
                release()
            }
        }
        .onDisappear {
            if active {
                release()
            }
        }
        .accessibilityLabel("Gimbal joystick")
    }

    private func release() {
        active = false
        withAnimation(.spring(response: 0.18, dampingFraction: 0.7)) {
            knobOffset = .zero
        }
        onChange(0, 0)
    }
}
