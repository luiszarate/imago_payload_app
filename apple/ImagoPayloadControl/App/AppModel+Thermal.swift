import Foundation
import ImagoCore

/// A temperature band for the isotherm, whole °C.
struct IsothermBand: Equatable, Identifiable {
    let name: String
    let range: ClosedRange<Int>
    var id: String { name }

    /// From Gremsy's own examples (people) and common thermal practice (hot spots, fire). Fire
    /// goes past 150 °C, so the camera drops to low gain for it.
    static let presets = [
        IsothermBand(name: "People", range: 35...40),
        IsothermBand(name: "Hot", range: 100...150),
        IsothermBand(name: "Fire", range: 150...550),
    ]

    /// The camera measures 0...550 °C in our Celsius settings.
    static let limits = 0...Int(IMAGO_IR_GAIN_LOW_MAX_C)
    static let highGainMax = Int(IMAGO_IR_GAIN_HIGH_MAX_C)
}

/// The thermal bar: IR palette, zoom, flat-field correction, gain and isotherm.
extension AppModel {
    private static let isothermLowKey = "thermal.isothermLow"
    private static let isothermHighKey = "thermal.isothermHigh"

    /// The bar shows while the IR picture is the main one.
    var showsThermalBar: Bool {
        isConnected && (camera.view == .ir || camera.view == .irEO)
    }

    /// Palette on the camera: what it last confirmed, else its data stream.
    var irPalette: Int? {
        cameraValues["C_T_PALETTE"].map(Int.init) ?? thermal.palette
    }

    /// IR digital zoom, 1...8×.
    var irZoom: Int? {
        cameraValues["C_T_ZOOM"].map { Int($0) + 1 } ?? thermal.zoom
    }

    /// The camera answered a read of its isotherm switch (Gremsy's F1 thermal core has it).
    var isothermSupported: Bool { cameraValues["C_T_ISO_M"] != nil }
    var isothermOn: Bool { cameraValues["C_T_ISO_M"] == 1 }

    /// The band on the camera when it is on and known, else the last one applied here.
    var isothermBand: ClosedRange<Int> {
        if isothermOn, let low = cameraValues["C_T_RG0_TMP"], let high = cameraValues["C_T_RG1_TMP"],
           low < high, Int(high) <= IsothermBand.limits.upperBound {
            return Int(low)...Int(high)
        }
        let defaults = UserDefaults.standard
        let low = defaults.object(forKey: Self.isothermLowKey) as? Int ?? 35
        let high = defaults.object(forKey: Self.isothermHighKey) as? Int ?? 40
        return low < high ? low...high : 35...40
    }

    /// Low gain measures up to 550 °C, high gain up to 150 °C. Nil when the camera has no gain
    /// setting.
    var irLowGain: Bool? { cameraValues["C_T_G"].map { $0 == UInt32(IMAGO_IR_GAIN_LOW) } }

    /// Called on every poll: once the view turns IR, asks for the settings the bar shows (the
    /// isotherm and gain ones go unanswered on a camera without them), and follows changes the
    /// data stream reports, so a palette changed elsewhere shows here too.
    func updateThermal(_ status: ThermalStatus) {
        let previous = thermal
        if previous != status {
            thermal = status
            if let palette = status.palette, palette != previous.palette {
                cameraValues["C_T_PALETTE"] = UInt32(palette)
            }
            if let zoom = status.zoom, zoom != previous.zoom, zoom >= 1 {
                cameraValues["C_T_ZOOM"] = UInt32(zoom - 1)
            }
        }
        if showsThermalBar, !thermalSettingsRead, let session {
            thermalSettingsRead = true
            for id in ["C_T_PALETTE", "C_T_ZOOM", "C_T_ISO_M", "C_T_G", "C_T_RG0_TMP", "C_T_RG1_TMP"] {
                session.readCameraParam(id)
            }
        } else if !showsThermalBar {
            thermalSettingsRead = false
        }
    }

    func setIRPalette(_ index: Int) {
        guard let session else { return }
        Haptics.tap()
        register(session.setCameraParam("C_T_PALETTE", UInt32(index)), "Palette")
    }

    /// One step of IR digital zoom (1...8×).
    func stepIRZoom(_ step: Int) {
        setIRZoom((irZoom ?? 1) + step)
    }

    func setIRZoom(_ zoom: Int) {
        guard let session else { return }
        let next = min(max(zoom, 1), 8)
        guard next != irZoom else { return }
        Haptics.tap()
        register(session.setCameraParam("C_T_ZOOM", UInt32(next - 1)), "IR zoom")
    }

    func setIRLowGain(_ low: Bool) {
        guard let session else { return }
        Haptics.tap()
        register(session.setCameraParam("C_T_G", UInt32(low ? IMAGO_IR_GAIN_LOW : IMAGO_IR_GAIN_HIGH)),
                 "IR gain")
    }

    /// Colours the band over the palette; above 150 °C the core also sets low gain.
    func applyIsotherm(_ band: ClosedRange<Int>) {
        guard let session else { return }
        Haptics.tap()
        UserDefaults.standard.set(band.lowerBound, forKey: Self.isothermLowKey)
        UserDefaults.standard.set(band.upperBound, forKey: Self.isothermHighKey)
        register(session.setIsotherm(band), "Isotherm")
    }

    func disableIsotherm() {
        guard let session else { return }
        Haptics.tap()
        register(session.setIsotherm(nil), "Isotherm off")
    }
}

/// The isotherm on the payload's web settings page: our Orus L has it only there (field check
/// 2026-10-16), as one threshold. Saving applies it at once (2026-10-19), whatever the page says
/// about a restart.
struct WebIsotherm: Equatable {
    var enabled: Bool
    var thresholdC: Int
    /// 0: -20...150 °C, 1: -20...550 °C (the IR gain, as far as the page tells).
    var measurementRange: Int

    static let thresholdLimits = 0...550
}

enum WebRequest {
    case read, save, restartService
}

extension AppModel {
    func readWebIsotherm() {
        guard let session, !webBusy else { return }
        startWebRequest(session.readWebIRSettings(), .read)
    }

    func saveWebIsotherm(_ isotherm: WebIsotherm) {
        guard let session, !webBusy else { return }
        Haptics.tap()
        startWebRequest(session.setWebIsotherm(enabled: isotherm.enabled, thresholdC: isotherm.thresholdC,
                                               measurementRange: isotherm.measurementRange), .save)
    }

    /// The tracker that stops obeying in long sessions (2026-10-15, again 2026-10-19) may come
    /// back with a restart of the payload's camera service *(unverified)*.
    func restartPayloadService() {
        guard let session, !webBusy else { return }
        Haptics.tap()
        startWebRequest(session.restartPayloadService(), .restartService)
    }

    private func startWebRequest(_ id: UInt32, _ kind: WebRequest) {
        guard id != 0 else {
            show("The payload's web page needs the payload IP in the connection profile", .error)
            return
        }
        webRequests[id] = kind
        webBusy = true
    }

    func webRequestEnded(_ id: UInt32, status: UInt32, result: Int32, error: String) {
        guard let kind = webRequests.removeValue(forKey: id) else { return }
        webBusy = !webRequests.isEmpty
        if result == IMAGO_RESULT_ACCEPTED, kind != .restartService, let session {
            let s = session.webIRSettings()
            if s.valid {
                webIsotherm = WebIsotherm(enabled: s.isotherm_enabled, thresholdC: Int(s.isotherm_threshold_c),
                                          measurementRange: Int(s.measurement_range))
            }
        }
        switch (kind, result) {
        case (.save, IMAGO_RESULT_ACCEPTED):
            show("Isotherm saved", .info)
        case (.restartService, IMAGO_RESULT_ACCEPTED), (.restartService, IMAGO_RESULT_TIMEOUT):
            // The service may go down before the page answers.
            show("Camera service restarting. Video and control come back by themselves.", .info)
        case (.read, IMAGO_RESULT_ACCEPTED):
            break
        default:
            Haptics.error()
            let what = kind == .read ? "Reading" : (kind == .save ? "Saving" : "Restarting")
            show("\(what) the payload's web settings failed: \(error.isEmpty ? "code \(status)" : error)", .error)
        }
    }
}
