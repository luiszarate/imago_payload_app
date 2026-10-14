import SwiftUI

/// The single operating screen: full-screen video with control rails floating over it.
struct OperatorView: View {
    @Environment(AppModel.self) private var model
    @Environment(\.verticalSizeClass) private var verticalSizeClass

    /// The picture is flagged as stale from this lag on.
    static let lagWarningMs = 250

    private var compact: Bool { verticalSizeClass == .compact }

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
                    Spacer(minLength: 0)
                    GimbalRail(compact: compact)
                }
                .padding(.horizontal, compact ? 12 : 16)
                .padding(.top, compact ? 44 : 56)
            }

            if model.isConnected && model.showAttitude {
                VStack {
                    Spacer()
                    AttitudeIndicator(compact: compact)
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

            ToastView(raised: model.isConnected && model.showAttitude, compact: compact)
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
    /// Above the attitude indicator when it is shown.
    var raised = false
    var compact = false

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
        .padding(.bottom, raised ? (compact ? 178 : 238) : 18)
        .animation(.easeOut(duration: 0.2), value: model.toast)
        .allowsHitTesting(false)
    }
}
