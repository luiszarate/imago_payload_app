import Foundation
import ImagoCore
import Observation

/// The Media screen: the payload's photos and videos, read through its web server by the core.
extension AppModel {
    /// The top bar's Media button. Without a session there is nothing to list.
    func openMediaScreen() {
        guard isConnected else {
            show("Connect to the payload to see its media.", .info)
            return
        }
        Haptics.tap()
        showMedia = true
    }

    /// Media appeared, or the session came back while it was up (iOS disconnects in the
    /// background and connects again on return): (re)open the store for the running session.
    func mediaAppeared() {
        guard let session else { return }
        if let media, media.session === session {
            media.open()
            return
        }
        media?.end()
        let store = MediaStore(session: session)
        media = store
        store.open()
    }

    func closeMediaScreen() {
        media?.close()
        showMedia = false
    }
}

/// The payload's media while a session runs. Polls the core's media state at 8 Hz while the
/// screen is open and copies the list only when its revision changes. Nothing here waits for
/// the network: the core does that on its own threads.
@MainActor
@Observable
final class MediaStore {
    enum Status: Equatable {
        case idle, loading, ready, failed, loginRequired, starting
    }

    enum Filter: String, CaseIterable, Identifiable {
        case all, photos, videos
        var id: String { rawValue }
        var title: String {
            switch self {
            case .all: "All"
            case .photos: "Photos"
            case .videos: "Videos"
            }
        }
    }

    /// The transfer the core reports: the file on its way and how far it got.
    struct Transfer: Equatable {
        var name = ""
        var bytes: Int64 = 0
        var total: Int64?
        var bytesPerSecond: Float = 0
        var queued = 0
        var deletesPending = 0

        var fraction: Double? {
            guard let total, total > 0 else { return nil }
            return min(Double(bytes) / Double(total), 1)
        }
    }

    /// The downloads asked for since the queue was last empty (previews not counted).
    struct Batch: Equatable {
        var total = 0
        var done = 0
        var failed = 0
        var cancelled = 0
        var lastError: String?
        /// request ID -> "folder/name", for the on-device check once whole.
        var pending: [UInt32: String] = [:]

        var running: Bool { !pending.isEmpty }
        /// The one in progress, counted from 1.
        var current: Int { min(done + failed + cancelled + 1, total) }
    }

    /// Where a download was asked for: each screen answers the live video question itself.
    enum Origin { case browser, preview }

    struct LiveVideoQuestion: Identifiable {
        let id = UUID()
        let files: [MediaFile]
        let origin: Origin
    }

    enum PreviewState: Equatable {
        case loading
        case ready(URL)
        case failed(String)
    }

    struct StorageUse: Equatable {
        var usedGB: Float
        var totalGB: Float
        var fraction: Double { totalGB > 0 ? Double(min(max(usedGB / totalGB, 0), 1)) : 0 }
        var text: String { String(format: "%.1f of %.1f GB", usedGB, totalGB) }
    }

    /// A request to scroll the sheet to a file (from the flight's timeline strip).
    struct ScrollRequest: Equatable {
        let name: String
        let token = UUID()
    }

    @ObservationIgnored let session: PayloadSession
    @ObservationIgnored let thumbnails: MediaThumbnails

    private(set) var status: Status = .idle
    private(set) var error = ""
    private(set) var files: [MediaFile] = []
    private(set) var flights: [MediaFlight] = []
    private(set) var storageLabels: [String] = []
    private(set) var currentStorage: Int?
    private(set) var storageUse: StorageUse?
    private(set) var transfer = Transfer()
    private(set) var batch: Batch?
    /// "folder/name" of the files already in Documents/Media.
    private(set) var localKeys: Set<String> = []
    private(set) var liveVideoOn = false
    /// This screen stopped the live video for its downloads and starts it again afterwards.
    private(set) var pausedVideo = false
    /// A short message in the bottom bar ("Already on this device."), until the next action.
    var notice: String?
    private(set) var deleteFailures = 0
    private(set) var previews: [String: PreviewState] = [:]

    var filter: Filter = .all
    var selectedFlightID: MediaFlight.ID?
    var selection: Set<String> = [] {
        didSet {
            // A new selection is the next action: the last batch's summary goes.
            if selection.count > oldValue.count {
                clearSummary()
            }
        }
    }
    /// Touch: taps toggle the selection instead of opening the preview.
    var selecting = false
    var scrollRequest: ScrollRequest?
    var liveVideoQuestion: LiveVideoQuestion?

    @ObservationIgnored private var timer: Timer?
    @ObservationIgnored private var revision: UInt32?
    @ObservationIgnored private var reading = false
    @ObservationIgnored private var isOpen = false
    /// The first file of the selected flight, to find it again when its start time moves.
    @ObservationIgnored private var flightAnchor: String?
    /// Shift-click and Shift-tap extend from here.
    @ObservationIgnored private var selectionAnchor: String?
    @ObservationIgnored private var retryAt: Date?
    @ObservationIgnored private var previewRequests: [UInt32: String] = [:]
    /// Starts and stops of the live video run here, in order, off the main thread: stopping
    /// joins the video thread.
    @ObservationIgnored private let videoQueue = DispatchQueue(label: "aero.imago.media.video")

    /// The payload says it is still starting its cameras (404): ask again after this long.
    static let startingRetry: TimeInterval = 5

    init(session: PayloadSession) {
        self.session = session
        thumbnails = MediaThumbnails(session: session)
    }

    // MARK: Lifecycle

    func open() {
        guard !isOpen else { return }
        isOpen = true
        session.setMediaActive(true)
        session.refreshMedia()
        revision = nil
        scanDownloads()
        let timer = Timer(timeInterval: 1.0 / 8.0, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.poll() }
        }
        RunLoop.main.add(timer, forMode: .common)
        self.timer = timer
        poll()
    }

    /// Downloads and deletes go on in the core; the background lookup of thumbnails pauses.
    func close() {
        guard isOpen else { return }
        isOpen = false
        timer?.invalidate()
        timer = nil
        session.setMediaActive(false)
        liveVideoQuestion = nil
        resumeVideo()
    }

    /// The session is going away.
    func end() {
        timer?.invalidate()
        timer = nil
        isOpen = false
    }

    func refresh() {
        retryAt = nil
        session.refreshMedia()
    }

    func setStorage(_ index: Int) {
        guard index != currentStorage else { return }
        selection.removeAll()
        session.setMediaStorage(index)
    }

    // MARK: Polling

    private func poll() {
        let state = session.mediaState()
        let newStatus: Status = switch state.status {
        case IMAGO_MEDIA_STATUS_LOADING: .loading
        case IMAGO_MEDIA_STATUS_READY: .ready
        case IMAGO_MEDIA_STATUS_FAILED: .failed
        case IMAGO_MEDIA_STATUS_LOGIN_REQUIRED: .loginRequired
        case IMAGO_MEDIA_STATUS_STARTING: .starting
        default: .idle
        }
        set(newStatus, \.status)
        set(String(cTuple: state.error), \.error)
        let labels = Self.storageLabels(state)
        set(labels, \.storageLabels)
        set(state.storage_current >= 0 ? Int(state.storage_current) : nil, \.currentStorage)

        var next = Transfer()
        if state.downloading {
            next.name = String(cTuple: state.download_name)
            next.bytes = state.download_bytes
            next.total = state.download_total >= 0 ? state.download_total : nil
            next.bytesPerSecond = state.download_bytes_per_s
        }
        next.queued = Int(state.downloads_queued)
        next.deletesPending = Int(state.deletes_pending)
        set(next, \.transfer)

        let payload = session.state()
        set(payload.video_active, \.liveVideoOn)
        set(payload.storage_valid && payload.storage_total_mib > 0
            ? StorageUse(usedGB: (payload.storage_total_mib - payload.storage_available_mib) / 1024,
                         totalGB: payload.storage_total_mib / 1024)
            : nil, \.storageUse)

        // The cameras are still starting: try again by itself.
        if status == .starting {
            if let retryAt {
                if Date.now >= retryAt {
                    refresh()
                }
            } else {
                retryAt = Date.now.addingTimeInterval(Self.startingRetry)
            }
        } else {
            retryAt = nil
        }

        if state.revision != revision, !reading {
            read(revision: state.revision, fileCount: Int(state.file_count),
                 flightCount: Int(state.flight_count))
        }
    }

    /// Copies and converts the list off the main thread: up to a few thousand names, and the
    /// revision bumps with every thumbnail while they arrive.
    private func read(revision: UInt32, fileCount: Int, flightCount: Int) {
        reading = true
        let session = session
        Task { [weak self] in
            let (files, flights) = await Task.detached(priority: .userInitiated) { () -> ([MediaFile], [MediaFlight]) in
                let files = session.mediaFiles(expected: fileCount).map(MediaFile.init)
                let flights = session.mediaFlights(expected: flightCount)
                    .map { MediaFlight($0, fileCount: files.count) }
                return (files, flights)
            }.value
            guard let self else { return }
            self.reading = false
            // The revision read before the copy: a change during it is read on the next poll.
            self.revision = revision
            self.apply(files: files, flights: flights)
        }
    }

    private func apply(files newFiles: [MediaFile], flights newFlights: [MediaFlight]) {
        set(newFiles, \.files)
        set(newFlights, \.flights)
        let names = Set(newFiles.map(\.name))
        let kept = selection.intersection(names)
        if kept != selection {
            selection = kept
        }
        if let id = selectedFlightID, newFlights.contains(where: { $0.id == id }) {
            // still there
        } else if let anchor = flightAnchor, let index = newFiles.firstIndex(where: { $0.name == anchor }),
                  newFlights.indices.contains(newFiles[index].flight) {
            selectedFlightID = newFlights[newFiles[index].flight].id
        } else {
            // Latest flight on open, and when the selected one is gone.
            selectedFlightID = newFlights.first?.id
        }
        flightAnchor = selectedFlight.flatMap { rollFiles($0).first?.name }
    }

    private func set<T: Equatable>(_ value: T, _ keyPath: ReferenceWritableKeyPath<MediaStore, T>) {
        if self[keyPath: keyPath] != value {
            self[keyPath: keyPath] = value
        }
    }

    private static func storageLabels(_ state: imago_media_state) -> [String] {
        let count = min(Int(state.storage_count), Int(IMAGO_MEDIA_MAX_STORAGES))
        guard count > 0 else { return [] }
        return withUnsafeBytes(of: state.storage_label) { raw in
            let stride = raw.count / Int(IMAGO_MEDIA_MAX_STORAGES)
            return (0..<count).map { i in
                let slice = raw[(i * stride)..<((i + 1) * stride)]
                return String(decoding: slice.prefix { $0 != 0 }, as: UTF8.self)
            }
        }
    }

    // MARK: Flights and files

    var selectedFlight: MediaFlight? {
        flights.first { $0.id == selectedFlightID }
    }

    func flight(_ id: MediaFlight.ID?) -> MediaFlight? {
        flights.first { $0.id == id }
    }

    func selectFlight(_ id: MediaFlight.ID) {
        guard id != selectedFlightID else { return }
        selectedFlightID = id
        flightAnchor = flight(id).flatMap { rollFiles($0).first?.name }
        selection.removeAll()
        selectionAnchor = nil
    }

    /// A flight's files in capture order, oldest first: the sheet reads like the roll was shot,
    /// left to right like its timeline strip.
    func rollFiles(_ flight: MediaFlight) -> [MediaFile] {
        files[flight.files].reversed()
    }

    func visibleFiles(in flight: MediaFlight) -> [MediaFile] {
        let all = rollFiles(flight)
        switch filter {
        case .all: return all
        case .photos: return all.filter(\.isPhoto)
        case .videos: return all.filter { $0.kind == .video }
        }
    }

    func file(named name: String) -> MediaFile? {
        files.first { $0.name == name }
    }

    func folderName(for file: MediaFile) -> String {
        flights.indices.contains(file.flight) ? flights[file.flight].folderName : "No capture time"
    }

    func localURL(for file: MediaFile) -> URL {
        MediaFolders.download(file.name, folder: folderName(for: file))
    }

    func isLocal(_ file: MediaFile) -> Bool {
        localKeys.contains("\(folderName(for: file))/\(file.name)")
    }

    func requestThumbnail(_ name: String) {
        session.requestMediaThumbnail(name)
    }

    /// The timeline strip was tapped or dragged at this point of the flight (0...1): scroll the
    /// sheet to the file captured nearest to that moment.
    func scrub(_ flight: MediaFlight, to fraction: Double) {
        guard let start = flight.start, let end = flight.end else { return }
        let moment = start.addingTimeInterval(end.timeIntervalSince(start) * min(max(fraction, 0), 1))
        let nearest = visibleFiles(in: flight)
            .compactMap { file in file.captureTime.map { (file.name, abs($0.timeIntervalSince(moment))) } }
            .min { $0.1 < $1.1 }
        if let nearest, nearest.0 != scrollRequest?.name {
            scrollRequest = ScrollRequest(name: nearest.0)
        }
    }

    private func scanDownloads() {
        Task { [weak self] in
            let keys = await Task.detached(priority: .utility) { MediaFolders.scanDownloads() }.value
            self?.localKeys = keys
        }
    }

    // MARK: Selection

    func toggle(_ file: MediaFile) {
        if selection.contains(file.name) {
            selection.remove(file.name)
        } else {
            selection.insert(file.name)
        }
        selectionAnchor = file.name
    }

    func selectOnly(_ file: MediaFile) {
        selection = [file.name]
        selectionAnchor = file.name
    }

    /// Shift-click: from the last file clicked to this one, in the order on screen.
    func extendSelection(to file: MediaFile, in visible: [MediaFile]) {
        guard let anchor = selectionAnchor, let from = visible.firstIndex(where: { $0.name == anchor }),
              let to = visible.firstIndex(where: { $0.name == file.name }) else {
            selectOnly(file)
            return
        }
        selection = Set(visible[min(from, to)...max(from, to)].map(\.name))
    }

    func selectAll(_ visible: [MediaFile]) {
        selection = Set(visible.map(\.name))
        selectionAnchor = visible.first?.name
    }

    func clearSelection() {
        selection.removeAll()
        selectionAnchor = nil
        selecting = false
    }

    var selectedFiles: [MediaFile] {
        files.filter { selection.contains($0.name) }.reversed()
    }

    var selectedSize: Int64 {
        selectedFiles.reduce(0) { $0 + ($1.size ?? 0) }
    }

    // MARK: Downloads

    /// Downloads to Documents/Media/<flight>/, skipping what is already there. While the live
    /// video runs, the operator chooses first whether it pauses (it shares the datalink).
    func download(_ wanted: [MediaFile], from origin: Origin = .browser) {
        clearSummary()
        let missing = wanted.filter { !isLocal($0) }
        guard !missing.isEmpty else {
            notice = wanted.count == 1 ? "Already on this device." : "All \(wanted.count) are already on this device."
            return
        }
        // Asked once per batch: a batch that already runs alongside, or paused, stays so.
        if liveVideoOn, !pausedVideo, batch?.running != true {
            liveVideoQuestion = LiveVideoQuestion(files: missing, origin: origin)
            return
        }
        enqueue(missing)
    }

    /// The question comes back with the alert's button, so it does not matter whether the
    /// alert clears `liveVideoQuestion` before or after.
    func answerLiveVideo(_ question: LiveVideoQuestion, pause: Bool) {
        liveVideoQuestion = nil
        if pause {
            pauseVideo()
        }
        enqueue(question.files)
    }

    func downloadFlight(_ flight: MediaFlight) {
        download(rollFiles(flight))
    }

    private func enqueue(_ list: [MediaFile]) {
        var next = batch?.running == true ? batch! : Batch()
        for file in list {
            let key = "\(folderName(for: file))/\(file.name)"
            guard !next.pending.values.contains(key) else { continue }
            let id = session.downloadMedia(file.name, to: localURL(for: file))
            guard id != 0 else {
                next.failed += 1
                next.total += 1
                next.lastError = "The payload's web server is not reachable."
                continue
            }
            next.pending[id] = key
            next.total += 1
        }
        batch = next
        clearSelection()
        Haptics.tap()
        if !next.running {
            batchEnded()
        }
    }

    func cancelDownloads() {
        session.cancelMediaDownloads()
    }

    private func batchEnded() {
        resumeVideo()
        if let batch, batch.failed == 0, batch.cancelled == 0 {
            Haptics.confirm()
        }
    }

    /// Dismisses the "12 downloaded" line and any notice.
    func clearSummary() {
        if batch?.running == false {
            batch = nil
        }
        notice = nil
        deleteFailures = 0
    }

    private func pauseVideo() {
        guard !pausedVideo else { return }
        pausedVideo = true
        let session = session
        videoQueue.async { session.stopVideo() }
    }

    private func resumeVideo() {
        guard pausedVideo else { return }
        pausedVideo = false
        let session = session
        videoQueue.async { session.startVideo() }
    }

    /// This file is in the running batch.
    func isDownloading(_ file: MediaFile) -> Bool {
        batch?.pending.values.contains("\(folderName(for: file))/\(file.name)") == true
    }

    // MARK: Deletes

    /// Queued in the core; the list drops them when the payload has deleted them.
    func delete(_ list: [MediaFile]) {
        clearSummary()
        for file in list {
            session.deleteMedia(file.name)
            thumbnails.forget(file.name)
        }
        clearSelection()
        Haptics.tap()
    }

    // MARK: Preview

    /// Where the preview reads a photo from: the download if there is one, else the cache,
    /// fetched now through the same download queue.
    func preparePreview(_ file: MediaFile) {
        guard file.previewable else { return }
        if isLocal(file) {
            previews[file.name] = .ready(localURL(for: file))
            return
        }
        if case .ready = previews[file.name] { return }
        if previews[file.name] == .loading { return }
        let url = MediaFolders.preview(file.name)
        if FileManager.default.fileExists(atPath: url.path(percentEncoded: false)) {
            previews[file.name] = .ready(url)
            return
        }
        let id = session.downloadMedia(file.name, to: url)
        guard id != 0 else {
            previews[file.name] = .failed("The payload's web server is not reachable.")
            return
        }
        previewRequests[id] = file.name
        previews[file.name] = .loading
    }

    /// A failed preview can be asked for again.
    func retryPreview(_ file: MediaFile) {
        previews[file.name] = nil
        preparePreview(file)
    }

    // MARK: Results

    func requestEnded(_ id: UInt32, op: UInt32, result: Int32, name: String) {
        switch op {
        case IMAGO_MEDIA_OP_DOWNLOAD.rawValue:
            if let previewName = previewRequests.removeValue(forKey: id) {
                previews[previewName] = result == IMAGO_RESULT_ACCEPTED
                    ? .ready(MediaFolders.preview(previewName))
                    : .failed(result == IMAGO_RESULT_CANCELLED ? "Cancelled." : failure(name))
                return
            }
            guard var current = batch, let key = current.pending.removeValue(forKey: id) else { return }
            switch result {
            case IMAGO_RESULT_ACCEPTED:
                current.done += 1
                localKeys.insert(key)
            case IMAGO_RESULT_CANCELLED:
                current.cancelled += 1
            default:
                current.failed += 1
                current.lastError = failure(name)
            }
            batch = current
            if !current.running {
                batchEnded()
            }
        case IMAGO_MEDIA_OP_DELETE.rawValue:
            if result != IMAGO_RESULT_ACCEPTED {
                deleteFailures += 1
                notice = failure(name)
            }
        default:
            break // the list and the storage come with the state
        }
    }

    /// The state's error text ("<file>: <why>"), else the file's name.
    private func failure(_ name: String) -> String {
        let text = String(cTuple: session.mediaState().error)
        return text.isEmpty ? "\(name): failed" : text
    }
}
