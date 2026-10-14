import Foundation

/// Field diagnostics: a MAVLink log, and noticing when someone else commands the payload.
extension AppModel {
    /// Commands that change what the camera does, worth a warning when they come from someone else.
    private static let cameraCommands: Set<UInt32> = [530, 531, 532, 2000, 2001, 2500, 2501, 31013]

    /// Where logs go: Documents/Logs, visible in the Files app.
    static var logsDirectory: URL {
        URL.documentsDirectory.appending(path: "Logs", directoryHint: .isDirectory)
    }

    /// Starts or stops recording all MAVLink to a .tlog (QGroundControl, MAVExplorer, pymavlink).
    func toggleLogRecording() {
        guard let session else { return }
        if isRecordingLog {
            let complete = session.stopRecording()
            isRecordingLog = false
            show(complete ? "MAVLink log saved" : "MAVLink log saved, but incomplete (storage full?)",
                 complete ? .info : .error)
            return
        }
        do {
            try FileManager.default.createDirectory(at: Self.logsDirectory, withIntermediateDirectories: true)
        } catch {
            show("Cannot create the log folder: \(error.localizedDescription)", .error)
            return
        }
        let stamp = Date.now.formatted(.iso8601.year().month().day().dateSeparator(.dash)
            .time(includingFractionalSeconds: false).timeSeparator(.omitted))
        var url = Self.logsDirectory.appending(path: "imago-\(stamp).tlog")
        var n = 2
        while FileManager.default.fileExists(atPath: url.path) {
            url = Self.logsDirectory.appending(path: "imago-\(stamp)-\(n).tlog")
            n += 1
        }
        guard session.startRecording(to: url) else {
            show("Cannot write the MAVLink log", .error)
            return
        }
        isRecordingLog = true
        lastLogURL = url
        Haptics.tap()
    }

    /// The payload acknowledged a command that came from elsewhere (autopilot, GCS).
    func foreignCommandSeen(command: UInt32, result: Int32, from: String) {
        foreignCommandCount += 1
        guard Self.cameraCommands.contains(command) else { return }
        // At most one warning every few seconds: a GCS can repeat a command.
        let now = Date.now
        if let last = lastForeignToast, now.timeIntervalSince(last) < 3 { return }
        lastForeignToast = now
        show("\(Self.commandName(command)) sent to the camera by \(Self.senderName(from))", .error)
    }

    /// "system/component" as people know it.
    private static func senderName(_ from: String) -> String {
        let parts = from.split(separator: "/").compactMap { Int($0) }
        guard parts.count == 2 else { return from }
        switch (parts[0], parts[1]) {
        case (_, 1): return "the autopilot (\(from))"
        case (255, _), (_, 190): return "a ground station (\(from))"
        default: return "another controller (\(from))"
        }
    }
}
