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

            // Once the video has started, the last picture stays instead of the panel while the
            // stream restarts (to catch up, or after it dropped): a badge at the top says so.
            if !model.isConnected || (!model.videoActive && !model.videoResyncing && !model.videoHadPicture) {
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

            // Top centre, under the top bar: one warning about the picture at a time.
            if let badge = videoBadge {
                VStack {
                    VideoBadge(kind: badge)
                    Spacer(minLength: 0)
                }
                .padding(.top, compact ? 46 : 58)
                .allowsHitTesting(false)
            }

            // Top right of the picture, left of the gimbal rail: the Network HUD, then the latest
            // notice under it, so neither covers the other nor the picture's centre. On a phone the
            // notice goes a line lower, where it cannot meet the badge at the top centre.
            VStack(alignment: .trailing, spacing: 6) {
                if model.showHUD && model.isConnected {
                    HUDView()
                }
                ToastView()
                Spacer(minLength: 0)
            }
            .frame(maxWidth: .infinity, alignment: .trailing)
            .padding(.top, model.showHUD && model.isConnected ? (compact ? 50 : 66) : (compact ? 80 : 58))
            .padding(.trailing, compact ? 100 : 120)
        }
        .coordinateSpace(.named(Self.space))
        .statusBarHidden(true)
        .persistentSystemOverlays(.hidden)
        // The model is passed to each presentation explicitly: a nested presentation (Media's
        // preview) did not inherit it and crashed on "My Mac (Designed for iPad)" (2026-10-20).
        .sheet(isPresented: $model.showConnections) {
            ConnectionsView()
                .environment(model)
        }
        .sheet(isPresented: $model.showSettings) {
            SettingsView()
                .environment(model)
        }
        // Full screen: Media is a task of its own, after (or between) flights.
        .fullScreenCover(isPresented: $model.showMedia) {
            MediaView()
                .environment(model)
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

    /// One warning about the picture at a time, the most important first.
    private var videoBadge: VideoBadge.Kind? {
        guard model.isConnected else { return nil }
        if model.videoResyncing {
            return .catchingUp
        }
        if !model.videoActive {
            // Before the first picture, the connect panel says what the video is doing.
            return model.videoHadPicture ? .reconnecting : nil
        }
        if model.videoFrozenMs > 0 {
            return .frozen(ms: model.videoFrozenMs)
        }
        if model.videoLagMs >= Self.lagWarningMs {
            return .behind(ms: model.videoLagMs)
        }
        return nil
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

/// Says the picture is not live, so nobody steers by a stale image without knowing.
struct VideoBadge: View {
    enum Kind: Equatable {
        /// The core restarts the stream to catch up with live video.
        case catchingUp
        /// The video dropped after it had started; the last picture stays meanwhile.
        case reconnecting
        /// No new picture has reached the screen for this long.
        case frozen(ms: Int)
        /// The picture is this far behind live.
        case behind(ms: Int)
    }

    let kind: Kind

    var body: some View {
        HStack(spacing: 7) {
            Image(systemName: symbol)
                .font(.system(size: 13, weight: .bold))
            Text(text)
                .font(AppFont.figures(12, weight: 700))
                .tracking(0.6)
        }
        .foregroundStyle(Theme.background)
        .padding(.horizontal, 12)
        .padding(.vertical, 6)
        .background(Theme.warning, in: Capsule())
        .accessibilityElement(children: .combine)
    }

    private var symbol: String {
        switch kind {
        case .catchingUp: return "arrow.triangle.2.circlepath"
        case .reconnecting: return "arrow.clockwise"
        case .frozen: return "snowflake"
        case .behind: return "clock.badge.exclamationmark"
        }
    }

    private var text: String {
        switch kind {
        case .catchingUp: return "CATCHING UP WITH LIVE VIDEO"
        case .reconnecting: return "VIDEO RECONNECTING…"
        case let .frozen(ms): return String(format: "VIDEO FROZEN %.1f s", Double(ms) / 1000)
        case let .behind(ms): return String(format: "VIDEO %.1f s BEHIND", Double(ms) / 1000)
        }
    }
}

/// Temporary messages: command failures, link changes. Small, in the top right corner of the
/// picture: at the bottom centre they covered what the operator was looking at (field test
/// over the sea).
struct ToastView: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        VStack {
            if let toast = model.toast {
                HStack(spacing: 7) {
                    Image(systemName: toast.style == .error ? "exclamationmark.triangle.fill" : "info.circle.fill")
                        .font(.system(size: 13, weight: .semibold))
                        .foregroundStyle(toast.style == .error ? Theme.danger : Theme.magenta)
                    Text(toast.text)
                        .font(AppFont.body(13, weight: 600))
                        .foregroundStyle(Theme.text)
                        .multilineTextAlignment(.leading)
                        .lineLimit(2)
                        .fixedSize(horizontal: false, vertical: true)
                }
                .padding(.horizontal, 14)
                .padding(.vertical, 8)
                .panel(cornerRadius: 14)
                .frame(maxWidth: 380, alignment: .trailing)
                .transition(.asymmetric(insertion: .move(edge: .trailing).combined(with: .opacity),
                                        removal: .opacity))
                .id(toast.id)
            }
        }
        .animation(.easeOut(duration: 0.2), value: model.toast)
        .allowsHitTesting(false)
    }
}
