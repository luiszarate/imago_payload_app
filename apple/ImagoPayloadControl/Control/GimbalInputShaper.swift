import Foundation

/// Turns a stick deflection into gimbal rates. Shared by the on-screen joystick and gamepads so
/// both feel the same. Every field is a user setting (Settings › Gimbal), saved across launches.
struct GimbalInputShaper: Codable, Equatable {
    /// Rate at full deflection with no zoom, degrees per second, for pan (yaw) and tilt (pitch).
    var maxYawDps: Float = 60
    var maxPitchDps: Float = 60
    /// Ignore deflections below this, so a resting thumb does not drift the gimbal.
    var deadzone: Float = 0.08
    /// 0 = linear, 1 = fully cubic. Higher values give finer control near the centre.
    var expo: Float = 0.55
    /// Rate of a D-pad or button: always fully "pressed", so it gets its own, usually lower, speed.
    var digitalYawDps: Float = 20
    var digitalPitchDps: Float = 20

    static let speedRange: ClosedRange<Float> = 2...120
    static let deadzoneRange: ClosedRange<Float> = 0...0.3

    /// x: right positive, y: up positive, both in -1...1. zoom: current EO zoom (x).
    /// digital: the input is a D-pad or button (on or off) rather than a stick.
    /// The rate shrinks with zoom so the image moves at a similar speed on screen.
    func rates(x: Float, y: Float, zoom: Float, digitalX: Bool = false,
               digitalY: Bool = false) -> (pitch: Float, yaw: Float) {
        let zoomFactor = 1 / max(1, zoom).squareRoot()
        let yaw = digitalX ? sign(x) * digitalYawDps : shape(x) * maxYawDps
        let pitch = digitalY ? sign(y) * digitalPitchDps : shape(y) * maxPitchDps
        return (pitch: pitch * zoomFactor, yaw: yaw * zoomFactor)
    }

    func shape(_ value: Float) -> Float {
        let magnitude = min(abs(value), 1)
        guard magnitude > deadzone else { return 0 }
        let normalized = (magnitude - deadzone) / (1 - deadzone)
        let curved = (1 - expo) * normalized + expo * normalized * normalized * normalized
        return value < 0 ? -curved : curved
    }

    /// A digital input: -1, 0 or 1 (a half-pressed analog button counts once past half way).
    private func sign(_ value: Float) -> Float {
        abs(value) < 0.5 ? 0 : (value < 0 ? -1 : 1)
    }

    // MARK: Storage

    private static let key = "controls.gimbalShaper"
    private static let legacyMaxRateKey = "controls.gimbalMaxRateDps"

    static func load(from defaults: UserDefaults = .standard) -> GimbalInputShaper {
        if let data = defaults.data(forKey: key),
           let shaper = try? JSONDecoder().decode(GimbalInputShaper.self, from: data) {
            return shaper
        }
        var shaper = GimbalInputShaper()
        if let legacy = defaults.object(forKey: legacyMaxRateKey) as? Float {
            shaper.maxYawDps = legacy // the single speed setting of the previous version
            shaper.maxPitchDps = legacy
        }
        return shaper
    }

    func save(to defaults: UserDefaults = .standard) {
        if let data = try? JSONEncoder().encode(self) {
            defaults.set(data, forKey: Self.key)
        }
    }
}

/// Value written on one thread and read on another without a data race.
final class Locked<Value>: @unchecked Sendable {
    private var value: Value
    private let lock = NSLock()

    init(_ value: Value) { self.value = value }

    func get() -> Value {
        lock.lock()
        defer { lock.unlock() }
        return value
    }

    func set(_ newValue: Value) {
        lock.lock()
        value = newValue
        lock.unlock()
    }
}
