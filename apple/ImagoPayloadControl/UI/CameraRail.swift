import SwiftUI

/// Left rail: capture mode, shutter, view source and zoom. On a phone in landscape the zoom
/// goes in a second column, since one column is taller than the screen.
struct CameraRail: View {
    @Environment(AppModel.self) private var model
    let compact: Bool

    var body: some View {
        Group {
            if compact {
                HStack(alignment: .center, spacing: 6) {
                    captureColumn
                    ZoomControl(compact: true)
                }
            } else {
                VStack(spacing: 10) {
                    captureColumn
                    ZoomControl(compact: false)
                }
            }
        }
        .padding(compact ? 6 : 8)
        .panel()
    }

    private var captureColumn: some View {
        VStack(spacing: compact ? 6 : 10) {
            modeToggle
            shutter
            viewMenu
        }
    }

    private var modeToggle: some View {
        HStack(spacing: 2) {
            modeButton(.photo, systemImage: "camera.fill", label: "Photo mode")
            modeButton(.video, systemImage: "video.fill", label: "Video mode")
        }
        .padding(2)
        .background(Theme.surfaceRaised, in: RoundedRectangle(cornerRadius: 12, style: .continuous))
    }

    private func modeButton(_ mode: CameraMode, systemImage: String, label: String) -> some View {
        let selected = model.captureMode == mode
        return Button {
            model.setCaptureMode(mode)
        } label: {
            Image(systemName: systemImage)
                .font(.system(size: 13, weight: .semibold))
                .foregroundStyle(selected ? Theme.text : Theme.textSecondary)
                .frame(width: 26, height: compact ? 26 : 30)
                .background {
                    RoundedRectangle(cornerRadius: 10, style: .continuous)
                        .fill(selected ? AnyShapeStyle(Theme.accent) : AnyShapeStyle(Color.clear))
                }
        }
        .buttonStyle(.plain)
        .accessibilityLabel(label)
        .accessibilityAddTraits(selected ? .isSelected : [])
    }

    private var shutter: some View {
        let size: CGFloat = compact ? 58 : 68
        let recording = model.camera.recording
        let video = model.captureMode == .video
        return Button {
            model.shutter()
        } label: {
            ZStack {
                Circle()
                    .stroke(Theme.text, lineWidth: 3)
                    .frame(width: size, height: size)
                if video {
                    RoundedRectangle(cornerRadius: recording ? 6 : size, style: .continuous)
                        .fill(Theme.danger)
                        .frame(width: recording ? size * 0.38 : size * 0.72,
                               height: recording ? size * 0.38 : size * 0.72)
                } else {
                    Circle()
                        .fill(Theme.text)
                        .frame(width: size * 0.76, height: size * 0.76)
                }
            }
            .animation(.easeOut(duration: 0.15), value: recording)
        }
        .buttonStyle(PressScaleStyle())
        .accessibilityLabel(video ? (recording ? "Stop recording" : "Start recording") : "Take photo")
    }

    private var viewMenu: some View {
        Menu {
            ForEach(ViewSource.allCases) { source in
                Button {
                    model.setView(source)
                } label: {
                    if model.camera.view == source {
                        Label(source.title, systemImage: "checkmark")
                    } else {
                        Text(source.title)
                    }
                }
            }
        } label: {
            VStack(spacing: 3) {
                Image(systemName: "square.on.square")
                    .font(.system(size: 17, weight: .semibold))
                Text(model.camera.view?.title ?? "View")
                    .font(AppFont.display(10, weight: 600))
                    .lineLimit(1)
                    .minimumScaleFactor(0.7)
            }
            .foregroundStyle(Theme.text)
            .frame(width: 56, height: compact ? 46 : 52)
            .background(Theme.surfaceRaised, in: RoundedRectangle(cornerRadius: 13, style: .continuous))
        }
        .accessibilityLabel("View source")
    }
}
