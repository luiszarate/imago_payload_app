import Foundation
import GameController

/// What a controller button does.
enum GamepadAction: String, Codable, CaseIterable, Identifiable {
    case shutter, record, stopTracking, toggleDetection, previousView, nextView, recenter,
         togglePointMode, toggleLockFollow, zoomIn, zoomOut

    var id: String { rawValue }

    var title: String {
        switch self {
        case .shutter: "Shutter (photo or record)"
        case .record: "Start / stop recording"
        case .stopTracking: "Stop tracking"
        case .toggleDetection: "AI detection on / off"
        case .previousView: "Previous view"
        case .nextView: "Next view"
        case .recenter: "Center gimbal"
        case .togglePointMode: "Point mode on / off"
        case .toggleLockFollow: "Gimbal lock / follow"
        case .zoomIn: "Zoom in (hold)"
        case .zoomOut: "Zoom out (hold)"
        }
    }

    /// Acts while the button is held instead of once per press.
    var isHeld: Bool { self == .zoomIn || self == .zoomOut }
}

/// What a controller axis drives.
enum GamepadAxisFunction: String, Codable, CaseIterable, Identifiable {
    case gimbalYaw, gimbalPitch, zoom

    var id: String { rawValue }

    var title: String {
        switch self {
        case .gimbalYaw: "Gimbal pan (yaw)"
        case .gimbalPitch: "Gimbal tilt (pitch)"
        case .zoom: "Zoom"
        }
    }
}

/// One analog input, named as in the controller's GCPhysicalInputProfile: a stand-alone axis
/// (a HOTAS throttle or twist), one axis of a stick or hat, or an analog button (a trigger).
struct GamepadAxisSource: Codable, Hashable {
    enum Component: String, Codable {
        case axis, x, y, button
    }

    var element: String
    var component: Component

    var label: String {
        switch component {
        case .axis, .button: element
        case .x: "\(element) ←→"
        case .y: "\(element) ↑↓"
        }
    }
}

struct GamepadAxisBinding: Codable, Hashable {
    var source: GamepadAxisSource
    var inverted = false
}

/// An input the controller is waiting for while the user assigns it.
enum GamepadLearnTarget: Equatable {
    case action(GamepadAction)
    case axis(GamepadAxisFunction)
}

/// Which controller inputs drive what. One mapping for the whole app, saved across launches.
struct GamepadMapping: Codable, Equatable {
    var axes: [GamepadAxisFunction: GamepadAxisBinding]
    /// Button element name -> action.
    var buttons: [String: GamepadAction]

    /// The layout the app always had: left stick gimbal, right stick zoom, face buttons for
    /// the camera, shoulders for the view.
    static let standard = GamepadMapping(
        axes: [
            .gimbalYaw: GamepadAxisBinding(source: GamepadAxisSource(element: GCInputLeftThumbstick, component: .x)),
            .gimbalPitch: GamepadAxisBinding(source: GamepadAxisSource(element: GCInputLeftThumbstick, component: .y)),
            .zoom: GamepadAxisBinding(source: GamepadAxisSource(element: GCInputRightThumbstick, component: .y)),
        ],
        buttons: [
            GCInputButtonA: .shutter,
            GCInputButtonB: .record,
            GCInputButtonX: .stopTracking,
            GCInputButtonY: .toggleDetection,
            GCInputLeftShoulder: .previousView,
            GCInputRightShoulder: .nextView,
            GCInputButtonMenu: .recenter,
        ])

    func button(for action: GamepadAction) -> String? {
        buttons.first { $0.value == action }?.key
    }

    /// One button per action and one action per button.
    mutating func bind(_ action: GamepadAction, to element: String) {
        buttons = buttons.filter { $0.value != action }
        buttons[element] = action
    }

    mutating func unbind(_ action: GamepadAction) {
        buttons = buttons.filter { $0.value != action }
    }

    /// One function per analog input.
    mutating func bind(_ function: GamepadAxisFunction, to source: GamepadAxisSource) {
        axes = axes.filter { $0.value.source != source }
        axes[function] = GamepadAxisBinding(source: source)
    }

    mutating func unbind(_ function: GamepadAxisFunction) {
        axes[function] = nil
    }

    // MARK: Storage

    private static let key = "controls.gamepadMapping"

    static func load(from defaults: UserDefaults = .standard) -> GamepadMapping {
        guard let data = defaults.data(forKey: key),
              let mapping = try? JSONDecoder().decode(GamepadMapping.self, from: data)
        else { return .standard }
        return mapping
    }

    func save(to defaults: UserDefaults = .standard) {
        if let data = try? JSONEncoder().encode(self) {
            defaults.set(data, forKey: Self.key)
        }
    }
}
