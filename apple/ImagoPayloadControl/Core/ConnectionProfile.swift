import Foundation

/// How the app reaches one payload. Like Payload Plus, control and video are configured
/// separately: MAVLink goes to the Orus L, video usually comes from the datalink (Herelink).
struct ConnectionProfile: Codable, Identifiable, Hashable {
    enum MavlinkMode: String, Codable, CaseIterable, Identifiable {
        /// Send to the payload's IP (Herelink Ethernet bridge, direct cable).
        case direct
        /// Listen on a local port and answer whoever sends MAVLink (Herelink forwarding).
        case listen
        var id: String { rawValue }
        var title: String { self == .direct ? "Payload IP" : "Listen (forwarded)" }
    }

    enum VideoTransport: String, Codable, CaseIterable, Identifiable {
        case auto, udp, tcp
        var id: String { rawValue }
        var title: String {
            switch self {
            case .auto: return "Auto"
            case .udp: return "UDP"
            case .tcp: return "TCP"
            }
        }
    }

    enum GimbalProtocol: String, Codable, CaseIterable, Identifiable {
        /// GIMBAL_DEVICE_SET_ATTITUDE, as the Gremsy SDK sends it.
        case attitude
        /// MAV_CMD_DO_MOUNT_CONTROL, the older MAVLink gimbal protocol.
        case mountControl
        /// Through the autopilot's gimbal manager (PX4, MAVLink gimbal protocol v2): the app
        /// takes primary control when it connects and gives it back when it disconnects.
        case manager
        var id: String { rawValue }
        var title: String {
            switch self {
            case .attitude: "Gimbal v2 (direct)"
            case .mountControl: "Mount control"
            case .manager: "Autopilot gimbal manager"
            }
        }
    }

    var id = UUID()
    var name: String
    var mavlinkMode: MavlinkMode = .direct
    var payloadIP: String
    var mavlinkPort: UInt16 = 14566
    /// 0 = default (same as mavlinkPort in direct mode, 14550 in listen mode).
    var localPort: UInt16 = 0
    /// Empty = use the URL the payload announces over MAVLink.
    var videoURL: String
    var videoTransport: VideoTransport = .auto
    var gimbalProtocol: GimbalProtocol = .attitude
    /// Sends RC_MODE = standard when the link comes up (the Gremsy SDK does this before
    /// rate control). Off until confirmed on the user's firmware.
    var setStandardRCMode: Bool = false
    /// Restart the video when it stays this far behind live (a queue upstream). 0 = never.
    var maxVideoLagMs: Int = 500
    /// Keeps the device's Wi-Fi from dozing between video packets (which delivers them, and
    /// telemetry, in bursts) with a 1-byte datagram ~30 times a second to the video host's
    /// discard port. Off by default: the first version (RTCP to the stream) stalled the
    /// Herelink's video, and its stored "on" lives under the old key, which is ignored.
    var keepWifiAwake: Bool = false
    static let videoLagChoices = [0, 300, 500, 1000]

    init(name: String, mavlinkMode: MavlinkMode = .direct, payloadIP: String,
         mavlinkPort: UInt16 = 14566, localPort: UInt16 = 0, videoURL: String,
         videoTransport: VideoTransport = .auto, gimbalProtocol: GimbalProtocol = .attitude,
         setStandardRCMode: Bool = false) {
        self.name = name
        self.mavlinkMode = mavlinkMode
        self.payloadIP = payloadIP
        self.mavlinkPort = mavlinkPort
        self.localPort = localPort
        self.videoURL = videoURL
        self.videoTransport = videoTransport
        self.gimbalProtocol = gimbalProtocol
        self.setStandardRCMode = setStandardRCMode
    }

    // Tolerant decoding: fields added in later versions get their defaults instead of
    // invalidating every saved profile.
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = try c.decodeIfPresent(UUID.self, forKey: .id) ?? UUID()
        name = try c.decodeIfPresent(String.self, forKey: .name) ?? "Payload"
        mavlinkMode = try c.decodeIfPresent(MavlinkMode.self, forKey: .mavlinkMode) ?? .direct
        payloadIP = try c.decodeIfPresent(String.self, forKey: .payloadIP) ?? ""
        mavlinkPort = try c.decodeIfPresent(UInt16.self, forKey: .mavlinkPort) ?? 14566
        localPort = try c.decodeIfPresent(UInt16.self, forKey: .localPort) ?? 0
        videoURL = try c.decodeIfPresent(String.self, forKey: .videoURL) ?? ""
        videoTransport = try c.decodeIfPresent(VideoTransport.self, forKey: .videoTransport) ?? .auto
        gimbalProtocol = try c.decodeIfPresent(GimbalProtocol.self, forKey: .gimbalProtocol) ?? .attitude
        setStandardRCMode = try c.decodeIfPresent(Bool.self, forKey: .setStandardRCMode) ?? false
        maxVideoLagMs = try c.decodeIfPresent(Int.self, forKey: .maxVideoLagMs) ?? 500
        keepWifiAwake = try c.decodeIfPresent(Bool.self, forKey: .keepWifiAwake) ?? false
    }

    /// Field setup confirmed with the user: Orus L behind Herelink, video from Herelink.
    static let herelink = ConnectionProfile(
        name: "Orus L · Herelink",
        payloadIP: "192.168.144.240",
        videoURL: "rtsp://192.168.43.1:8554/fpv_stream")

    /// Orus L on a direct Ethernet link, video announced by the payload.
    static let direct = ConnectionProfile(
        name: "Orus L · Direct",
        payloadIP: "192.168.55.1",
        videoURL: "")

    /// tools/orus-sim running on the Mac, for the iOS Simulator. The local port differs from
    /// the simulator's 14566 because both run on the same machine.
    static let simulator = ConnectionProfile(
        name: "orus-sim on this Mac",
        payloadIP: "127.0.0.1",
        localPort: 14567,
        videoURL: "")

    /// Problem with the profile, or nil if it can be used.
    var validationError: String? {
        if name.trimmingCharacters(in: .whitespaces).isEmpty {
            return "Enter a name."
        }
        if mavlinkMode == .direct && !Self.isIPv4(payloadIP) {
            return "Enter the payload IP address, e.g. 192.168.144.240."
        }
        let url = videoURL.trimmingCharacters(in: .whitespaces)
        if !url.isEmpty && !url.lowercased().hasPrefix("rtsp://") {
            return "The video URL must start with rtsp://"
        }
        if mavlinkMode == .listen && url.isEmpty {
            return "Listen mode needs a video URL."
        }
        return nil
    }

    static func isIPv4(_ text: String) -> Bool {
        let parts = text.trimmingCharacters(in: .whitespaces).split(separator: ".", omittingEmptySubsequences: false)
        guard parts.count == 4 else { return false }
        return parts.allSatisfy { part in
            guard !part.isEmpty, part.count <= 3, let value = Int(part) else { return false }
            return (0...255).contains(value)
        }
    }
}

/// Saved profiles, persisted in UserDefaults as JSON.
struct ProfileStore {
    private static let profilesKey = "connectionProfiles.v1"
    private static let selectedKey = "connectionProfiles.selected"
    private let defaults: UserDefaults

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
    }

    func load() -> [ConnectionProfile] {
        guard let data = defaults.data(forKey: Self.profilesKey),
              let profiles = try? JSONDecoder().decode([ConnectionProfile].self, from: data),
              !profiles.isEmpty
        else {
            return [.herelink, .direct, .simulator]
        }
        return profiles
    }

    func save(_ profiles: [ConnectionProfile]) {
        if let data = try? JSONEncoder().encode(profiles) {
            defaults.set(data, forKey: Self.profilesKey)
        }
    }

    var selectedID: UUID? {
        get { defaults.string(forKey: Self.selectedKey).flatMap(UUID.init(uuidString:)) }
        nonmutating set { defaults.set(newValue?.uuidString, forKey: Self.selectedKey) }
    }
}
