import AVFoundation
import SwiftUI

/// Drawn over the picture: AI detections, the tracked target and a centre reticle.
/// Tap selects a target (or re-points the gimbal in Point mode); pinch zooms, and on a Mac
/// the mouse wheel too.
struct VideoOverlay: View {
    @Environment(AppModel.self) private var model
    @State private var pinchDirection: Int32 = 0
    @GestureState private var pinching = false

    var body: some View {
        GeometryReader { geometry in
            let rect = videoRect(in: geometry.size)
            ZStack(alignment: .topLeading) {
                Color.clear
                    .contentShape(Rectangle())

                ForEach(model.tracking.detections) { detection in
                    DetectionBox(detection: detection)
                        .frame(width: detection.rect.width * rect.width,
                               height: detection.rect.height * rect.height)
                        .position(x: rect.minX + detection.rect.midX * rect.width,
                                  y: rect.minY + detection.rect.midY * rect.height)
                }

                if let box = model.tracking.box {
                    TrackingBox(lost: model.tracking.state == .lost)
                        .frame(width: max(box.width * rect.width, 24),
                               height: max(box.height * rect.height, 24))
                        .position(x: rect.minX + box.midX * rect.width,
                                  y: rect.minY + box.midY * rect.height)
                }

                Reticle(pointMode: model.pointMode)
                    .position(x: rect.midX, y: rect.midY)
            }
            .gesture(
                SpatialTapGesture()
                    .onEnded { value in
                        guard rect.contains(value.location) else { return }
                        let point = CGPoint(x: (value.location.x - rect.minX) / rect.width,
                                            y: (value.location.y - rect.minY) / rect.height)
                        model.videoTapped(at: point)
                    }
            )
            .simultaneousGesture(
                MagnifyGesture()
                    .updating($pinching) { _, state, _ in state = true }
                    .onChanged { value in
                        let direction: Int32 = value.magnification > 1.04 ? 1
                            : (value.magnification < 0.96 ? -1 : 0)
                        if direction != pinchDirection {
                            pinchDirection = direction
                            model.zoom(direction)
                        }
                    }
                    .onEnded { _ in stopPinchZoom() }
            )
            .onChange(of: pinching) { _, isPinching in
                if !isPinching {
                    stopPinchZoom() // also runs when iOS cancels the gesture
                }
            }
            #if targetEnvironment(macCatalyst)
            .overlay { ScrollZoomView { model.zoom($0) } }
            #endif
        }
    }

    private func stopPinchZoom() {
        if pinchDirection != 0 {
            pinchDirection = 0
            model.zoom(0)
        }
    }

    /// Where the picture sits inside the view (aspect fit, like the display layer).
    private func videoRect(in size: CGSize) -> CGRect {
        let picture = model.videoSize.width > 0 ? model.videoSize : CGSize(width: 16, height: 9)
        return AVMakeRect(aspectRatio: picture, insideRect: CGRect(origin: .zero, size: size))
    }
}

private struct DetectionBox: View {
    let detection: Detection

    var body: some View {
        let color = Theme.classColors[Int(detection.classID) % Theme.classColors.count]
        ZStack(alignment: .topLeading) {
            RoundedRectangle(cornerRadius: 3)
                .stroke(color, lineWidth: 2)
            Text("\(detection.label) \(Int((detection.confidence * 100).rounded()))%")
                .font(AppFont.display(11, weight: 700))
                .foregroundStyle(Color.black)
                .padding(.horizontal, 5)
                .padding(.vertical, 2)
                .background(color, in: RoundedRectangle(cornerRadius: 3))
                .fixedSize()
                .offset(y: -20)
        }
        .allowsHitTesting(false)
    }
}

/// Corner brackets in the brand gradient; amber while the target is lost.
private struct TrackingBox: View {
    let lost: Bool

    var body: some View {
        GeometryReader { geometry in
            let w = geometry.size.width
            let h = geometry.size.height
            let arm = min(w, h) * 0.28
            Path { path in
                path.move(to: CGPoint(x: 0, y: arm)); path.addLine(to: .zero); path.addLine(to: CGPoint(x: arm, y: 0))
                path.move(to: CGPoint(x: w - arm, y: 0)); path.addLine(to: CGPoint(x: w, y: 0)); path.addLine(to: CGPoint(x: w, y: arm))
                path.move(to: CGPoint(x: w, y: h - arm)); path.addLine(to: CGPoint(x: w, y: h)); path.addLine(to: CGPoint(x: w - arm, y: h))
                path.move(to: CGPoint(x: arm, y: h)); path.addLine(to: CGPoint(x: 0, y: h)); path.addLine(to: CGPoint(x: 0, y: h - arm))
            }
            .stroke(lost ? AnyShapeStyle(Theme.warning) : AnyShapeStyle(Theme.accent),
                    style: StrokeStyle(lineWidth: 3, lineCap: .round, lineJoin: .round))
            Text(lost ? "LOST" : "TRACKING")
                .font(AppFont.display(10, weight: 700))
                .tracking(1.2)
                .foregroundStyle(lost ? Theme.warning : Theme.magenta)
                .fixedSize()
                .offset(y: h + 4)
        }
        .allowsHitTesting(false)
    }
}

private struct Reticle: View {
    let pointMode: Bool

    var body: some View {
        ZStack {
            Circle()
                .stroke(Color.white.opacity(0.55), lineWidth: 1)
                .frame(width: 34, height: 34)
            Rectangle().fill(Color.white.opacity(0.55)).frame(width: 1, height: 12).offset(y: -23)
            Rectangle().fill(Color.white.opacity(0.55)).frame(width: 1, height: 12).offset(y: 23)
            Rectangle().fill(Color.white.opacity(0.55)).frame(width: 12, height: 1).offset(x: -23)
            Rectangle().fill(Color.white.opacity(0.55)).frame(width: 12, height: 1).offset(x: 23)
            if pointMode {
                Circle().fill(Theme.magenta).frame(width: 5, height: 5)
            }
        }
        .shadow(color: .black.opacity(0.6), radius: 1)
        .allowsHitTesting(false)
    }
}
