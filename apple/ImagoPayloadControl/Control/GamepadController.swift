import Foundation
import GameController

/// Physical controllers (Xbox, PlayStation, MFi).
///
/// Stick input is handled on a dedicated high-priority queue and sent straight to the core,
/// never through the main thread. Button presses are forwarded to the main queue.
///
/// Mapping:
/// - Left stick: gimbal pitch and yaw
/// - Right stick up/down: zoom in/out
/// - A: photo, or start/stop recording in video mode
/// - B: start/stop recording
/// - X: stop tracking
/// - Y: AI detection on/off
/// - L1 / R1: previous / next view (EO, IR, ...)
/// - Menu: recenter the gimbal
final class GamepadController {
    enum Button {
        case shutter, record, stopTracking, toggleDetection, previousView, nextView, recenter
    }

    /// Called from the gamepad queue with gimbal rates in degrees per second.
    var onGimbalRate: ((Float, Float) -> Void)?
    /// Called from the gamepad queue: -1 zoom out, 0 stop, 1 zoom in.
    var onZoom: ((Int32) -> Void)?
    /// Called on the main queue.
    var onButton: ((Button) -> Void)?
    /// Called on the main queue with the controller name, or nil when none is connected.
    var onConnectionChange: ((String?) -> Void)?

    /// Latest EO zoom, used to scale the gimbal rate.
    let zoomLevel = Locked<Float>(1)
    var shaper = GimbalInputShaper()

    private let queue = DispatchQueue(label: "aero.imago.gamepad", qos: .userInteractive)
    private var zoomDirection: Int32 = 0 // gamepad queue only
    private var observers: [NSObjectProtocol] = []
    /// iOS also announces controllers that were paired at launch; attach each one once.
    private var attached = Set<ObjectIdentifier>()

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
        guard let pad = controller.extendedGamepad,
              attached.insert(ObjectIdentifier(controller)).inserted
        else { return }
        controller.handlerQueue = queue

        pad.leftThumbstick.valueChangedHandler = { [weak self] _, x, y in
            guard let self else { return }
            let rates = self.shaper.rates(x: x, y: y, zoom: self.zoomLevel.get())
            self.onGimbalRate?(rates.pitch, rates.yaw)
        }
        pad.rightThumbstick.yAxis.valueChangedHandler = { [weak self] _, value in
            self?.zoomAxisChanged(value)
        }
        bind(pad.buttonA, to: .shutter)
        bind(pad.buttonB, to: .record)
        bind(pad.buttonX, to: .stopTracking)
        bind(pad.buttonY, to: .toggleDetection)
        bind(pad.leftShoulder, to: .previousView)
        bind(pad.rightShoulder, to: .nextView)
        bind(pad.buttonMenu, to: .recenter)

        let name = controller.vendorName ?? "Gamepad"
        DispatchQueue.main.async { [weak self] in self?.onConnectionChange?(name) }
    }

    private func bind(_ button: GCControllerButtonInput, to action: Button) {
        button.pressedChangedHandler = { [weak self] _, _, pressed in
            guard pressed else { return }
            DispatchQueue.main.async { self?.onButton?(action) }
        }
    }

    private func zoomAxisChanged(_ value: Float) {
        // Hysteresis: start at 0.5, stop below 0.3, so the zoom does not chatter.
        var direction = zoomDirection
        if value > 0.5 {
            direction = 1
        } else if value < -0.5 {
            direction = -1
        } else if abs(value) < 0.3 {
            direction = 0
        }
        if direction != zoomDirection {
            zoomDirection = direction
            onZoom?(direction)
        }
    }

    private func controllerDisconnected() {
        // Never leave the gimbal or the zoom moving.
        queue.async { [weak self] in
            guard let self else { return }
            self.onGimbalRate?(0, 0)
            if self.zoomDirection != 0 {
                self.zoomDirection = 0
                self.onZoom?(0)
            }
        }
        let remaining = GCController.controllers().first { $0.extendedGamepad != nil }
        onConnectionChange?(remaining.map { $0.vendorName ?? "Gamepad" })
    }
}
