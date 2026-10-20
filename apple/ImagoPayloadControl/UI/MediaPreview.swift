import SwiftUI
import UIKit

/// One file large, with previous / next (swipe, or the arrow keys with a keyboard). Photos and
/// TIFFs are fetched whole into a cache first; videos do not play here: download, then share
/// (or open, on a Mac).
struct MediaPreview: View {
    @Environment(\.dismiss) private var dismiss
    let media: MediaStore
    @State private var names: [String]
    @State private var current: String
    @State private var deleteRequest: MediaDeleteRequest?

    init(media: MediaStore, target: MediaPreviewTarget) {
        self.media = media
        _names = State(initialValue: target.names)
        _current = State(initialValue: target.start)
    }

    private var index: Int { names.firstIndex(of: current) ?? 0 }
    private var file: MediaFile? { media.file(named: current) }

    var body: some View {
        NavigationStack {
            TabView(selection: $current) {
                ForEach(names, id: \.self) { name in
                    PreviewPage(media: media, name: name)
                        .tag(name)
                }
            }
            .tabViewStyle(.page(indexDisplayMode: .never))
            .background(Theme.background.ignoresSafeArea())
            .safeAreaInset(edge: .bottom) { caption }
            .navigationTitle("\(index + 1) of \(names.count)")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar { toolbar }
        }
        .tint(Theme.magenta)
        .mediaLiveVideoQuestion(media, origin: .preview)
        .mediaDeleteConfirmation($deleteRequest) { files in
            media.delete(files)
            removeFromSequence(files.map(\.name))
        }
    }

    @ToolbarContentBuilder private var toolbar: some ToolbarContent {
        ToolbarItem(placement: .cancellationAction) {
            Button("Close") { dismiss() }
                .keyboardShortcut(.cancelAction)
        }
        ToolbarItemGroup(placement: .topBarTrailing) {
            Button {
                step(-1)
            } label: {
                Label("Previous", systemImage: "chevron.left")
            }
            .keyboardShortcut(.leftArrow, modifiers: [])
            .disabled(index == 0)
            Button {
                step(1)
            } label: {
                Label("Next", systemImage: "chevron.right")
            }
            .keyboardShortcut(.rightArrow, modifiers: [])
            .disabled(index >= names.count - 1)

            if let file {
                if media.isLocal(file) {
                    ShareLink(item: media.localURL(for: file)) {
                        Label("Share", systemImage: "square.and.arrow.up")
                    }
                } else if media.isDownloading(file) {
                    ProgressView()
                } else {
                    Button {
                        media.download([file], from: .preview)
                    } label: {
                        Label("Download", systemImage: "arrow.down.circle")
                    }
                }
                Button(role: .destructive) {
                    deleteRequest = MediaDeleteRequest(files: [file])
                } label: {
                    Label("Delete", systemImage: "trash")
                }
            }
        }
    }

    /// Date, capture time, size and kind of the file on screen.
    private var caption: some View {
        HStack(spacing: 8) {
            if let file {
                Text(captionText(file))
                    .font(AppFont.figures(13, weight: 600))
                    .foregroundStyle(Theme.textSecondary)
                    .lineLimit(1)
                    .truncationMode(.middle)
                if file.thermal {
                    MediaBadge(text: "IR")
                }
                if file.kind == .radiometric {
                    MediaBadge(text: "TIFF")
                }
                if media.isLocal(file) {
                    Label("On this device", systemImage: "checkmark.circle.fill")
                        .font(AppFont.body(12, weight: 600))
                        .foregroundStyle(Theme.ok)
                }
            }
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 8)
        .background(Theme.surface.opacity(0.85), in: Capsule())
        .padding(.bottom, 10)
    }

    private func captionText(_ file: MediaFile) -> String {
        var parts: [String] = []
        if let time = file.captureTime {
            parts.append(MediaFormat.day.string(from: time))
            parts.append(MediaFormat.seconds.string(from: time))
        }
        parts.append(file.name)
        if let size = file.size {
            parts.append(MediaFormat.size(size))
        }
        return parts.joined(separator: " · ")
    }

    private func step(_ by: Int) {
        let next = index + by
        guard names.indices.contains(next) else { return }
        withAnimation(.easeOut(duration: 0.2)) {
            current = names[next]
        }
    }

    /// After a delete: on to the next file, or close when none is left.
    private func removeFromSequence(_ deleted: [String]) {
        let at = index
        names.removeAll { deleted.contains($0) }
        guard !names.isEmpty else {
            dismiss()
            return
        }
        current = names[min(at, names.count - 1)]
    }
}

private struct PreviewPage: View {
    let media: MediaStore
    let name: String
    @State private var image: UIImage?
    @State private var undecodable = false

    /// Large enough for a 12.9" iPad or a Mac window at full screen, still downscaled.
    private static let maxPixels = 3000

    private var readyURL: URL? {
        if case let .ready(url) = media.previews[name] { return url }
        return nil
    }

    var body: some View {
        Group {
            if let file = media.file(named: name) {
                if file.previewable {
                    photo(file)
                } else {
                    tile(file)
                }
            } else {
                MediaMessage(systemImage: "trash", title: "No longer on the payload")
            }
        }
        .task(id: name) {
            if let file = media.file(named: name) {
                media.preparePreview(file)
            }
        }
        .task(id: readyURL) {
            guard let url = readyURL else { return }
            let maxPixels = Self.maxPixels
            let decoded = await Task.detached(priority: .userInitiated) {
                MediaThumbnails.decode(url: url, maxPixels: maxPixels)
            }.value
            image = decoded
            undecodable = decoded == nil
        }
    }

    @ViewBuilder private func photo(_ file: MediaFile) -> some View {
        ZStack {
            if let image {
                Image(uiImage: image)
                    .resizable()
                    .scaledToFit()
                    .accessibilityLabel("Photo \(MediaFormat.time(file))")
            } else if let thumbnail = media.thumbnails.cached(file.name) {
                // The small picture while the whole file comes over the datalink.
                Image(uiImage: thumbnail)
                    .resizable()
                    .scaledToFit()
                    .opacity(0.6)
            }
            if image == nil {
                status(file)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    @ViewBuilder private func status(_ file: MediaFile) -> some View {
        if undecodable {
            MediaMessage(systemImage: file.kind.systemImage, title: "No preview for this file",
                         detail: "Download it to open it in another app.")
        } else {
            switch media.previews[file.name] {
            case let .failed(reason):
                MediaMessage(systemImage: "exclamationmark.triangle", title: "Could not fetch the photo",
                             detail: reason, action: ("Try again", { media.retryPreview(file) }))
            default:
                VStack(spacing: 10) {
                    if media.transfer.name == file.name, let fraction = media.transfer.fraction {
                        ProgressView(value: fraction)
                            .progressViewStyle(.linear)
                            .frame(width: 200)
                        Text("\(Int(fraction * 100)) %")
                            .font(AppFont.figures(13, weight: 600))
                            .foregroundStyle(Theme.text)
                    } else {
                        ProgressView()
                        Text("Waiting for the download queue…")
                            .font(AppFont.body(13))
                            .foregroundStyle(Theme.textSecondary)
                    }
                }
                .padding(16)
                .background(Theme.surface.opacity(0.85), in: RoundedRectangle(cornerRadius: 14, style: .continuous))
            }
        }
    }

    /// Videos and other files: no playback, a tile with what can be done with it.
    private func tile(_ file: MediaFile) -> some View {
        VStack(spacing: 14) {
            Image(systemName: file.kind == .video ? "film" : file.kind.systemImage)
                .font(.system(size: 44, weight: .regular))
                .foregroundStyle(Theme.textTertiary)
            Text(file.name)
                .font(AppFont.body(15, weight: 600))
                .foregroundStyle(Theme.text)
            if let size = file.size {
                Text(MediaFormat.size(size))
                    .font(AppFont.figures(14))
                    .foregroundStyle(Theme.textSecondary)
            }
            if media.isLocal(file) {
                HStack(spacing: 12) {
                    ShareLink(item: media.localURL(for: file)) {
                        Label("Share", systemImage: "square.and.arrow.up")
                    }
                    .buttonStyle(.borderedProminent)
                    #if targetEnvironment(macCatalyst)
                    Button {
                        let url = media.localURL(for: file)
                        Task { @MainActor in
                            if !(await UIApplication.shared.open(url)) {
                                media.notice = "Cannot open \(file.name)."
                            }
                        }
                    } label: {
                        Label("Open", systemImage: "play.rectangle")
                    }
                    .buttonStyle(.bordered)
                    #endif
                }
            } else if media.isDownloading(file) {
                VStack(spacing: 8) {
                    ProgressView(value: media.transfer.name == file.name ? media.transfer.fraction ?? 0 : 0)
                        .progressViewStyle(.linear)
                        .frame(width: 220)
                    Text(media.transfer.name == file.name ? "Downloading…" : "Waiting for the download queue…")
                        .font(AppFont.body(13))
                        .foregroundStyle(Theme.textSecondary)
                }
            } else {
                Button {
                    media.download([file], from: .preview)
                } label: {
                    Label("Download", systemImage: "arrow.down.circle")
                }
                .buttonStyle(.borderedProminent)
                Text(file.kind == .video ? "Videos do not play here. Download, then share it to a player."
                                         : "No preview for this file.")
                    .font(AppFont.body(13))
                    .foregroundStyle(Theme.textSecondary)
            }
        }
        .padding(24)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}
