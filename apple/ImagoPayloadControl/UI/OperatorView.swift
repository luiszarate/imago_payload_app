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

            VideoView(pipeline: model.video)
                .ignoresSafeArea()

            if model.isConnected {
                VideoOverlay()
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
                    Spacer(minLength: 0)
                    GimbalRail(compact: compact)
                        .background(WidthReader(width: $gimbalRailWidth))
                }
                .padding(.horizontal, margin)
                .padding(.top, compact ? 44 : 56)
            }

            if model.showsThermalBar {
                VStack {
                    Spacer()
                    ThermalBar(compact: compact)
                }
                .padding(.leading, thermalInsets.leading)
                .padding(.trailing, thermalInsets.trailing)
                .padding(.bottom, bottomMargin)
            }

            if model.isConnected && model.showAttitude {
                // With the thermal bar it moves to the right, just above the bar.
                VStack {
                    Spacer()
                    HStack {
                        if model.showsThermalBar { Spacer() }
                        AttitudeIndicator(compact: compact)
                    }
                }
                .padding(.trailing, model.showsThermalBar ? thermalInsets.trailing : 0)
                .padding(.bottom, model.showsThermalBar
                         ? bottomMargin + ThermalBar.height(compact: compact) + 8
                         : (compact ? 6 : 12))
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
        .statusBarHidden(true)
        .persistentSystemOverlays(.hidden)
        .sheet(isPresented: $model.showConnections) {
            ConnectionsView()
        }
        .sheet(isPresented: $model.showSettings) {
            SettingsView()
        }
    }
}

extension OperatorView {
    /// Toasts sit above whatever occupies the bottom centre: the attitude indicator, the thermal
    /// bar, or both.
    private var toastInset: CGFloat {
        guard model.isConnected else { return 18 }
        var inset: CGFloat = 18
        if model.showsThermalBar {
            inset = bottomMargin + ThermalBar.height(compact: compact) + 10
        }
        if model.showAttitude && !model.showsThermalBar {
            inset = compact ? 178 : 238
        }
        return inset
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
