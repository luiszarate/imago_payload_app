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
        if setting.id == CameraSettingsCatalog.rangeFinderID {
            setRangeFinderMode(value)
            return
        }
        register(session.setCameraParam(setting.id, value), setting.title)
    }

    /// The range finder's rate goes as a command, not as a setting. Shown as asked at once:
    /// through Herelink the acknowledgement often never comes though the rate changed
    /// (2026-11-13). A read 1.5 s later puts what the payload holds in its place.
    private func setRangeFinderMode(_ mode: UInt32) {
        guard let session else { return }
        let id = session.setRangeFinderMode(mode)
        register(id, "Range finder")
        guard id != 0 else { return }
        cameraValues[CameraSettingsCatalog.rangeFinderID] = mode
        rangeFinderReadBack?.cancel()
        rangeFinderReadBack = Task { [weak self, weak session] in
            try? await Task.sleep(for: .seconds(1.5))
            guard !Task.isCancelled, let session, self?.session === session else { return }
            session.readCameraParam(CameraSettingsCatalog.rangeFinderID)
        }
    }

    /// The payload's own stream, for the camera chosen in Stream settings. Lower bitrates survive
    /// a weak link with fewer lost packets (frozen picture).
    func setStreamBitrate(_ bitsPerSecond: UInt32) {
        guard let session else { return }
        Haptics.tap()
        let camera = streamRequests.camera
        let id = session.setStreamBitrate(camera, bitsPerSecond: bitsPerSecond)
        register(id, "Stream bitrate")
        if id != 0 {
            streamRequests.bitrates[camera] = bitsPerSecond
        }
    }

    /// A resolution level of `StreamCamera.sizes`, for the camera chosen in Stream settings.
    func setStreamResolution(_ level: UInt32) {
        guard let session else { return }
        Haptics.tap()
        let camera = streamRequests.camera
        let id = session.setStreamResolution(camera, level: level)
        register(id, "Stream picture size")
        if id != 0 {
            streamRequests.levels[camera] = level
        }
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
