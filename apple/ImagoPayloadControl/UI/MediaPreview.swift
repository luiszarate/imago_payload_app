import SwiftUI
import UIKit

/// The two cameras of a shot (IMG_EO_<n> + IMG_IR_<n> on the Orus L).
enum PreviewCamera: String, CaseIterable, Identifiable {
    case eo, ir
    var id: String { rawValue }
    var title: String { self == .eo ? "EO" : "IR" }
}

/// One shot large, with previous / next (swipe, or the arrow keys with a keyboard) and an EO / IR
/// switch for a pair. The EO photo is fetched whole into a cache first (~10 MB); the IR one is
/// already here (the core fetched it whole for the thumbnail). Videos do not play here: download,
/// then share (or open, on a Mac). Download and Delete act on both files of the shot.
struct MediaPreview: View {
    @Environment(\.dismiss) private var dismiss
    let media: MediaStore
    @State private var names: [String]
    @State private var current: String
    @State private var camera = PreviewCamera.eo
    @State private var deleteRequest: MediaDeleteRequest?

    init(media: MediaStore, target: MediaPreviewTarget) {
        self.media = media
        _names = State(initialValue: target.names)
        _current = State(initialValue: target.start)
    }

    private var index: Int { names.firstIndex(of: current) ?? 0 }
    private var file: MediaFile? { media.file(named: current) }

    /// The other camera's photo, when the shot has one to switch to.
    private func photoPair(_ file: MediaFile) -> MediaFile? {
        guard file.previewable, let pair = media.pair(of: file), pair.previewable else { return nil }
        return pair
    }

    /// The file on screen: the shot's EO photo, or its IR twin when IR is chosen.
    private func shown(_ file: MediaFile) -> MediaFile {
        camera == .ir ? (photoPair(file) ?? file) : file
    }

    var body: some View {
        NavigationStack {
            TabView(selection: $current) {
                ForEach(names, id: \.self) { name in
                    PreviewPage(media: media, name: name, camera: camera)
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
                if media.isShotLocal(file) {
                    ShareLink(items: media.shotFiles(file).map { media.localURL(for: $0) }) {
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

    /// The EO / IR switch for a pair, then date, capture time, name and size of the file on screen.
    private var caption: some View {
        HStack(spacing: 10) {
            if let file {
                if photoPair(file) != nil {
                    Picker("Camera", selection: $camera) {
                        ForEach(PreviewCamera.allCases) { Text($0.title).tag($0) }
                    }
                    .pickerStyle(.segmented)
                    .frame(width: 120)
                }
                let onScreen = shown(file)
                Text(captionText(onScreen))
                    .font(AppFont.figures(13, weight: 600))
                    .foregroundStyle(Theme.textSecondary)
                    .lineLimit(1)
                    .truncationMode(.middle)
                if photoPair(file) == nil, media.pair(of: file) != nil {
                    MediaBadge(text: "EO+IR")
                } else if photoPair(file) == nil, file.thermal {
                    MediaBadge(text: "IR")
                }
                if onScreen.kind == .radiometric {
                    MediaBadge(text: "TIFF")
                }
                if media.isShotLocal(file) {
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

    /// After a delete: on to the next shot, or close when none is left.
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
    /// The shot's primary file.
    let name: String
    let camera: PreviewCamera
    @State private var image: UIImage?
    /// Which file `image` shows: switching cameras never shows the other camera's picture.
    @State private var imageName: String?
    @State private var undecodable: String?

    /// Large enough for a 12.9" iPad or a Mac window at full screen, still downscaled.
    private static let maxPixels = 3000

    private var shown: MediaFile? {
        guard let file = media.file(named: name) else { return nil }
        if camera == .ir, file.previewable, let pair = media.pair(of: file), pair.previewable {
            return pair
        }
        return file
    }

    private func readyURL(_ file: MediaFile?) -> URL? {
        guard let file else { return nil }
        if case let .ready(url) = media.previews[file.name] { return url }
        return nil
    }

    var body: some View {
        let shown = self.shown
        Group {
            if let file = media.file(named: name), let shown {
                if shown.previewable {
                    photo(shown)
                } else {
                    tile(file)
                }
            } else {
                MediaMessage(systemImage: "trash", title: "No longer on the payload")
            }
        }
        .task(id: shown?.name) {
            guard let shown, shown.previewable, imageName != shown.name else { return }
            // An IR photo is already here, whole: no download.
            if let whole = await media.thumbnails.whole(shown, maxPixels: Self.maxPixels) {
                image = whole
                imageName = shown.name
                return
            }
            media.preparePreview(shown)
        }
        .task(id: readyURL(shown)) {
            guard let shown, let url = readyURL(shown), imageName != shown.name else { return }
            let maxPixels = Self.maxPixels
            let decoded = await Task.detached(priority: .userInitiated) {
                MediaThumbnails.decode(url: url, maxPixels: maxPixels)
            }.value
            if let decoded {
                image = decoded
                imageName = shown.name
            } else {
                undecodable = shown.name
            }
        }
    }

    @ViewBuilder private func photo(_ file: MediaFile) -> some View {
        ZStack {
            if let image, imageName == file.name {
                Image(uiImage: image)
                    .resizable()
                    .scaledToFit()
                    .accessibilityLabel("Photo \(MediaFormat.time(file))")
            } else {
                // The small picture while the whole file comes over the datalink, unless it is
                // the other camera's (an EO photo's thumbnail is its IR twin).
                if !file.thumbnailFromPair, let thumbnail = media.thumbnails.cached(file.name) {
                    Image(uiImage: thumbnail)
                        .resizable()
                        .scaledToFit()
                        .opacity(0.6)
                }
                status(file)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    @ViewBuilder private func status(_ file: MediaFile) -> some View {
        if undecodable == file.name {
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

    /// Videos and other files: no playback, a tile naming the shot's files and what can be done.
    private func tile(_ file: MediaFile) -> some View {
        let files = media.shotFiles(file)
        return VStack(spacing: 14) {
            Image(systemName: file.kind == .video ? "film" : file.kind.systemImage)
                .font(.system(size: 44, weight: .regular))
                .foregroundStyle(Theme.textTertiary)
            VStack(spacing: 4) {
                ForEach(files) { part in
                    Text(part.name)
                        .font(AppFont.body(15, weight: 600))
                        .foregroundStyle(Theme.text)
                }
            }
            if files.contains(where: { $0.size != nil }) {
                Text(MediaFormat.size(media.size(of: [file])))
                    .font(AppFont.figures(14))
                    .foregroundStyle(Theme.textSecondary)
            }
            if media.isShotLocal(file) {
                HStack(spacing: 12) {
                    ShareLink(items: files.map { media.localURL(for: $0) }) {
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
                let current = files.contains { $0.name == media.transfer.name }
                VStack(spacing: 8) {
                    ProgressView(value: current ? media.transfer.fraction ?? 0 : 0)
                        .progressViewStyle(.linear)
                        .frame(width: 220)
                    Text(current ? "Downloading \(media.transfer.name)…" : "Waiting for the download queue…")
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
