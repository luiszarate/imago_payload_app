import SwiftUI

/// Controller mapping: pick a function, then press the button or move the axis for it.
struct GamepadSettingsSections: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        Section {
            HStack(spacing: 10) {
                Image(systemName: model.gamepadName == nil ? "gamecontroller" : "gamecontroller.fill")
                    .foregroundStyle(model.gamepadName == nil ? Theme.textTertiary : Theme.ok)
                Text(model.gamepadName ?? "No controller connected")
            }
        } header: {
            Text("Controller")
        } footer: {
            Text("Xbox, PlayStation and MFi controllers work, and a HOTAS or flight stick that presents itself as one of them (most console HOTAS do). Pair it in iPad Settings › Bluetooth or plug it in by USB-C.")
        }

        Section {
            ForEach(GamepadAxisFunction.allCases) { function in
                axisRow(function)
            }
        } header: {
            Text("Axes")
        } footer: {
            Text("Assign, then move the stick, hat, throttle or trigger fully in the direction that should mean right, up or zoom in.")
        }

        Section {
            ForEach(GamepadAction.allCases) { action in
                actionRow(action)
            }
        } header: {
            Text("Buttons")
        } footer: {
            Text("Assign, then press the button. A button does one thing: assigning it again moves it.")
        }

        Section {
            Button("Restore default mapping", role: .destructive) {
                model.learnGamepadInput(nil)
                model.gamepadMapping = .standard
            }
        }
    }

    private func axisRow(_ function: GamepadAxisFunction) -> some View {
        let binding = model.gamepadMapping.axes[function]
        let learning = model.gamepadLearning == .axis(function)
        return VStack(alignment: .leading, spacing: 6) {
            HStack {
                VStack(alignment: .leading, spacing: 2) {
                    Text(function.title)
                    Text(learning ? "Move the axis now…" : (binding?.source.label ?? "Not assigned"))
                        .font(AppFont.body(12))
                        .foregroundStyle(learning ? Theme.magenta : Theme.textSecondary)
                }
                Spacer()
                assignButton(learning: learning) { model.learnGamepadInput(learning ? nil : .axis(function)) }
            }
            if binding != nil {
                Toggle("Invert", isOn: Binding(
                    get: { binding?.inverted ?? false },
                    set: { model.setGamepadAxis(function, inverted: $0) }))
                    .font(AppFont.body(13))
            }
        }
        .swipeActions {
            if binding != nil {
                Button("Clear", role: .destructive) { model.gamepadMapping.unbind(function) }
            }
        }
    }

    private func actionRow(_ action: GamepadAction) -> some View {
        let element = model.gamepadMapping.button(for: action)
        let learning = model.gamepadLearning == .action(action)
        return HStack {
            VStack(alignment: .leading, spacing: 2) {
                Text(action.title)
                Text(learning ? "Press a button now…" : (element ?? "Not assigned"))
                    .font(AppFont.body(12))
                    .foregroundStyle(learning ? Theme.magenta : Theme.textSecondary)
            }
            Spacer()
            assignButton(learning: learning) { model.learnGamepadInput(learning ? nil : .action(action)) }
        }
        .swipeActions {
            if element != nil {
                Button("Clear", role: .destructive) { model.gamepadMapping.unbind(action) }
            }
        }
    }

    private func assignButton(learning: Bool, action: @escaping () -> Void) -> some View {
        Button(learning ? "Cancel" : "Assign", action: action)
            .buttonStyle(.bordered)
            .disabled(model.gamepadName == nil && !learning)
    }
}
