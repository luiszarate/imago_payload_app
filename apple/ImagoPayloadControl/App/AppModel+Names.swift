import Foundation

/// Names of commands and settings for the notices.
extension AppModel {
    static func commandName(_ command: UInt32) -> String {
        switch command {
        case 530: return "Camera mode"
        case 531: return "Zoom"
        case 532: return "Focus"
        case 2000: return "Photo"
        case 2500: return "Start recording"
        case 2501: return "Stop recording"
        case 31013: return "Tracking"
        default: return "Command \(command)"
        }
    }

    static func paramName(_ id: String) -> String {
        switch id {
        case "GB_MODE": return "Gimbal mode"
        case "C_SOURCE": return "View"
        case "TRACK_MODE": return "AI detection"
        case "RC_MODE": return "RC mode"
        case "C_T_G": return "IR gain"
        case "LRF_MODE": return "Range finder"
        default: return id.hasPrefix("C_T_ISO") || id.hasPrefix("C_T_RG") ? "Isotherm" : id
        }
    }
}
