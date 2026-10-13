import Foundation

/// Turns a stick deflection into gimbal rates. Shared by the on-screen joystick and gamepads so
/// both feel the same.
struct GimbalInputShaper {
    /// Rate at full deflection with no zoom, degrees per second.
    var maxRateDps: Float = 60
    /// Ignore deflections below this, so a resting thumb does not drift the gimbal.
    var deadzone: Float = 0.08
    /// 0 = linear, 1 = fully cubic. Higher values give finer control near the centre.
    var expo: Float = 0.55

    /// x: right positive, y: up positive, both in -1...1. zoom: current EO zoom (x).
    /// The rate shrinks with zoom so the image moves at a similar speed on screen.
    func rates(x: Float, y: Float, zoom: Float) -> (pitch: Float, yaw: Float) {
        let zoomFactor = 1 / max(1, zoom).squareRoot()
        let scale = maxRateDps * zoomFactor
        return (pitch: shape(y) * scale, yaw: shape(x) * scale)
    }

    func shape(_ value: Float) -> Float {
        let magnitude = min(abs(value), 1)
        guard magnitude > deadzone else { return 0 }
        let normalized = (magnitude - deadzone) / (1 - deadzone)
        let curved = (1 - expo) * normalized + expo * normalized * normalized * normalized
        return value < 0 ? -curved : curved
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
