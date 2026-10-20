import GameController
import SwiftUI
import UIKit

/// The payload's photos and videos read as flight rolls: captures fall into flights by their time
/// gaps, and a flight is what the operator opens, judges and pulls. Full screen over the video.
/// iPad and Mac: the flights as a sidebar and the selected flight's sheet beside it. iPhone: the
/// flights list pushes the sheet.
struct MediaView: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        Group {
            if let media = model.media, model.isConnected {
                MediaBrowser(media: media)
            } else {
                NavigationStack {
                    MediaMessage(systemImage: "wifi.slash", title: "Not connected",
                                 detail: "Connect to the payload to see its media.")
                        .navigationTitle("Media")
                        .navigationBarTitleDisplayMode(.inline)
                        .toolbar {
                            ToolbarItem(placement: .confirmationAction) {
                                Button("Done") { model.closeMediaScreen() }
                            }
                        }
                }
            }
        }
        .tint(Theme.magenta)
        // No onDisappear: the preview's full-screen cover makes this view disappear too.
        // Media closes through Done only.
        .onAppear { model.mediaAppeared() }
        .onChange(of: model.isConnected) { _, connected in
            if connected {
                model.mediaAppeared()
            }
        }
    }
}

#if targetEnvironment(macCatalyst)
/// Mac: a click selects, ⌘-click toggles, ⇧-click extends, a double-click opens.
private let clickSelects = true
#else
/// Touch: a tap opens the preview, or toggles in selection mode.
private let clickSelects = false
#endif

struct MediaPreviewTarget: Identifiable {
    let id = UUID()
    /// The sheet's files in order, for previous / next.
    let names: [String]
    let start: String
}

struct MediaDeleteRequest: Identifiable {
    let id = UUID()
    let files: [MediaFile]
}

private struct MediaBrowser: View {
    @Environment(AppModel.self) private var model
    @Environment(\.horizontalSizeClass) private var sizeClass
    @Bindable var media: MediaStore
    @State private var preview: MediaPreviewTarget?
    @State private var deleteRequest: MediaDeleteRequest?
    @State private var columns = NavigationSplitViewVisibility.all

    var body: some View {
        content
            .mediaLiveVideoQuestion(media, origin: .browser)
            .mediaDeleteConfirmation($deleteRequest) { media.delete($0) }
            .fullScreenCover(item: $preview) { target in
                MediaPreview(media: media, target: target)
                    .environment(model)
            }
    }

    @ViewBuilder private var content: some View {
        if media.files.isEmpty {
            NavigationStack {
                MediaStateView(media: media)
                    .navigationTitle("Media")
                    .navigationBarTitleDisplayMode(.inline)
                    .toolbar {
                        ToolbarItem(placement: .confirmationAction) { doneButton }
                        ToolbarItem(placement: .topBarTrailing) { RefreshButton(media: media) }
                    }
            }
        } else if sizeClass == .compact {
            NavigationStack {
                FlightRail(media: media, pushes: true)
                    .navigationTitle("Media")
                    .navigationBarTitleDisplayMode(.inline)
                    .toolbar {
                        ToolbarItem(placement: .cancellationAction) { doneButton }
                        ToolbarItem(placement: .topBarTrailing) { RefreshButton(media: media) }
                    }
                    .navigationDestination(for: MediaFlight.ID.self) { id in
                        RollSheet(media: media, flightID: id, compact: true, showsDone: false,
                                  open: open, askDelete: askDelete)
                    }
            }
        } else {
            NavigationSplitView(columnVisibility: $columns) {
                FlightRail(media: media, pushes: false)
                    .navigationTitle("Media")
                    .navigationBarTitleDisplayMode(.inline)
                    .toolbar {
                        ToolbarItem(placement: .topBarTrailing) { RefreshButton(media: media) }
                    }
                    .navigationSplitViewColumnWidth(min: 280, ideal: 330, max: 420)
            } detail: {
                RollSheet(media: media, flightID: media.selectedFlightID, compact: false, showsDone: true,
                          open: open, askDelete: askDelete)
            }
            .navigationSplitViewStyle(.balanced)
        }
    }

    private var doneButton: some View {
        Button("Done") { model.closeMediaScreen() }
            .fontWeight(.semibold)
    }

    private func open(_ file: MediaFile, in visible: [MediaFile]) {
        preview = MediaPreviewTarget(names: visible.map(\.name), start: file.name)
    }

    private func askDelete(_ files: [MediaFile]) {
        guard !files.isEmpty else { return }
        deleteRequest = MediaDeleteRequest(files: files)
    }
}

// MARK: - Flight rail

/// Flights newest first. Each row: date, local time range, counts, size, "Download flight" and
/// the capture timeline strip.
private struct FlightRail: View {
    @Bindable var media: MediaStore
    /// iPhone: a row pushes the flight's sheet.
    let pushes: Bool

    var body: some View {
        List {
            Section {
                ForEach(media.flights) { flight in
                    row(flight)
                }
            } header: {
                RailHeader(media: media)
            }
        }
        .listStyle(.plain)
        .scrollContentBackground(.hidden)
        .background(Theme.surface)
    }

    @ViewBuilder private func row(_ flight: MediaFlight) -> some View {
        let selected = !pushes && flight.id == media.selectedFlightID
        let files = media.rollFiles(flight)
        let scrub: ((Double) -> Void)? = selected ? { media.scrub(flight, to: $0) } : nil
        let content = FlightRow(flight: flight, files: files, selected: selected, scrub: scrub,
                                download: { media.downloadFlight(flight) })
        Group {
            if pushes {
                NavigationLink(value: flight.id) { content }
            } else {
                content
                    .contentShape(Rectangle())
                    .onTapGesture { media.selectFlight(flight.id) }
                    .accessibilityAddTraits(selected ? [.isButton, .isSelected] : .isButton)
            }
        }
        .listRowBackground(selected ? Theme.surfaceRaised : Color.clear)
        .listRowSeparatorTint(Theme.stroke)
        .swipeActions(edge: .trailing) {
            Button {
                media.downloadFlight(flight)
            } label: {
                Label("Download flight", systemImage: "arrow.down.circle")
            }
            .tint(Theme.purple)
        }
        .contextMenu {
            Button {
                media.downloadFlight(flight)
            } label: {
                Label("Download flight", systemImage: "arrow.down.circle")
            }
        }
    }
}

/// Storage gauge, the storage picker when the payload has more than one, and a failed refresh.
private struct RailHeader: View {
    @Bindable var media: MediaStore

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            if let use = media.storageUse {
                HStack(spacing: 10) {
                    Image(systemName: "sdcard")
                        .font(.system(size: 12, weight: .semibold))
                        .foregroundStyle(Theme.textSecondary)
                    Text(use.text)
                        .font(AppFont.figures(12, weight: 600))
                        .foregroundStyle(Theme.textSecondary)
                    GeometryReader { geometry in
                        ZStack(alignment: .leading) {
                            Capsule().fill(Theme.stroke)
                            Capsule().fill(Theme.textSecondary)
                                .frame(width: geometry.size.width * use.fraction)
                        }
                    }
                    .frame(height: 4)
                }
                .accessibilityElement(children: .ignore)
                .accessibilityLabel("Payload storage")
                .accessibilityValue(use.text)
            }
            if media.storageLabels.count > 1 {
                Menu {
                    Picker("Storage", selection: Binding(get: { media.currentStorage ?? -1 },
                                                         set: { media.setStorage($0) })) {
                        ForEach(Array(media.storageLabels.enumerated()), id: \.offset) { index, label in
                            Text(label).tag(index)
                        }
                    }
                } label: {
                    Label(media.currentStorage.flatMap { media.storageLabels.indices.contains($0) ? media.storageLabels[$0] : nil }
                          ?? "Storage", systemImage: "externaldrive")
                        .font(AppFont.body(13, weight: 600))
                }
            }
            if media.status == .failed {
                HStack(alignment: .firstTextBaseline, spacing: 8) {
                    Text(media.error.isEmpty ? "Could not read the list again." : media.error)
                        .font(AppFont.body(12, weight: 600))
                        .foregroundStyle(Theme.danger)
                        .lineLimit(2)
                    Spacer(minLength: 4)
                    Button("Try again") { media.refresh() }
                        .font(AppFont.body(12, weight: 700))
                        .buttonStyle(.borderless)
                }
            }
        }
        .padding(.vertical, 6)
        .textCase(nil)
    }
}

private struct FlightRow: View {
    let flight: MediaFlight
    let files: [MediaFile]
    let selected: Bool
    let scrub: ((Double) -> Void)?
    let download: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(alignment: .firstTextBaseline, spacing: 8) {
                Text(flight.title)
                    .font(AppFont.display(17, weight: 600))
                    .foregroundStyle(Theme.text)
                if let range = flight.timeRange {
                    Text(range)
                        .font(AppFont.figures(13))
                        .foregroundStyle(Theme.textSecondary)
                }
                Spacer(minLength: 6)
                Text(MediaFormat.size(flight.size))
                    .font(AppFont.figures(13))
                    .foregroundStyle(Theme.textSecondary)
            }
            HStack(spacing: 8) {
                Text(flight.counts)
                    .font(AppFont.figures(12))
                    .foregroundStyle(Theme.textSecondary)
                Spacer(minLength: 6)
                Button(action: download) {
                    Image(systemName: "arrow.down.circle")
                        .font(.system(size: 19))
                        .foregroundStyle(Theme.text)
                        .frame(width: 44, height: 30)
                        .contentShape(Rectangle())
                }
                .buttonStyle(.borderless)
                .accessibilityLabel("Download flight")
            }
            if flight.start != nil {
                CaptureStrip(flight: flight, files: files, selected: selected, scrub: scrub)
            }
        }
        .padding(.vertical, 6)
    }
}

/// The flight's captures across its span: photos as ticks, videos as short bars, in magenta when
/// the flight is selected. Tapping or dragging on the selected flight's strip scrolls the sheet to
/// the file captured nearest that moment.
private struct CaptureStrip: View {
    let flight: MediaFlight
    let files: [MediaFile]
    let selected: Bool
    let scrub: ((Double) -> Void)?

    private static let inset: CGFloat = 4

    var body: some View {
        GeometryReader { geometry in
            let strip = Canvas { context, size in draw(&context, size) }
            if let scrub {
                strip
                    .contentShape(Rectangle())
                    .gesture(DragGesture(minimumDistance: 0).onChanged { value in
                        let width = max(geometry.size.width - 2 * Self.inset, 1)
                        scrub(Double((value.location.x - Self.inset) / width))
                    })
            } else {
                strip
            }
        }
        .frame(height: 20)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel("Capture timeline")
        .accessibilityValue(flight.counts)
    }

    private func draw(_ context: inout GraphicsContext, _ size: CGSize) {
        let mid = size.height / 2
        context.fill(Path(CGRect(x: 0, y: mid - 0.5, width: size.width, height: 1)), with: .color(Theme.stroke))
        guard let start = flight.start, let end = flight.end else { return }
        let span = end.timeIntervalSince(start)
        let width = size.width - 2 * Self.inset
        let color = selected ? Theme.magenta : Theme.textTertiary
        for file in files {
            guard let time = file.captureTime else { continue }
            let fraction = span > 0 ? time.timeIntervalSince(start) / span : 0.5
            let x = Self.inset + width * CGFloat(fraction)
            if file.kind == .video {
                let bar = CGRect(x: x - 4, y: mid - 3, width: 8, height: 6)
                context.fill(Path(roundedRect: bar, cornerRadius: 3), with: .color(color))
            } else {
                context.fill(Path(CGRect(x: x - 0.75, y: 2, width: 1.5, height: size.height - 4)),
                             with: .color(color))
            }
        }
    }
}

// MARK: - Sheet

/// The selected flight's thumbnails in capture order, with the selection / transfer bar.
private struct RollSheet: View {
    @Bindable var media: MediaStore
    let flightID: MediaFlight.ID?
    let compact: Bool
    let showsDone: Bool
    let open: (MediaFile, [MediaFile]) -> Void
    let askDelete: ([MediaFile]) -> Void
    @Environment(AppModel.self) private var model

    private var columns: [GridItem] {
        [GridItem(.adaptive(minimum: compact ? 150 : 190), spacing: 12)]
    }

    var body: some View {
        let flight = media.flight(flightID)
        let visible = flight.map { media.visibleFiles(in: $0) } ?? []
        let marking = clickSelects || media.selecting || !media.selection.isEmpty
        ScrollViewReader { proxy in
            ScrollView {
                LazyVGrid(columns: columns, spacing: 14) {
                    ForEach(visible) { file in
                        MediaCell(file: file, thumbnails: media.thumbnails,
                                  request: { media.requestThumbnail($0) },
                                  selected: media.selection.contains(file.name),
                                  showsCheck: marking && (!clickSelects || media.selection.contains(file.name)),
                                  onDevice: media.isLocal(file))
                            .id(file.name)
                            .gesture(tap(file, visible))
                            .contextMenu { menu(file) }
                    }
                }
                .padding(16)
            }
            .onChange(of: media.scrollRequest) { _, request in
                if let request {
                    proxy.scrollTo(request.name, anchor: .center)
                }
            }
        }
        .overlay {
            if flight == nil {
                MediaMessage(systemImage: "airplane", title: "Choose a flight")
            } else if visible.isEmpty {
                MediaMessage(systemImage: media.filter == .videos ? "video" : "photo",
                             title: media.filter == .videos ? "No videos in this flight." : "No photos in this flight.")
            }
        }
        .background(Theme.background)
        .safeAreaInset(edge: .bottom) {
            MediaBar(media: media, askDelete: askDelete)
        }
        .background { shortcuts(visible) }
        .navigationTitle(title(flight))
        .navigationBarTitleDisplayMode(.inline)
        .toolbar { toolbar(visible) }
        .onAppear {
            if let flightID, compact {
                media.selectFlight(flightID)
            }
        }
    }

    private func title(_ flight: MediaFlight?) -> String {
        guard let flight else { return "Media" }
        guard let range = flight.timeRange else { return flight.title }
        return "\(flight.title)  \(range)"
    }

    private func shortcuts(_ visible: [MediaFile]) -> SheetShortcuts {
        var delete: (() -> Void)?
        if !media.selection.isEmpty {
            delete = { askDelete(media.selectedFiles) }
        }
        var openOne: (() -> Void)?
        if media.selection.count == 1, let file = media.selectedFiles.first {
            openOne = { open(file, visible) }
        }
        return SheetShortcuts(
            selectAll: {
                media.selectAll(visible)
                if !clickSelects {
                    media.selecting = true
                }
            },
            delete: delete,
            clear: { media.clearSelection() },
            open: openOne)
    }

    @ToolbarContentBuilder
    private func toolbar(_ visible: [MediaFile]) -> some ToolbarContent {
        ToolbarItem(placement: .principal) {
            Picker("Show", selection: $media.filter) {
                ForEach(MediaStore.Filter.allCases) { Text($0.title).tag($0) }
            }
            .pickerStyle(.segmented)
            .frame(width: compact ? 210 : 260)
        }
        ToolbarItemGroup(placement: .topBarTrailing) {
            #if targetEnvironment(macCatalyst)
            Button {
                MacSupport.showInFinder(MediaFolders.downloads) {
                    media.notice = "Cannot open the downloads folder in Finder."
                }
            } label: {
                Label("Show downloads in Finder", systemImage: "folder")
            }
            #else
            if media.selecting {
                Button("Select All") { media.selectAll(visible) }
            }
            Button(media.selecting ? "Cancel" : "Select") {
                if media.selecting {
                    media.clearSelection()
                } else {
                    media.selecting = true
                }
            }
            #endif
            if showsDone {
                Button("Done") { model.closeMediaScreen() }
                    .fontWeight(.semibold)
            }
        }
    }

    /// ⌘ and ⇧ taps work wherever a keyboard is attached (iPad, Mac); the plain tap differs.
    /// SwiftUI's TapGesture.modifiers is macOS-only (not iOS or Catalyst; Xcode 2026-10-20), so
    /// the keys are read when the tap ends.
    private func tap(_ file: MediaFile, _ visible: [MediaFile]) -> AnyGesture<Void> {
        let single = {
            if KeyboardModifiers.command {
                if !clickSelects { media.selecting = true }
                media.toggle(file)
            } else if KeyboardModifiers.shift {
                if !clickSelects { media.selecting = true }
                media.extendSelection(to: file, in: visible)
            } else if clickSelects {
                media.selectOnly(file)
            } else if media.selecting {
                media.toggle(file)
            } else {
                open(file, visible)
            }
        }
        if clickSelects {
            return AnyGesture(
                TapGesture(count: 2).onEnded { open(file, visible) }
                    .exclusively(before: TapGesture().onEnded { single() })
                    .map { _ in () })
        }
        return AnyGesture(TapGesture().onEnded { single() })
    }

    /// On the selection when the file is part of it, else on the file alone.
    @ViewBuilder private func menu(_ file: MediaFile) -> some View {
        let targets = media.selection.contains(file.name) ? media.selectedFiles : [file]
        Button {
            media.download(targets)
        } label: {
            Label(targets.count > 1 ? "Download \(targets.count) files" : "Download", systemImage: "arrow.down.circle")
        }
        if targets.count == 1, media.isLocal(file) {
            ShareLink(item: media.localURL(for: file)) {
                Label("Share", systemImage: "square.and.arrow.up")
            }
        }
        Button(role: .destructive) {
            askDelete(targets)
        } label: {
            Label(targets.count > 1 ? "Delete \(targets.count) files…" : "Delete…", systemImage: "trash")
        }
    }
}

/// Hardware keyboard on iPad and Mac: ⌘A selects the flight, Delete asks to delete, Esc clears,
/// Return opens the one selected file. Invisible buttons, so their shortcuts are always there.
private struct SheetShortcuts: View {
    let selectAll: () -> Void
    let delete: (() -> Void)?
    let clear: () -> Void
    let open: (() -> Void)?

    var body: some View {
        ZStack {
            Button("Select All", action: selectAll)
                .keyboardShortcut("a", modifiers: .command)
            Button("Delete") { delete?() }
                .keyboardShortcut(.delete, modifiers: [])
                .disabled(delete == nil)
            Button("Delete") { delete?() }
                .keyboardShortcut(.deleteForward, modifiers: [])
                .disabled(delete == nil)
            Button("Clear selection", action: clear)
                .keyboardShortcut(.escape, modifiers: [])
            Button("Open") { open?() }
                .keyboardShortcut(.return, modifiers: [])
                .disabled(open == nil)
        }
        .opacity(0)
        .frame(width: 0, height: 0)
        .allowsHitTesting(false)
        .accessibilityHidden(true)
    }
}

/// One file: 16:9 thumbnail cropped to fill, capture time, IR / TIFF badges, the on-device check.
private struct MediaCell: View {
    let file: MediaFile
    let thumbnails: MediaThumbnails
    let request: (String) -> Void
    let selected: Bool
    let showsCheck: Bool
    let onDevice: Bool
    @State private var image: UIImage?

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Color.clear
                .aspectRatio(16 / 9, contentMode: .fit)
                .overlay { picture }
                .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
                .overlay {
                    if selected {
                        RoundedRectangle(cornerRadius: 8, style: .continuous)
                            .strokeBorder(Theme.magenta, lineWidth: 3)
                    }
                }
                .overlay(alignment: .topLeading) {
                    if showsCheck {
                        Image(systemName: selected ? "checkmark.circle.fill" : "circle")
                            .symbolRenderingMode(.palette)
                            .foregroundStyle(Theme.text, selected ? Theme.magenta : Theme.background.opacity(0.35))
                            .font(.system(size: 20, weight: .semibold))
                            .shadow(color: .black.opacity(0.4), radius: 2, y: 1)
                            .padding(6)
                    }
                }
                .overlay(alignment: .topTrailing) {
                    if onDevice {
                        Image(systemName: "checkmark.circle.fill")
                            .symbolRenderingMode(.palette)
                            .foregroundStyle(Theme.background, Theme.ok)
                            .font(.system(size: 16, weight: .semibold))
                            .padding(6)
                    }
                }
            HStack(spacing: 5) {
                Text(MediaFormat.time(file))
                    .font(AppFont.figures(12, weight: 600))
                    .foregroundStyle(selected ? Theme.text : Theme.textSecondary)
                    .lineLimit(1)
                    .truncationMode(.middle)
                Spacer(minLength: 4)
                if file.thermal {
                    MediaBadge(text: "IR")
                }
                if file.kind == .radiometric {
                    MediaBadge(text: "TIFF")
                }
            }
        }
        .contentShape(Rectangle())
        .task(id: file.hasThumbnail) { await loadThumbnail() }
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(accessibilityText)
        .accessibilityAddTraits(selected ? [.isButton, .isSelected] : .isButton)
    }

    @ViewBuilder private var picture: some View {
        if let image {
            Image(uiImage: image)
                .resizable()
                .scaledToFill()
        } else if file.kind == .video {
            ZStack {
                Theme.surfaceRaised
                VStack(spacing: 6) {
                    Image(systemName: "play.fill")
                        .font(.system(size: 22, weight: .semibold))
                    if let size = file.size {
                        Text(MediaFormat.size(size))
                            .font(AppFont.figures(12, weight: 600))
                    }
                }
                .foregroundStyle(Theme.textSecondary)
            }
        } else if file.thumbnailFailed || file.kind != .photo {
            // No picture to come: the icon alone.
            ZStack {
                Theme.surface
                RoundedRectangle(cornerRadius: 8, style: .continuous).strokeBorder(Theme.stroke)
                Image(systemName: file.kind.systemImage)
                    .font(.system(size: 20))
                    .foregroundStyle(Theme.textTertiary)
            }
        } else {
            ZStack {
                Theme.surfaceRaised
                Image(systemName: file.kind.systemImage)
                    .font(.system(size: 20))
                    .foregroundStyle(Theme.textTertiary)
            }
        }
    }

    private func loadThumbnail() async {
        if let cached = thumbnails.cached(file.name) {
            image = cached
            return
        }
        guard file.kind == .photo, !file.thumbnailFailed else { return }
        if file.hasThumbnail {
            image = await thumbnails.load(file.name)
        } else {
            // On screen: the core fetches this one next.
            request(file.name)
        }
    }

    private var accessibilityText: String {
        var parts = [file.kind == .video ? "Video" : "Photo", MediaFormat.time(file)]
        if file.thermal { parts.append("infrared") }
        if file.kind == .radiometric { parts.append("radiometric TIFF") }
        if let size = file.size { parts.append(MediaFormat.size(size)) }
        if onDevice { parts.append("on this device") }
        return parts.joined(separator: ", ")
    }
}

struct MediaBadge: View {
    let text: String

    var body: some View {
        Text(text)
            .font(AppFont.figures(10, weight: 700))
            .foregroundStyle(Theme.text)
            .padding(.horizontal, 5)
            .padding(.vertical, 2)
            .background(Theme.surfaceRaised, in: RoundedRectangle(cornerRadius: 4, style: .continuous))
    }
}

// MARK: - Bottom bar

/// The selection bar, which becomes the transfer row while downloads run and the summary after.
private struct MediaBar: View {
    @Bindable var media: MediaStore
    let askDelete: ([MediaFile]) -> Void

    var body: some View {
        Group {
            if let batch = media.batch, batch.running {
                transferRow(batch)
            } else if let batch = media.batch {
                summaryRow(batch)
            } else if !media.selection.isEmpty {
                selectionRow
            } else if media.transfer.deletesPending > 0 {
                deletingRow
            } else if let notice = media.notice {
                noticeRow(notice)
            }
        }
        .padding(.horizontal, 16)
        .padding(.bottom, 12)
    }

    private var selectionRow: some View {
        let count = media.selection.count
        return HStack(spacing: 10) {
            Text("\(count) selected · \(MediaFormat.size(media.selectedSize))")
                .font(AppFont.figures(14, weight: 600))
                .foregroundStyle(Theme.text)
                .lineLimit(1)
            Spacer(minLength: 8)
            Button {
                media.download(media.selectedFiles)
            } label: {
                Label("Download", systemImage: "arrow.down.circle")
            }
            .buttonStyle(.borderedProminent)
            Button(role: .destructive) {
                askDelete(media.selectedFiles)
            } label: {
                Label("Delete", systemImage: "trash")
            }
            .buttonStyle(.bordered)
            .tint(Theme.danger)
            Button("Clear") { media.clearSelection() }
                .buttonStyle(.borderless)
                .foregroundStyle(Theme.textSecondary)
        }
        .font(AppFont.body(14, weight: 600))
        .mediaPane()
    }

    private func transferRow(_ batch: MediaStore.Batch) -> some View {
        let transfer = media.transfer
        return VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 10) {
                Text("Downloading \(batch.current) of \(batch.total)")
                    .font(AppFont.figures(14, weight: 600))
                    .foregroundStyle(Theme.text)
                Text(transfer.name)
                    .font(AppFont.body(13))
                    .foregroundStyle(Theme.textSecondary)
                    .lineLimit(1)
                    .truncationMode(.middle)
                Spacer(minLength: 8)
                if media.pausedVideo {
                    Label("Live video paused", systemImage: "video.slash")
                        .font(AppFont.body(12, weight: 600))
                        .foregroundStyle(Theme.textTertiary)
                        .labelStyle(.titleAndIcon)
                        .lineLimit(1)
                }
                if let fraction = transfer.fraction {
                    Text("\(Int(fraction * 100)) %")
                        .font(AppFont.figures(13, weight: 600))
                        .foregroundStyle(Theme.text)
                }
                if transfer.bytesPerSecond > 0 {
                    Text(MediaFormat.rate(transfer.bytesPerSecond))
                        .font(AppFont.figures(13))
                        .foregroundStyle(Theme.textSecondary)
                }
                Button("Cancel") { media.cancelDownloads() }
                    .buttonStyle(.bordered)
                    .font(AppFont.body(14, weight: 600))
            }
            ProgressView(value: transfer.fraction ?? 0)
                .progressViewStyle(.linear)
                .tint(Theme.magenta)
        }
        .mediaPane()
    }

    private func summaryRow(_ batch: MediaStore.Batch) -> some View {
        let problem = batch.failed > 0
        return HStack(spacing: 10) {
            Image(systemName: problem ? "exclamationmark.triangle.fill" : "checkmark.circle.fill")
                .foregroundStyle(problem ? Theme.danger : Theme.ok)
            Text("\(batch.done) downloaded")
                .font(AppFont.figures(14, weight: 600))
                .foregroundStyle(Theme.text)
            if batch.failed > 0 {
                Text("\(batch.failed) failed")
                    .font(AppFont.figures(14, weight: 600))
                    .foregroundStyle(Theme.danger)
            }
            if batch.cancelled > 0 {
                Text("\(batch.cancelled) cancelled")
                    .font(AppFont.figures(14, weight: 600))
                    .foregroundStyle(Theme.textSecondary)
            }
            if problem, let error = batch.lastError {
                Text(error)
                    .font(AppFont.body(12))
                    .foregroundStyle(Theme.textSecondary)
                    .lineLimit(1)
                    .truncationMode(.middle)
            }
            Spacer(minLength: 8)
            #if targetEnvironment(macCatalyst)
            Button("Show in Finder") {
                MacSupport.showInFinder(MediaFolders.downloads) {
                    media.notice = "Cannot open the downloads folder in Finder."
                }
            }
            .buttonStyle(.bordered)
            #endif
            dismissButton
        }
        .mediaPane()
    }

    private var deletingRow: some View {
        let count = media.transfer.deletesPending
        return HStack(spacing: 10) {
            ProgressView()
            Text(count == 1 ? "Deleting 1 file…" : "Deleting \(count) files…")
                .font(AppFont.figures(14, weight: 600))
                .foregroundStyle(Theme.text)
            Spacer(minLength: 0)
        }
        .mediaPane()
    }

    private func noticeRow(_ notice: String) -> some View {
        HStack(spacing: 10) {
            if media.deleteFailures > 0 {
                Image(systemName: "exclamationmark.triangle.fill")
                    .foregroundStyle(Theme.danger)
            }
            Text(media.deleteFailures > 1 ? "\(media.deleteFailures) not deleted. \(notice)" : notice)
                .font(AppFont.body(14, weight: 600))
                .foregroundStyle(Theme.text)
                .lineLimit(2)
            Spacer(minLength: 8)
            dismissButton
        }
        .mediaPane()
    }

    private var dismissButton: some View {
        Button {
            media.clearSummary()
        } label: {
            Image(systemName: "xmark")
                .font(.system(size: 13, weight: .bold))
                .foregroundStyle(Theme.textSecondary)
                .frame(width: 36, height: 36)
                .contentShape(Rectangle())
        }
        .buttonStyle(.borderless)
        .accessibilityLabel("Dismiss")
    }
}

private extension View {
    /// The bottom bar's raised pane.
    func mediaPane() -> some View {
        padding(.horizontal, 14)
            .padding(.vertical, 10)
            .frame(maxWidth: .infinity, minHeight: 52, alignment: .leading)
            .background(Theme.surfaceRaised, in: RoundedRectangle(cornerRadius: 14, style: .continuous))
            .overlay(RoundedRectangle(cornerRadius: 14, style: .continuous).stroke(Theme.stroke))
            .shadow(color: .black.opacity(0.35), radius: 12, y: 4)
    }
}

// MARK: - States and shared pieces

/// What the payload's media service says when there is no list to show.
private struct MediaStateView: View {
    let media: MediaStore

    var body: some View {
        switch media.status {
        case .idle, .loading:
            VStack(spacing: 14) {
                ProgressView()
                Text("Reading the payload's media…")
                    .font(AppFont.body(14, weight: 500))
                    .foregroundStyle(Theme.textSecondary)
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(Theme.background)
        case .ready:
            MediaMessage(systemImage: "photo.on.rectangle", title: "No photos or videos on the payload.")
        case .failed:
            MediaMessage(systemImage: "exclamationmark.triangle", title: "Cannot read the payload's media",
                         detail: media.error.isEmpty ? nil : media.error,
                         action: ("Try again", { media.refresh() }))
        case .loginRequired:
            MediaMessage(systemImage: "lock", title: "Login required",
                         detail: "This payload asks for a login before it shares its media. The app does not support that yet.")
        case .starting:
            MediaMessage(systemImage: "hourglass", title: "The payload is still starting its cameras.",
                         detail: "Trying again in a few seconds.")
        }
    }
}

struct MediaMessage: View {
    let systemImage: String
    let title: String
    var detail: String?
    var action: (title: String, run: () -> Void)?

    var body: some View {
        VStack(spacing: 12) {
            Image(systemName: systemImage)
                .font(.system(size: 34, weight: .regular))
                .foregroundStyle(Theme.textTertiary)
            Text(title)
                .font(AppFont.display(18, weight: 600))
                .foregroundStyle(Theme.text)
                .multilineTextAlignment(.center)
            if let detail {
                Text(detail)
                    .font(AppFont.body(14))
                    .foregroundStyle(Theme.textSecondary)
                    .multilineTextAlignment(.center)
            }
            if let action {
                Button(action.title, action: action.run)
                    .buttonStyle(.borderedProminent)
                    .font(AppFont.body(15, weight: 700))
                    .padding(.top, 4)
            }
        }
        .frame(maxWidth: 420)
        .padding(24)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Theme.background)
    }
}

private struct RefreshButton: View {
    let media: MediaStore

    var body: some View {
        if media.status == .loading {
            ProgressView()
        } else {
            Button {
                media.refresh()
            } label: {
                Label("Refresh", systemImage: "arrow.clockwise")
            }
        }
    }
}

@MainActor
extension View {
    /// Asks before downloading while the live video runs: it shares the datalink.
    func mediaLiveVideoQuestion(_ media: MediaStore, origin: MediaStore.Origin) -> some View {
        let question = media.liveVideoQuestion?.origin == origin ? media.liveVideoQuestion : nil
        return alert(Text("Live video is on"),
                     isPresented: Binding(get: { question != nil },
                                          set: { shown in
                                              if !shown, media.liveVideoQuestion?.id == question?.id {
                                                  media.liveVideoQuestion = nil
                                              }
                                          }),
                     presenting: question) { question in
            Button("Pause live video") { media.answerLiveVideo(question, pause: true) }
            Button("Download alongside") { media.answerLiveVideo(question, pause: false) }
            Button("Cancel", role: .cancel) {}
        } message: { _ in
            Text("Downloading over the datalink can make the live video stutter or fall behind.")
        }
    }

    /// One confirmation before deleting from the payload.
    func mediaDeleteConfirmation(_ request: Binding<MediaDeleteRequest?>,
                                 perform: @escaping ([MediaFile]) -> Void) -> some View {
        let count = request.wrappedValue?.files.count ?? 0
        return alert(Text(count == 1 ? "Delete 1 file from the payload?" : "Delete \(count) files from the payload?"),
                     isPresented: Binding(get: { request.wrappedValue != nil },
                                          set: { if !$0 { request.wrappedValue = nil } }),
                     presenting: request.wrappedValue) { request in
            Button("Delete", role: .destructive) { perform(request.files) }
            Button("Cancel", role: .cancel) {}
        } message: { _ in
            Text("This cannot be undone.")
        }
    }
}

/// The hardware keyboard's modifier keys right now (iPad with a keyboard, Mac).
private enum KeyboardModifiers {
    static var command: Bool { pressed(.leftGUI) || pressed(.rightGUI) }
    static var shift: Bool { pressed(.leftShift) || pressed(.rightShift) }

    private static func pressed(_ key: GCKeyCode) -> Bool {
        GCKeyboard.coalesced?.keyboardInput?.button(forKeyCode: key)?.isPressed ?? false
    }
}
