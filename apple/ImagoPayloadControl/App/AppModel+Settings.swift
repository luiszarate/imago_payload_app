import Foundation

/// Camera, gimbal and controller settings.
extension AppModel {
    // MARK: Payload settings

    /// Asks the payload for the current value of each setting; they arrive in `cameraValues`.
    /// onlyMissing: ask again only for those that have not answered.
    func readSettings(_ sections: [CameraSettingSection], onlyMissing: Bool = false) {
        guard let session else { return }
        for setting in sections.flatMap(\.settings)
        where !onlyMissing || cameraValues[setting.id] == nil {
            session.readCameraParam(setting.id)
        }
    }

    /// The payload confirms the new value, which then shows up in `cameraValues`.
    func set(_ setting: CameraSetting, to value: UInt32) {
        guard let session else { return }
        Haptics.tap()
        register(session.setCameraParam(setting.id, value), setting.title)
    }

    /// Lower bitrates survive a weak link with fewer lost packets (frozen picture).
    func setStreamBitrate(_ bitsPerSecond: UInt32) {
        guard let session else { return }
        Haptics.tap()
        register(session.setStreamBitrate(cameraID: 0, bitsPerSecond: bitsPerSecond), "Stream bitrate")
    }

    // MARK: Controls

    /// Waits for the next button press or axis movement on the controller; nil cancels.
    func learnGamepadInput(_ target: GamepadLearnTarget?) {
        gamepadLearning = target
        gamepad.setLearning(target)
    }

    func gamepadLearned(_ target: GamepadLearnTarget, _ learned: GamepadController.Learned) {
        // Cancelled, or another assignment started, while this input was on its way.
        guard gamepadLearning == target else { return }
        switch (target, learned) {
        case let (.action(action), .button(element)):
            gamepadMapping.bind(action, to: element)
        case let (.axis(function), .axis(source)):
            gamepadMapping.bind(function, to: source)
        default:
            break
        }
        gamepadLearning = nil
        Haptics.tap()
    }

    func setGamepadAxis(_ function: GamepadAxisFunction, inverted: Bool) {
        gamepadMapping.axes[function]?.inverted = inverted
    }
}
