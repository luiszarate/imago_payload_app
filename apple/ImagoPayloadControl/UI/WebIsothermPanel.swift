import ImagoCore
import SwiftUI

/// The isotherm of the payload's web settings page (our Orus L has it only there): one
/// threshold, everything warmer is coloured. On/off and the presets go at once; a threshold of
/// one's own goes with Apply. Until the page has answered it says why nothing can be set.
struct WebIsothermPanel: View {
    @Environment(AppModel.self) private var model
    @State private var draft: Int?

    var body: some View {
        let web = model.webIsotherm
        let threshold = draft ?? web?.thresholdC ?? 35
        VStack(alignment: .leading, spacing: 14) {
            HStack {
                Text("ISOTHERM")
                    .font(AppFont.display(13, weight: 700))
                    .tracking(1.2)
                    .foregroundStyle(Theme.text)
                Spacer()
                Toggle("Isotherm", isOn: Binding(get: { web?.enabled == true },
                                                 set: { model.setWebIsotherm(on: $0) }))
                    .labelsHidden()
                    .tint(Theme.magenta)
                    .disabled(web == nil)
            }
            Text(status(web))
                .font(AppFont.body(12))
                .foregroundStyle(web == nil && model.webIsothermFailed ? Theme.warning : Theme.textSecondary)
                .fixedSize(horizontal: false, vertical: true)

            HStack(spacing: 8) {
                ForEach(WebIsotherm.presets, id: \.threshold) { preset in
                    presetButton(preset, current: web)
                }
            }
            .disabled(web == nil)

            HStack {
                Text("Threshold")
                    .font(AppFont.body(14, weight: 600))
                    .foregroundStyle(Theme.textSecondary)
                Spacer()
                stepper("minus", "Threshold lower", enabled: web != nil && threshold > WebIsotherm.thresholdLimits.lowerBound) {
                    draft = max(WebIsotherm.thresholdLimits.lowerBound, threshold - 1)
                }
                Text(web == nil ? "— °C" : "\(threshold) °C")
                    .font(AppFont.figures(16, weight: 600))
                    .foregroundStyle(Theme.text)
                    .frame(width: 70)
                stepper("plus", "Threshold higher", enabled: web != nil && threshold < WebIsotherm.thresholdLimits.upperBound) {
                    draft = min(WebIsotherm.thresholdLimits.upperBound, threshold + 1)
                }
            }
            if threshold > Int(IMAGO_WEB_ISOTHERM_LOW_RANGE_MAX_C) {
                Text("Over 150 °C the camera measures −20 to 550 °C.")
                    .font(AppFont.body(11))
                    .foregroundStyle(Theme.textSecondary)
            }

            Button {
                model.setWebIsotherm(on: true, thresholdC: threshold)
            } label: {
                Text(web?.enabled == true ? "Apply \(threshold) °C" : "Turn on \(threshold) °C")
                    .font(AppFont.display(14, weight: 700))
                    .foregroundStyle(Theme.text)
                    .frame(maxWidth: .infinity, minHeight: 46)
                    .background(Theme.accent, in: RoundedRectangle(cornerRadius: 12, style: .continuous))
                    .opacity(web == nil ? 0.4 : 1)
            }
            .buttonStyle(PressScaleStyle())
            .disabled(web == nil)
        }
        .padding(16)
        .frame(width: 340)
        .background(Theme.surface)
        .preferredColorScheme(.dark)
    }

    private func status(_ web: WebIsotherm?) -> String {
        if web != nil {
            return "The payload's own isotherm: everything warmer than the threshold is coloured. Saved on its web page; it shows at once."
        }
        if model.webIsothermFailed {
            return "The payload's web page does not answer (the payload IP in the profile, port 8000). The app keeps trying."
        }
        return "Reading the payload's web page…"
    }

    private func presetButton(_ preset: (threshold: Int, name: String), current: WebIsotherm?) -> some View {
        let lit = current?.enabled == true && current?.thresholdC == preset.threshold
        return Button {
            draft = preset.threshold
            model.setWebIsotherm(on: true, thresholdC: preset.threshold)
        } label: {
            VStack(spacing: 2) {
                Text(preset.name)
                    .font(AppFont.display(12, weight: 700))
                Text("\(preset.threshold) °C")
                    .font(AppFont.figures(10, weight: 500))
                    .foregroundStyle(lit ? Theme.text : Theme.textSecondary)
            }
            .foregroundStyle(current == nil ? Theme.textTertiary : Theme.text)
            .frame(maxWidth: .infinity, minHeight: 46)
            .background {
                RoundedRectangle(cornerRadius: 11, style: .continuous)
                    .fill(lit ? AnyShapeStyle(Theme.accent) : AnyShapeStyle(Theme.surfaceRaised))
            }
        }
        .buttonStyle(PressScaleStyle())
        .accessibilityLabel("\(preset.name), \(preset.threshold) degrees")
    }

    private func stepper(_ systemImage: String, _ label: String, enabled: Bool,
                         action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Image(systemName: systemImage)
                .font(.system(size: 14, weight: .bold))
                .foregroundStyle(enabled ? Theme.text : Theme.textTertiary)
                .frame(width: 44, height: 40)
                .background(Theme.surfaceRaised, in: RoundedRectangle(cornerRadius: 10, style: .continuous))
        }
        .buttonStyle(PressScaleStyle())
        .buttonRepeatBehavior(.enabled)
        .disabled(!enabled)
        .accessibilityLabel(label)
    }
}
