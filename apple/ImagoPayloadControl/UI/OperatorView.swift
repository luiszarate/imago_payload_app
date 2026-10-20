import SwiftUI

/// The single operating screen: full-screen video with control rails floating over it.
struct OperatorView: View {
    @Environment(AppModel.self) private var model
    @Environment(\.verticalSizeClass) private var verticalSizeClass

    /// The picture is flagged as stale from this lag on.
    static let lagWarningMs = 250

    private var compact: Bool { verticalSizeClass == .compact }
    /// Widths of the side rails, so the thermal bar fits between them.
    @State private var cameraRailWidth: CGFloat = 0
    @State private var gimbalRailWidth: CGFloat = 0
    /// Where the gimbal rail ends: on a phone it reaches down beside the thermal bar.
    @State private var gimbalRailBottom: CGFloat = 0
    /// The thermal bar starts right of the attitude indicator.
    @State private var attitudeWidth: CGFloat = 0
    /// Right edge of the camera rail on screen: the picture starts after it.
    @State private var cameraRailEdge: CGFloat = 0

    private var margin: CGFloat { compact ? 12 : 16 }
    private var bottomMargin: CGFloat { compact ? 6 : 10 }
    /// The thermal bar spans the space between the rails at the bottom.
    private var thermalInsets: (leading: CGFloat, trailing: CGFloat) {
        (margin + cameraRailWidth + 10, margin + gimbalRailWidth + 10)
    }

    var body: some View {
        @Bindable var model = model
        ZStack {
            Theme.background.ignoresSafeArea()

            // Padded before ignoring the safe area, so the inset counts from the screen's edge.
            VideoView(pipeline: model.video)
                .padding(.leading, videoInset)
                .ignoresSafeArea()

            if model.isConnected {
                VideoOverlay()
                    .padding(.leading, videoInset)
                    .ignoresSafeArea()
            }

            // While the stream restarts to catch up, the last picture stays instead of the panel.
            if !model.isConnected || (!model.videoActive && !model.videoResyncing) {
                ConnectPanel()
            }

            VStack(spacing: 0) {
                TopBar()
                Spacer(minLength: 0)
            }
            .padding(.horizontal, compact ? 12 : 16)
            .padding(.top, compact ? 6 : 10)

            if model.isConnected {
                HStack(alignment: .center) {
                    CameraRail(compact: compact)
                        .background(WidthReader(width: $cameraRailWidth))
                        .background(EdgeReader(edge: $cameraRailEdge, space: .global, \.maxX))
                    Spacer(minLength: 0)
                    GimbalRail(compact: compact)
                        .background(WidthReader(width: $gimbalRailWidth))
                        .background(EdgeReader(edge: $gimbalRailBottom, space: .named(Self.space), \.maxY))
                }
                .padding(.horizontal, margin)
                .padding(.top, compact ? 44 : 56)
            }

            if model.showsThermalBar {
                // Bottom right, so the attitude indicator keeps its place in the centre.
                GeometryReader { geometry in
                    let insets = thermalBarInsets(geometry.frame(in: .named(Self.space)))
                    VStack {
                        Spacer()
                        ThermalBar(compact: compact)
                    }
                    .padding(.leading, insets.leading)
                    .padding(.trailing, insets.trailing)
                    .padding(.bottom, bottomMargin)
                }
            }

            if model.isConnected && model.showAttitude {
                VStack {
                    Spacer()
                    AttitudeIndicator(compact: compact)
                        .background(WidthReader(width: $attitudeWidth))
                }
                .padding(.bottom, compact ? 6 : 12)
            }

            if model.isConnected && (model.videoLagMs >= Self.lagWarningMs || model.videoResyncing) {
                VStack {
                    VideoLagBadge(lagMs: model.videoLagMs, resyncing: model.videoResyncing)
                        .padding(.top, compact ? 46 : 58)
                    Spacer()
                }
                .allowsHitTesting(false)
            }

            if model.showHUD && model.isConnected {
                VStack {
                    HStack {
                        Spacer()
                        HUDView()
                    }
                    Spacer()
                }
                .padding(.top, compact ? 50 : 66)
                .padding(.trailing, compact ? 100 : 120)
            }

            ToastView(bottomInset: toastInset)
        }
        .coordinateSpace(.named(Self.space))
        .statusBarHidden(true)
        .persistentSystemOverlays(.hidden)
        .sheet(isPresented: $model.showConnections) {
            ConnectionsView()
        }
        .sheet(isPresented: $model.showSettings) {
            SettingsView()
        }
        // Full screen: Media is a task of its own, after (or between) flights.
        .fullScreenCover(isPresented: $model.showMedia) {
            MediaView()
        }
    }
}

extension OperatorView {
    fileprivate static let space = "operator"

    /// The camera draws its OSD down the left edge of the picture, under the camera rail
    /// (field report 2026-10-15). With the setting on, the picture fits right of the rail
    /// instead: a little smaller on a tablet, only shifted on a phone, whose picture is narrower
    /// than the screen anyway. The thumbs keep their place at the sides.
    private var videoInset: CGFloat {
        guard model.isConnected, model.videoClearOfCameraRail, cameraRailEdge > 0 else { return 0 }
        return cameraRailEdge + 8
    }

    /// The thermal bar runs from the attitude indicator (or the camera rail when the indicator
    /// is hidden) to the right edge, or to the gimbal rail when the rail reaches down beside it.
    private func thermalBarInsets(_ frame: CGRect) -> (leading: CGFloat, trailing: CGFloat) {
        let leading = model.showAttitude && attitudeWidth > 0
            ? frame.width / 2 + attitudeWidth / 2 + 10
            : thermalInsets.leading
        let barTop = frame.maxY - bottomMargin - ThermalBar.height(compact: compact)
        let trailing = gimbalRailBottom > barTop - 8 ? thermalInsets.trailing : margin
        return (leading, trailing)
    }

    /// Toasts sit above whatever occupies the bottom centre: the attitude indicator, or the
    /// thermal bar when the indicator is hidden.
    private var toastInset: CGFloat {
        guard model.isConnected else { return 18 }
        if model.showAttitude {
            return compact ? 178 : 238
        }
        if model.showsThermalBar {
            return bottomMargin + ThermalBar.height(compact: compact) + 10
        }
        return 18
    }
}

/// Reports one edge of the view it is the background of, in the given coordinate space.
private struct EdgeReader: View {
    @Binding var edge: CGFloat
    let space: CoordinateSpace
    let keyPath: KeyPath<CGRect, CGFloat>

    init(edge: Binding<CGFloat>, space: CoordinateSpace, _ keyPath: KeyPath<CGRect, CGFloat>) {
        _edge = edge
        self.space = space
        self.keyPath = keyPath
    }

    var body: some View {
        GeometryReader { geometry in
            let value = geometry.frame(in: space)[keyPath: keyPath]
            Color.clear
                .onAppear { edge = value }
                .onChange(of: value) { _, newValue in edge = newValue }
        }
    }
}

/// Reports the width of the view it is the background of.
private struct WidthReader: View {
    @Binding var width: CGFloat

    var body: some View {
        GeometryReader { geometry in
            Color.clear
                .onAppear { width = geometry.size.width }
                .onChange(of: geometry.size.width) { _, newWidth in width = newWidth }
        }
    }
}

/// Says the picture is behind live, so nobody steers by a stale image without knowing.
struct VideoLagBadge: View {
    let lagMs: Int
    let resyncing: Bool

    var body: some View {
        HStack(spacing: 7) {
            Image(systemName: resyncing ? "arrow.triangle.2.circlepath" : "clock.badge.exclamationmark")
                .font(.system(size: 13, weight: .bold))
            Text(resyncing ? "CATCHING UP WITH LIVE VIDEO"
                           : String(format: "VIDEO %.1f s BEHIND", Double(lagMs) / 1000))
                .font(AppFont.figures(12, weight: 700))
                .tracking(0.6)
        }
        .foregroundStyle(Theme.background)
        .padding(.horizontal, 12)
        .padding(.vertical, 6)
        .background(Theme.warning, in: Capsule())
        .accessibilityElement(children: .combine)
    }
}

/// Temporary messages: command failures, link changes.
struct ToastView: View {
    @Environment(AppModel.self) private var model
    /// Distance from the bottom: above the attitude indicator or the thermal bar.
    var bottomInset: CGFloat = 18

    var body: some View {
        VStack {
            Spacer()
            if let toast = model.toast {
                HStack(spacing: 8) {
                    Image(systemName: toast.style == .error ? "exclamationmark.triangle.fill" : "info.circle.fill")
                        .foregroundStyle(toast.style == .error ? Theme.danger : Theme.magenta)
                    Text(toast.text)
                        .font(AppFont.body(14, weight: 600))
                        .foregroundStyle(Theme.text)
                }
                .padding(.horizontal, 16)
                .padding(.vertical, 10)
                .panel(cornerRadius: 14)
                .transition(.move(edge: .bottom).combined(with: .opacity))
                .id(toast.id)
            }
        }
        .padding(.bottom, bottomInset)
        .animation(.easeOut(duration: 0.2), value: model.toast)
        .allowsHitTesting(false)
    }
}
