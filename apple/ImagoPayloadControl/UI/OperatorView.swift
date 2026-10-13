import SwiftUI

/// The single operating screen: full-screen video with control rails floating over it.
struct OperatorView: View {
    @Environment(AppModel.self) private var model
    @Environment(\.verticalSizeClass) private var verticalSizeClass

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

            if !model.isConnected || !model.videoActive {
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

            ToastView()
        }
        .statusBarHidden(true)
        .persistentSystemOverlays(.hidden)
        .sheet(isPresented: $model.showConnections) {
            ConnectionsView()
        }
    }
}

/// Temporary messages: command failures, link changes.
struct ToastView: View {
    @Environment(AppModel.self) private var model

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
        .padding(.bottom, 18)
        .animation(.easeOut(duration: 0.2), value: model.toast)
        .allowsHitTesting(false)
    }
}
