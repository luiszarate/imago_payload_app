import SwiftUI

/// Shown while there is no session or no picture yet.
struct ConnectPanel: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        VStack(spacing: 16) {
            Wordmark(size: 26)
            Text("Payload Control")
                .font(AppFont.display(15, weight: 500))
                .foregroundStyle(Theme.textSecondary)
                .padding(.top, -10)

            if model.isConnected {
                ProgressView()
                    .tint(Theme.magenta)
                Text(model.videoMessage ?? "Connecting…")
                    .font(AppFont.body(14, weight: 500))
                    .foregroundStyle(Theme.textSecondary)
                    .multilineTextAlignment(.center)
                    .frame(maxWidth: 380)
                Text(model.link.up ? "Payload link up" : "Waiting for the payload…")
                    .font(AppFont.body(13, weight: 600))
                    .foregroundStyle(model.link.up ? Theme.ok : Theme.warning)
            } else {
                profilePicker
                if let error = model.connectError {
                    Text(error)
                        .font(AppFont.body(13, weight: 600))
                        .foregroundStyle(Theme.danger)
                        .multilineTextAlignment(.center)
                        .frame(maxWidth: 360)
                }
                Button {
                    model.connect()
                } label: {
                    Text("Connect")
                        .font(AppFont.display(16, weight: 700))
                        .foregroundStyle(Theme.text)
                        .frame(width: 220, height: 50)
                        .background(Theme.accent, in: RoundedRectangle(cornerRadius: 14, style: .continuous))
                }
                .buttonStyle(PressScaleStyle())
                .disabled(model.selectedProfile == nil)
            }
        }
        .padding(28)
        .panel(cornerRadius: 22)
    }

    private var profilePicker: some View {
        Menu {
            ForEach(model.profiles) { profile in
                Button {
                    model.selectProfile(profile.id)
                } label: {
                    if profile.id == model.selectedProfileID {
                        Label(profile.name, systemImage: "checkmark")
                    } else {
                        Text(profile.name)
                    }
                }
            }
            Divider()
            Button("Edit connections…") {
                model.showConnections = true
            }
        } label: {
            HStack(spacing: 10) {
                VStack(alignment: .leading, spacing: 2) {
                    Text(model.selectedProfile?.name ?? "Choose a connection")
                        .font(AppFont.display(15, weight: 600))
                        .foregroundStyle(Theme.text)
                    if let profile = model.selectedProfile {
                        Text(profile.mavlinkMode == .direct ? profile.payloadIP : "Listening for MAVLink")
                            .font(AppFont.figures(12))
                            .foregroundStyle(Theme.textSecondary)
                    }
                }
                Spacer()
                Image(systemName: "chevron.up.chevron.down")
                    .foregroundStyle(Theme.textSecondary)
            }
            .padding(.horizontal, 14)
            .frame(width: 280, height: 54)
            .background(Theme.surfaceRaised, in: RoundedRectangle(cornerRadius: 12, style: .continuous))
        }
    }
}
