import SwiftUI

/// Thermal controls along the bottom while the IR picture is the main one (view IR or IR + EO):
/// palette, the picture's temperature scale, IR zoom, flat-field correction and, when the
/// camera has it, the isotherm. On a tablet it sits in the black band under the 16:9 picture.
struct ThermalBar: View {
    @Environment(AppModel.self) private var model
    let compact: Bool
    @State private var showIsotherm = false

    static func height(compact: Bool) -> CGFloat { compact ? 52 : 68 }
    private var buttonHeight: CGFloat { compact ? 42 : 52 }

    var body: some View {
        let thermal = model.thermal
        let palette = model.irPalette
        HStack(spacing: compact ? 6 : 10) {
            paletteMenu(palette, cameraType: thermal.cameraType)
            TemperatureScale(colors: IRPalette.colors(palette ?? 0, cameraType: thermal.cameraType)
                                .map { Color(hex: $0) },
                             minC: thermal.minC, maxC: thermal.maxC, meanC: thermal.meanC,
                             band: model.isothermOn ? model.isothermBand : nil,
                             compact: compact)
                .frame(minWidth: compact ? 90 : 140, maxWidth: .infinity)
            zoomStepper
            ffcMenu(auto: thermal.ffcAuto)
            if model.isothermSupported {
                isothermButton
            }
        }
        .padding(.horizontal, compact ? 5 : 8)
        .frame(height: Self.height(compact: compact))
        .panel(cornerRadius: 16)
    }

    // MARK: Palette

    private func paletteMenu(_ current: Int?, cameraType: Int32) -> some View {
        Menu {
            ForEach(0..<IRPalette.count, id: \.self) { index in
                Button {
                    model.setIRPalette(index)
                } label: {
                    if current == index {
                        Label(IRPalette.name(index, cameraType: cameraType), systemImage: "checkmark")
                    } else {
                        Text(IRPalette.name(index, cameraType: cameraType))
                    }
                }
            }
        } label: {
            HStack(spacing: 7) {
                RoundedRectangle(cornerRadius: 4, style: .continuous)
                    .fill(LinearGradient(colors: IRPalette.colors(current ?? 0, cameraType: cameraType)
                                            .map { Color(hex: $0) },
                                         startPoint: .bottom, endPoint: .top))
                    .overlay(RoundedRectangle(cornerRadius: 4, style: .continuous).stroke(Theme.stroke))
                    .frame(width: compact ? 12 : 14, height: compact ? 26 : 32)
                VStack(alignment: .leading, spacing: 1) {
                    Text("PALETTE")
                        .font(AppFont.display(compact ? 8 : 9, weight: 700))
                        .tracking(1)
                        .foregroundStyle(Theme.textTertiary)
                    Text(current.map { IRPalette.name($0, cameraType: cameraType) } ?? "—")
                        .font(AppFont.display(compact ? 12 : 14, weight: 600))
                        .foregroundStyle(Theme.text)
                        .lineLimit(1)
                        .minimumScaleFactor(0.75)
                }
                Image(systemName: "chevron.up.chevron.down")
                    .font(.system(size: 10, weight: .semibold))
                    .foregroundStyle(Theme.textSecondary)
            }
            .padding(.horizontal, compact ? 8 : 10)
            .frame(width: compact ? 118 : 150, height: buttonHeight, alignment: .leading)
            .background(Theme.surfaceRaised, in: RoundedRectangle(cornerRadius: 12, style: .continuous))
        }
        .accessibilityLabel("IR palette")
        .accessibilityValue(current.map { IRPalette.name($0, cameraType: cameraType) } ?? "Unknown")
    }

    // MARK: Zoom

    private var zoomStepper: some View {
        let zoom = model.irZoom
        return HStack(spacing: 0) {
            stepButton("minus", "IR zoom out", enabled: (zoom ?? 1) > 1) { model.stepIRZoom(-1) }
            Text(zoom.map { "\($0)×" } ?? "—")
                .font(AppFont.figures(compact ? 13 : 15, weight: 600))
                .foregroundStyle(Theme.text)
                .frame(width: compact ? 28 : 34)
                .accessibilityLabel("IR zoom \(zoom ?? 1) times")
            stepButton("plus", "IR zoom in", enabled: (zoom ?? 1) < 8) { model.stepIRZoom(1) }
        }
        .frame(height: buttonHeight)
        .background(Theme.surfaceRaised, in: RoundedRectangle(cornerRadius: 12, style: .continuous))
    }

    private func stepButton(_ systemImage: String, _ label: String, enabled: Bool,
                            action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Image(systemName: systemImage)
                .font(.system(size: compact ? 13 : 15, weight: .bold))
                .foregroundStyle(enabled ? Theme.text : Theme.textTertiary)
                .frame(width: compact ? 34 : 42, height: buttonHeight)
                .contentShape(Rectangle())
        }
        .buttonStyle(PressScaleStyle())
        .disabled(!enabled)
        .accessibilityLabel(label)
    }

    // MARK: FFC

    /// Tap: recalibrate now. Hold: automatic or manual.
    private func ffcMenu(auto: Bool?) -> some View {
        Menu {
            Button {
                model.triggerFFC()
            } label: {
                Label("Calibrate now", systemImage: "arrow.triangle.2.circlepath")
            }
            Toggle("Automatic FFC", isOn: Binding(get: { auto == true },
                                                  set: { model.setFFCAuto($0) }))
        } label: {
            barLabel(systemImage: "arrow.triangle.2.circlepath", title: "FFC",
                     detail: auto.map { $0 ? "AUTO" : "MAN" } ?? " ")
        } primaryAction: {
            model.triggerFFC()
        }
        .accessibilityLabel("Flat-field correction")
        .accessibilityHint("Recalibrates the thermal sensor. Hold for automatic or manual.")
    }

    // MARK: Isotherm

    private var isothermButton: some View {
        let on = model.isothermOn
        let band = model.isothermBand
        return Button {
            showIsotherm = true
        } label: {
            barLabel(systemImage: "thermometer.medium", title: "ISO",
                     detail: on ? "\(band.lowerBound)–\(band.upperBound)°" : "OFF", active: on)
        }
        .buttonStyle(PressScaleStyle())
        .popover(isPresented: $showIsotherm, arrowEdge: .bottom) {
            IsothermPanel(initial: band)
                .presentationCompactAdaptation(.popover)
        }
        .accessibilityLabel("Isotherm")
        .accessibilityValue(on ? "\(band.lowerBound) to \(band.upperBound) degrees" : "Off")
    }

    private func barLabel(systemImage: String, title: String, detail: String,
                          active: Bool = false) -> some View {
        VStack(spacing: 1) {
            Image(systemName: systemImage)
                .font(.system(size: compact ? 14 : 17, weight: .semibold))
            Text(title)
                .font(AppFont.display(compact ? 9 : 10, weight: 700))
            Text(detail)
                .font(AppFont.figures(compact ? 8 : 9, weight: 600))
                .foregroundStyle(active ? Theme.text : Theme.textSecondary)
                .lineLimit(1)
                .minimumScaleFactor(0.7)
        }
        .foregroundStyle(Theme.text)
        .frame(width: compact ? 50 : 60, height: buttonHeight)
        .background {
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .fill(active ? AnyShapeStyle(Theme.accent) : AnyShapeStyle(Theme.surfaceRaised))
        }
    }
}

/// The picture's temperature range on the palette's colours: coldest on the left, hottest on
/// the right, a tick at the mean and the isotherm band outlined where it falls.
struct TemperatureScale: View {
    let colors: [Color]
    let minC: Float?
    let maxC: Float?
    let meanC: Float?
    let band: ClosedRange<Int>?
    let compact: Bool

    var body: some View {
        VStack(spacing: compact ? 3 : 5) {
            GeometryReader { geometry in
                let width = geometry.size.width
                ZStack(alignment: .leading) {
                    RoundedRectangle(cornerRadius: 3, style: .continuous)
                        .fill(LinearGradient(colors: colors, startPoint: .leading, endPoint: .trailing))
                        .overlay(RoundedRectangle(cornerRadius: 3, style: .continuous).stroke(Theme.stroke))
                    if let span = bandSpan {
                        RoundedRectangle(cornerRadius: 3, style: .continuous)
                            .stroke(Theme.danger, lineWidth: 2)
                            .frame(width: max(4, (span.upperBound - span.lowerBound) * width))
                            .offset(x: span.lowerBound * width)
                    }
                    if let mean = fraction(meanC) {
                        Capsule()
                            .fill(Theme.text)
                            .frame(width: 2, height: geometry.size.height + 6)
                            .shadow(color: .black.opacity(0.6), radius: 1)
                            .offset(x: mean * width - 1)
                    }
                }
            }
            .frame(height: compact ? 8 : 11)
            HStack(spacing: 4) {
                Text(Self.celsius(minC))
                Spacer(minLength: 2)
                Text(meanC.map { "MEAN " + Self.celsius($0) } ?? "")
                    .foregroundStyle(Theme.textSecondary)
                Spacer(minLength: 2)
                Text(Self.celsius(maxC))
            }
            .font(AppFont.figures(compact ? 10 : 12, weight: 600))
            .foregroundStyle(Theme.text)
            .lineLimit(1)
            .minimumScaleFactor(0.7)
        }
        .accessibilityElement(children: .ignore)
        .accessibilityLabel("Temperature in the picture")
        .accessibilityValue(minC == nil ? "Not reported"
                            : "from \(Self.celsius(minC)) to \(Self.celsius(maxC)), mean \(Self.celsius(meanC))")
    }

    /// Where a temperature sits on the scale, 0...1; nil without a range.
    private func fraction(_ value: Float?) -> CGFloat? {
        guard let value, let minC, let maxC, maxC > minC else { return nil }
        return CGFloat(min(max((value - minC) / (maxC - minC), 0), 1))
    }

    /// The isotherm band on the scale, nil when it lies wholly outside the picture's range.
    private var bandSpan: ClosedRange<CGFloat>? {
        guard let band, let minC, let maxC, maxC > minC,
              Float(band.upperBound) > minC, Float(band.lowerBound) < maxC,
              let low = fraction(Float(band.lowerBound)), let high = fraction(Float(band.upperBound))
        else { return nil }
        return low...high
    }

    static func celsius(_ value: Float?) -> String {
        guard let value else { return "—" }
        return String(format: abs(value) < 100 ? "%.1f°" : "%.0f°", value)
    }
}

/// Isotherm settings: the band to colour, presets, the IR gain and on/off.
struct IsothermPanel: View {
    @Environment(AppModel.self) private var model
    @State private var low: Int
    @State private var high: Int

    init(initial: ClosedRange<Int>) {
        _low = State(initialValue: initial.lowerBound)
        _high = State(initialValue: initial.upperBound)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack {
                Text("ISOTHERM")
                    .font(AppFont.display(13, weight: 700))
                    .tracking(1.2)
                    .foregroundStyle(Theme.text)
                Spacer()
                Toggle("Isotherm", isOn: Binding(get: { model.isothermOn }, set: { on in
                    if on { model.applyIsotherm(low...high) } else { model.disableIsotherm() }
                }))
                .labelsHidden()
                .tint(Theme.magenta)
            }
            Text("Colours everything between the two temperatures from black to red, over the palette.")
                .font(AppFont.body(12))
                .foregroundStyle(Theme.textSecondary)
                .fixedSize(horizontal: false, vertical: true)

            HStack(spacing: 8) {
                ForEach(IsothermBand.presets) { preset in
                    Button {
                        low = preset.range.lowerBound
                        high = preset.range.upperBound
                        model.applyIsotherm(preset.range)
                    } label: {
                        VStack(spacing: 2) {
                            Text(preset.name)
                                .font(AppFont.display(12, weight: 700))
                            Text("\(preset.range.lowerBound)–\(preset.range.upperBound) °C")
                                .font(AppFont.figures(10, weight: 500))
                                .foregroundStyle(Theme.textSecondary)
                        }
                        .foregroundStyle(Theme.text)
                        .frame(maxWidth: .infinity, minHeight: 46)
                        .background(Theme.surfaceRaised, in: RoundedRectangle(cornerRadius: 11, style: .continuous))
                    }
                    .buttonStyle(PressScaleStyle())
                }
            }

            temperatureRow("From", value: $low, range: IsothermBand.limits.lowerBound...(high - 1))
            temperatureRow("To", value: $high, range: (low + 1)...IsothermBand.limits.upperBound)

            if let lowGain = model.irLowGain {
                VStack(alignment: .leading, spacing: 6) {
                    Text("GAIN")
                        .font(AppFont.display(10, weight: 700))
                        .tracking(1)
                        .foregroundStyle(Theme.textTertiary)
                    Picker("Gain", selection: Binding(get: { lowGain }, set: { model.setIRLowGain($0) })) {
                        Text("High · to 150 °C").tag(false)
                        Text("Low · to 550 °C").tag(true)
                    }
                    .pickerStyle(.segmented)
                }
            }
            if high > IsothermBand.highGainMax && model.irLowGain != true {
                Text("Above 150 °C the camera switches to low gain.")
                    .font(AppFont.body(11))
                    .foregroundStyle(Theme.warning)
            }

            Button {
                model.applyIsotherm(low...high)
            } label: {
                Text(model.isothermOn ? "Apply \(low)–\(high) °C" : "Turn on \(low)–\(high) °C")
                    .font(AppFont.display(14, weight: 700))
                    .foregroundStyle(Theme.text)
                    .frame(maxWidth: .infinity, minHeight: 46)
                    .background(Theme.accent, in: RoundedRectangle(cornerRadius: 12, style: .continuous))
            }
            .buttonStyle(PressScaleStyle())
        }
        .padding(16)
        .frame(width: 330)
        .background(Theme.surface)
        .preferredColorScheme(.dark)
    }

    private func temperatureRow(_ title: String, value: Binding<Int>, range: ClosedRange<Int>) -> some View {
        HStack {
            Text(title)
                .font(AppFont.body(14, weight: 600))
                .foregroundStyle(Theme.textSecondary)
            Spacer()
            stepper("minus", "\(title) lower", enabled: value.wrappedValue > range.lowerBound) {
                value.wrappedValue = max(range.lowerBound, value.wrappedValue - 1)
            }
            Text("\(value.wrappedValue) °C")
                .font(AppFont.figures(16, weight: 600))
                .foregroundStyle(Theme.text)
                .frame(width: 70)
            stepper("plus", "\(title) higher", enabled: value.wrappedValue < range.upperBound) {
                value.wrappedValue = min(range.upperBound, value.wrappedValue + 1)
            }
        }
        .accessibilityElement(children: .contain)
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
