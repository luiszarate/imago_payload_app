import Foundation
import GameController

/// Physical controllers: Xbox, PlayStation, MFi, and HOTAS or flight sticks that present
/// themselves as one of those (iPadOS exposes no other HID joysticks).
///
/// Every input of the controller is read through its GCPhysicalInputProfile, so extra buttons,
/// hats and axes of a HOTAS can be assigned too. What each input does comes from a
/// `GamepadMapping` the user edits ("press the button to assign it").
///
/// Input is handled on a dedicated high-priority queue: gimbal rates and zoom go straight to
/// the core, never through the main thread. Other actions are forwarded to the main queue.
final class GamepadController {
    /// An input the controller reported while learning.
    enum Learned {
        case button(String)
        case axis(GamepadAxisSource)
    }

    /// Called from the gamepad queue with gimbal rates in degrees per second.
    var onGimbalRate: ((Float, Float) -> Void)?
    /// Called from the gamepad queue: -1 zoom out, 0 stop, 1 zoom in.
    var onZoom: ((Int32) -> Void)?
    /// Called on the main queue for actions that are not held (zoom is sent with onZoom).
    var onAction: ((GamepadAction) -> Void)?
    /// Called on the main queue with what was pressed or moved while learning.
    var onLearned: ((GamepadLearnTarget, Learned) -> Void)?
    /// Called on the main queue with the controller name, or nil when none is connected.
    var onConnectionChange: ((String?) -> Void)?

    /// Latest EO zoom, used to scale the gimbal rate.
    let zoomLevel = Locked<Float>(1)
    private let shaper = Locked(GimbalInputShaper())
    private let mapping = Locked(GamepadMapping.standard)
    /// Set while the user assigns an input; the next input is reported instead of acted on.
    private let learning = Locked<GamepadLearnTarget?>(nil)

    private let queue = DispatchQueue(label: "aero.imago.gamepad", qos: .userInteractive)
    // Gamepad queue only.
    private var zoomDirection: Int32 = 0
    private var zoomButtonDirection: Int32 = 0
    private var pressed = Set<String>()
    private var lastRates: (pitch: Float, yaw: Float) = (0, 0)
    private var observers: [NSObjectProtocol] = []
    /// iOS also announces controllers that were paired at launch; attach each one once.
    private var attached = Set<ObjectIdentifier>()

    /// Deflection that counts as "this input" while learning.
    private static let learnThreshold: Float = 0.6

    init() {
        let center = NotificationCenter.default
        observers.append(center.addObserver(forName: .GCControllerDidConnect, object: nil,
                                            queue: .main) { [weak self] note in
            guard let controller = note.object as? GCController else { return }
            self?.attach(controller)
        })
        observers.append(center.addObserver(forName: .GCControllerDidDisconnect, object: nil,
                                            queue: .main) { [weak self] note in
            if let controller = note.object as? GCController {
                self?.attached.remove(ObjectIdentifier(controller))
            }
            self?.controllerDisconnected()
        })
    }

    deinit {
        for observer in observers {
            NotificationCenter.default.removeObserver(observer)
        }
    }

    /// Attaches controllers that were connected before the app started.
    func start() {
        GCController.controllers().forEach(attach)
    }

    private func attach(_ controller: GCController) {
        guard attached.insert(ObjectIdentifier(controller)).inserted else { return }
        controller.handlerQueue = queue
        controller.physicalInputProfile.valueDidChangeHandler = { [weak self] profile, element in
            self?.changed(profile, element)
        }
        let name = Self.name(of: controller)
        DispatchQueue.main.async { [weak self] in self?.onConnectionChange?(name) }
    }

    // MARK: Configuration (any thread)
    //
    // Each change stops whatever the controller is driving: a held button or stick may no
    // longer be mapped, or its release may go to learning, and neither must leave the gimbal
    // or the zoom moving. The next input starts it again with the new settings.

    func setMapping(_ newMapping: GamepadMapping) {
        mapping.set(newMapping)
        stopAll()
    }

    func setShaper(_ newShaper: GimbalInputShaper) {
        shaper.set(newShaper)
        stopAll()
    }

    func setLearning(_ target: GamepadLearnTarget?) {
        learning.set(target)
        stopAll()
    }

    private func stopAll() {
        queue.async { [weak self] in
            guard let self else { return }
            self.lastRates = (0, 0)
            self.onGimbalRate?(0, 0)
            self.pressed.removeAll()
            self.zoomButtonDirection = 0
            self.setZoom(0)
        }
    }

    private static func name(of controller: GCController) -> String {
        controller.vendorName ?? controller.productCategory
    }

    // MARK: Input (gamepad queue)

    private func changed(_ profile: GCPhysicalInputProfile, _ element: GCControllerElement) {
        if let target = learning.get() {
            learn(target, profile, element)
            return
        }
        let map = mapping.get()
        updateAxes(profile, map)
        updateButtons(profile, map)
    }

    private func updateAxes(_ profile: GCPhysicalInputProfile, _ map: GamepadMapping) {
        let yawBinding = map.axes[.gimbalYaw]
        let pitchBinding = map.axes[.gimbalPitch]
        let yaw = yawBinding.map { value(of: $0, in: profile) } ?? 0
        let pitch = pitchBinding.map { value(of: $0, in: profile) } ?? 0
        let rates = shaper.get().rates(
            x: yaw, y: pitch, zoom: zoomLevel.get(),
            digitalX: yawBinding.map { !isAnalog($0, in: profile) } ?? false,
            digitalY: pitchBinding.map { !isAnalog($0, in: profile) } ?? false)
        if rates != lastRates {
            lastRates = rates
            onGimbalRate?(rates.pitch, rates.yaw)
        }
        // Unbound means centred, so a zoom running from an axis that was just cleared stops.
        zoomAxisChanged(map.axes[.zoom].map { value(of: $0, in: profile) } ?? 0)
    }

    private func value(of binding: GamepadAxisBinding, in profile: GCPhysicalInputProfile) -> Float {
        let source = binding.source
        let raw: Float = switch source.component {
        case .x: profile.dpads[source.element]?.xAxis.value ?? 0
        case .y: profile.dpads[source.element]?.yAxis.value ?? 0
        case .axis: profile.axes[source.element]?.value ?? 0
        case .button: profile.buttons[source.element]?.value ?? 0
        }
        return binding.inverted ? -raw : raw
    }

    /// A D-pad or plain button is on or off; it gets the digital speed instead of the curve.
    private func isAnalog(_ binding: GamepadAxisBinding, in profile: GCPhysicalInputProfile) -> Bool {
        let source = binding.source
        return switch source.component {
        case .x: profile.dpads[source.element]?.xAxis.isAnalog ?? true
        case .y: profile.dpads[source.element]?.yAxis.isAnalog ?? true
        case .axis: profile.axes[source.element]?.isAnalog ?? true
        case .button: profile.buttons[source.element]?.isAnalog ?? true
        }
    }

    private func updateButtons(_ profile: GCPhysicalInputProfile, _ map: GamepadMapping) {
        for (element, action) in map.buttons {
            let isPressed = profile.buttons[element]?.isPressed ?? false
            let wasPressed = pressed.contains(element)
            guard isPressed != wasPressed else { continue }
            if isPressed {
                pressed.insert(element)
            } else {
                pressed.remove(element)
            }
            switch action {
            case .zoomIn, .zoomOut:
                zoomButton(action == .zoomIn ? 1 : -1, pressed: isPressed)
            default:
                if isPressed {
                    DispatchQueue.main.async { [weak self] in self?.onAction?(action) }
                }
            }
        }
    }

    private func zoomButton(_ direction: Int32, pressed: Bool) {
        if pressed {
            zoomButtonDirection = direction
            setZoom(direction)
        } else if zoomButtonDirection == direction {
            zoomButtonDirection = 0
            setZoom(0)
        }
    }

    private func zoomAxisChanged(_ value: Float) {
        guard zoomButtonDirection == 0 else { return } // a held zoom button wins
        // Hysteresis: start at 0.5, stop below 0.3, so the zoom does not chatter.
        var direction = zoomDirection
        if value > 0.5 {
            direction = 1
        } else if value < -0.5 {
            direction = -1
        } else if abs(value) < 0.3 {
            direction = 0
        }
        setZoom(direction)
    }

    private func setZoom(_ direction: Int32) {
        if direction != zoomDirection {
            zoomDirection = direction
            onZoom?(direction)
        }
    }

    // MARK: Learning (gamepad queue)

    private func learn(_ target: GamepadLearnTarget, _ profile: GCPhysicalInputProfile,
                       _ element: GCControllerElement) {
        guard let name = profile.elements.first(where: { $0.value === element })?.key,
              let learned = Self.learned(target, name, element)
        else { return }
        learning.set(nil)
        DispatchQueue.main.async { [weak self] in self?.onLearned?(target, learned) }
    }

    private static func learned(_ target: GamepadLearnTarget, _ name: String,
                                _ element: GCControllerElement) -> Learned? {
        switch target {
        case .action:
            if let button = element as? GCControllerButtonInput, button.isPressed {
                return .button(name)
            }
        case .axis:
            if let pad = element as? GCControllerDirectionPad {
                let x = pad.xAxis.value
                let y = pad.yAxis.value
                guard max(abs(x), abs(y)) >= learnThreshold else { return nil }
                return .axis(GamepadAxisSource(element: name, component: abs(x) > abs(y) ? .x : .y))
            }
            if let axis = element as? GCControllerAxisInput, abs(axis.value) >= learnThreshold {
                return .axis(GamepadAxisSource(element: name, component: .axis))
            }
            if let button = element as? GCControllerButtonInput, button.isAnalog,
               button.value >= learnThreshold {
                return .axis(GamepadAxisSource(element: name, component: .button))
            }
        }
        return nil
    }

    // MARK: Disconnect

    private func controllerDisconnected() {
        stopAll() // never leave the gimbal or the zoom moving
        let remaining = GCController.controllers().first
        onConnectionChange?(remaining.map(Self.name(of:)))
    }
}
