import SwiftUI

struct TopBar: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        HStack(spacing: 10) {
            Wordmark(size: 16)
                .padding(.trailing, 4)
            if model.isConnected {
                linkPill
                if model.camera.recording {
                    recordingBadge
                }
            }
            Spacer(minLength: 8)
            if model.isConnected {
                if let target = model.target {
                    TargetChip(target: target)
                }
                if let range = model.camera.laserRangeMeters {
                    StatusChip(text: String(format: "%.0f m", range), systemImage: "scope")
                }
                if let gb = model.camera.storageAvailableGB {
                    StatusChip(text: String(format: "%.0f GB", gb), systemImage: "sdcard")
                }
            }
            if let gamepad = model.gamepadName {
                StatusChip(text: gamepad, systemImage: "gamecontroller.fill", color: Theme.text)
            }
            if model.isConnected {
                iconButton(model.showAttitude ? "location.north.circle.fill" : "location.north.circle",
                           active: model.showAttitude, label: "Gimbal attitude") {
                    model.showAttitude.toggle()
                }
                iconButton(model.showHUD ? "gauge.with.dots.needle.67percent" : "gauge.with.dots.needle.33percent",
                           active: model.showHUD, label: "Network HUD") {
                    model.showHUD.toggle()
                }
            }
            iconButton("gearshape", active: false, label: "Settings") {
                model.showSettings = true
            }
            iconButton("slider.horizontal.3", active: false, label: "Connections") {
                model.showConnections = true
            }
            if model.isConnected {
                iconButton("xmark", active: false, label: "Disconnect") {
                    model.disconnect()
                }
            }
        }
    }

    private var linkPill: some View {
        let up = model.link.up
        return HStack(spacing: 7) {
            Circle()
                .fill(up ? Theme.ok : Theme.warning)
                .frame(width: 8, height: 8)
            Text(model.connectedProfile?.name ?? "Payload")
                .font(AppFont.display(13, weight: 600))
                .foregroundStyle(Theme.text)
                .lineLimit(1)
            if up && model.link.roundTripMs > 0 {
                Text("\(model.link.roundTripMs) ms")
                    .font(AppFont.figures(12))
                    .foregroundStyle(Theme.textSecondary)
            } else if !up {
                Text("waiting for payload")
                    .font(AppFont.body(12))
                    .foregroundStyle(Theme.textSecondary)
            }
        }
        .padding(.horizontal, 11)
        .padding(.vertical, 6)
        .background(Theme.surface.opacity(0.8), in: Capsule())
        .overlay(Capsule().stroke(Theme.stroke))
    }

    private var recordingBadge: some View {
        let seconds = model.camera.recordingSeconds
        return HStack(spacing: 6) {
            Circle().fill(Theme.danger).frame(width: 8, height: 8)
            Text(String(format: "REC %02d:%02d", seconds / 60, seconds % 60))
                .font(AppFont.figures(12, weight: 700))
                .foregroundStyle(Theme.text)
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 6)
        .background(Theme.danger.opacity(0.25), in: Capsule())
    }

    private func iconButton(_ systemImage: String, active: Bool, label: String,
                            action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Image(systemName: systemImage)
                .font(.system(size: 15, weight: .semibold))
                .foregroundStyle(Theme.text)
                .frame(width: 38, height: 34)
                .background {
                    Capsule().fill(active ? AnyShapeStyle(Theme.accent) : AnyShapeStyle(Theme.surface.opacity(0.8)))
                }
                .overlay(Capsule().stroke(Theme.stroke))
        }
        .buttonStyle(PressScaleStyle())
        .accessibilityLabel(label)
    }
}

/// Where the camera points, as the payload works it out. Tap for copy, share, open in Maps and
/// mark; the coordinate gives way to its icon when the bar runs out of room (a phone).
private struct TargetChip: View {
    @Environment(AppModel.self) private var model
    let target: TargetPosition

    var body: some View {
        Menu {
            Section(String(format: "Target · alt %.0f m", target.altitude)) {
                Button {
                    model.copyTarget()
                } label: {
                    Label("Copy coordinates", systemImage: "doc.on.doc")
                }
                ShareLink(item: target.mapsURL, subject: Text("Target"),
                          message: Text(target.coordinateText)) {
                    Label("Share", systemImage: "square.and.arrow.up")
                }
                Link(destination: target.mapsURL) {
                    Label("Open in Maps", systemImage: "map")
                }
            }
            Button {
                model.markTarget()
            } label: {
                Label("Mark point", systemImage: "mappin.and.ellipse")
            }
        } label: {
            ViewThatFits(in: .horizontal) {
                StatusChip(text: target.coordinateText, systemImage: "mappin.and.ellipse",
                           color: Theme.text)
                StatusChip(text: "TGT", systemImage: "mappin.and.ellipse", color: Theme.text)
            }
        }
        .accessibilityLabel("Target position")
        .accessibilityValue(target.coordinateText)
    }
}
