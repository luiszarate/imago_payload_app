import Foundation

/// TEST HOOK for screenshots in CI (like the desktop app's --connect / --media): launch arguments
/// that connect, open Media and select files without touching the screen. Does nothing unless
/// launched with -imagoConnect.
///
///   -imagoConnect <payload IP>   connect a "Launch hook" profile to it (local port 14567, so
///                                orus-sim on the same Mac keeps 14566)
///   -imagoRtsp <url>             video URL (default: the one the payload announces)
///   -imagoOpenMedia              open Media once connected
///   -imagoSelect <n>             then select the first n files of the latest flight
///
/// The web server is the core's default port 8000 (the profile has no setting for it).
enum LaunchHook {
    private static let profileName = "Launch hook"

    @MainActor
    static func run(_ model: AppModel) async {
        let arguments = ProcessInfo.processInfo.arguments
        func value(_ key: String) -> String? {
            guard let index = arguments.firstIndex(of: key), arguments.indices.contains(index + 1) else {
                return nil
            }
            return arguments[index + 1]
        }
        guard let ip = value("-imagoConnect") else { return }

        var profile = model.profiles.first { $0.name == profileName }
            ?? ConnectionProfile(name: profileName, payloadIP: ip, videoURL: "")
        profile.payloadIP = ip
        profile.localPort = 14567
        profile.videoURL = value("-imagoRtsp") ?? ""
        model.saveProfile(profile)
        model.selectProfile(profile.id)
        model.connect()

        guard arguments.contains("-imagoOpenMedia") else { return }
        try? await Task.sleep(for: .seconds(2))
        model.openMediaScreen()

        guard let count = value("-imagoSelect").flatMap(Int.init), count > 0 else { return }
        // Wait for the list (up to 20 s), then select like a ⌘A on part of the flight.
        for _ in 0..<80 {
            if let media = model.media, let flight = media.selectedFlight {
                let files = Array(media.visibleFiles(in: flight).prefix(count))
                #if !targetEnvironment(macCatalyst)
                media.selecting = true
                #endif
                media.selectAll(files)
                return
            }
            try? await Task.sleep(for: .milliseconds(250))
        }
    }
}
