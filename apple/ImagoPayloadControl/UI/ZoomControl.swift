import SwiftUI

/// Zoom level, hold buttons and a slider between them.
///
/// The slider is logarithmic, so every doubling gets the same travel and 1–25× keeps most of
/// the length even when the combined range reaches 300×. Its range follows the camera's zoom
/// mode; the digital part, above 25×, is marked. Dragging moves the thumb with the finger
/// (a tap does not jump), and the thumb shows the level asked for while a marker beside the
/// track shows where the lens actually is until it gets there.
struct ZoomControl: View {
    @Environment(AppModel.self) private var model
    let compact: Bool

    /// Level asked for that the lens has not reached yet.
    @State private var target: Float?
    /// Level when the current drag began.
    @State private var dragStart: Float?
    /// Bumped on every release, to restart the timeout that gives up on an unreached target.
    @State private var releases = 0
    @GestureState private var touching = false

    /// Range used until the camera reports its zoom mode.
    private static let fallbackMax: Float = 300
    private static let thumbSize: CGFloat = 24
    private static let width: CGFloat = 56

    private var trackLength: CGFloat { compact ? 140 : 190 }
    private var maxLevel: Float { max(model.camera.zoomMax ?? Self.fallbackMax, 2) }
    private var actual: Float { model.camera.zoom > 0 ? model.camera.zoom : 1 }
    private var shown: Float { target ?? actual }

    var body: some View {
        VStack(spacing: compact ? 6 : 8) {
            levelText
            HoldButton(systemImage: "plus.magnifyingglass", accessibilityText: "Zoom in",
                       onPress: { model.zoom(1) }, onRelease: { model.zoom(0) },
                       height: compact ? 44 : 52)
            slider
            HoldButton(systemImage: "minus.magnifyingglass", accessibilityText: "Zoom out",
                       onPress: { model.zoom(-1) }, onRelease: { model.zoom(0) },
                       height: compact ? 44 : 52)
        }
        .onChange(of: model.zoomDirection) { _, direction in
            if direction != 0 {
                target = nil // the buttons or a pinch took over
            }
        }
        .onChange(of: model.camera.zoom) { _, zoom in
            if let target, dragStart == nil, abs(zoom - target) <= max(0.1, target * 0.03) {
                self.target = nil
            }
        }
        .task(id: releases) {
            // A lens that never gets there (refused, clamped, lost): follow the camera again.
            try? await Task.sleep(for: .seconds(5))
            if !Task.isCancelled && dragStart == nil {
                target = nil
            }
        }
    }

    private var levelText: some View {
        let digital = shown > CameraStatus.opticalZoomMax + 0.05
        return Text(model.camera.zoom > 0 || target != nil ? Self.format(shown) : "—")
            .font(AppFont.figures(13, weight: 600))
            .foregroundStyle(digital ? Theme.warning : Theme.text)
            .frame(height: 16)
            .accessibilityHidden(true)
    }

    // MARK: Slider

    private var slider: some View {
        let max = maxLevel
        return ZStack(alignment: .top) {
            ZoomTrack(level: shown, actual: target != nil && model.camera.zoom > 0 ? actual : nil,
                      maxLevel: max, inset: Self.thumbSize / 2)
                .equatable()
            Circle()
                .fill(Theme.text)
                .overlay(Circle().stroke(Theme.magenta, lineWidth: 2.5))
                .frame(width: Self.thumbSize, height: Self.thumbSize)
                .scaleEffect(dragStart != nil ? 1.15 : 1)
                .offset(x: ZoomTrack.trackOffset,
                        y: (1 - Self.position(shown, max)) * (trackLength - Self.thumbSize))
                .animation(dragStart == nil ? .linear(duration: 0.2) : nil, value: shown)
                .animation(.easeOut(duration: 0.1), value: dragStart != nil)
        }
        .frame(width: Self.width, height: trackLength)
        .contentShape(Rectangle())
        .gesture(
            DragGesture(minimumDistance: 0)
                .updating($touching) { _, state, _ in state = true }
                .onChanged { drag(by: $0.translation.height) }
                .onEnded { _ in release() }
        )
        .onChange(of: touching) { _, isTouching in
            if !isTouching {
                release() // also when iOS cancels the gesture
            }
        }
        .accessibilityElement()
        .accessibilityLabel("Zoom level")
        .accessibilityValue(Self.format(shown))
        .accessibilityAdjustableAction { direction in
            let factor: Float = direction == .increment ? 1.5 : 1 / 1.5
            send(Swift.min(Swift.max(shown * factor, 1), maxLevel))
            releases += 1
        }
    }

    private func drag(by dy: CGFloat) {
        let start = dragStart ?? shown
        if dragStart == nil {
            dragStart = start
            Haptics.tap()
        }
        let travel = trackLength - Self.thumbSize
        let position = Swift.min(Swift.max(Self.position(start, maxLevel) - dy / travel, 0), 1)
        let level = (Self.level(at: position, maxLevel) * 10).rounded() / 10
        guard level != shown else { return }
        // A tick when crossing into or out of digital zoom.
        let optical = CameraStatus.opticalZoomMax
        if maxLevel > optical && (shown > optical) != (level > optical) {
            Haptics.tap()
        }
        send(level)
    }

    private func send(_ level: Float) {
        target = level
        model.zoomTo(level)
    }

    private func release() {
        guard dragStart != nil else { return }
        dragStart = nil
        releases += 1
    }

    // MARK: Scale

    /// 0 at 1×, 1 at the top of the range.
    static func position(_ level: Float, _ maxLevel: Float) -> CGFloat {
        CGFloat(Swift.min(Swift.max(log(Swift.max(level, 1)) / log(maxLevel), 0), 1))
    }

    static func level(at position: CGFloat, _ maxLevel: Float) -> Float {
        exp(Float(position) * log(maxLevel))
    }

    static func format(_ level: Float) -> String {
        level >= 100 ? String(format: "%.0f×", level) : String(format: "%.1f×", level)
    }
}

/// Track, scale and the lens marker, redrawn only when one of them changes.
private struct ZoomTrack: View, Equatable {
    var level: Float
    /// The lens's level while it travels to `level`.
    var actual: Float?
    var maxLevel: Float
    /// Distance from each end to the centre of the thumb at that end.
    var inset: CGFloat

    /// The track sits right of centre, leaving room for the labels on its left.
    static let trackOffset: CGFloat = 9

    var body: some View {
        Canvas { context, size in
            draw(in: &context, size: size)
        }
    }

    private func draw(in context: inout GraphicsContext, size: CGSize) {
        let x = size.width / 2 + Self.trackOffset
        let bottom = size.height - inset
        let top = inset
        func y(_ level: Float) -> CGFloat {
            bottom - ZoomControl.position(level, maxLevel) * (bottom - top)
        }
        let optical = CameraStatus.opticalZoomMax
        let hasDigital = maxLevel > optical

        func segment(_ from: CGFloat, _ to: CGFloat) -> Path {
            var path = Path()
            path.move(to: CGPoint(x: x, y: from))
            path.addLine(to: CGPoint(x: x, y: to))
            return path
        }
        let round = StrokeStyle(lineWidth: 4, lineCap: .round)

        // Optical range, then the digital range dashed in amber.
        let opticalTop = hasDigital ? y(optical) : top
        context.stroke(segment(bottom, opticalTop), with: .color(Color.white.opacity(0.18)),
                       style: round)
        if hasDigital {
            context.stroke(segment(opticalTop - 3, top), with: .color(Theme.warning.opacity(0.55)),
                           style: StrokeStyle(lineWidth: 4, lineCap: .butt, dash: [3, 3]))
        }

        // Fill up to the level asked for.
        let levelY = y(level)
        context.stroke(segment(bottom, levelY), with: .linearGradient(
            Gradient(colors: [Theme.purple, Theme.magenta]),
            startPoint: CGPoint(x: x, y: bottom), endPoint: CGPoint(x: x, y: levelY)),
                       style: round)

        // Scale
        let marks: [Float] = hasDigital ? [1, 5, optical, 100, maxLevel] : [1, 2, 5, 10, optical]
        for mark in marks where mark <= maxLevel {
            let markY = y(mark)
            let boundary = hasDigital && mark == optical
            var tick = Path()
            tick.move(to: CGPoint(x: x - (boundary ? 7 : 5), y: markY))
            tick.addLine(to: CGPoint(x: x + (boundary ? 7 : -3), y: markY))
            context.stroke(tick, with: .color(boundary ? Theme.warning : Theme.textTertiary),
                           lineWidth: boundary ? 1.5 : 1)
            let label = Text(String(format: "%.0f", mark))
                .font(AppFont.figures(9, weight: 600))
                .foregroundStyle(boundary ? Theme.warning : Theme.textSecondary)
            context.draw(label, at: CGPoint(x: x - 9, y: markY), anchor: .trailing)
        }

        // Where the lens is while it travels.
        if let actual {
            let ay = y(actual)
            var marker = Path()
            marker.move(to: CGPoint(x: x + 5, y: ay))
            marker.addLine(to: CGPoint(x: x + 12, y: ay - 4.5))
            marker.addLine(to: CGPoint(x: x + 12, y: ay + 4.5))
            marker.closeSubpath()
            context.fill(marker, with: .color(Theme.text))
        }
    }
}
