import Foundation

/// A setting the payload keeps, written with PARAM_EXT_SET and read with PARAM_EXT_REQUEST_READ
/// (values in docs/protocol/orus-l.md).
struct CameraSetting: Identifiable {
    struct Option: Hashable {
        let value: UInt32
        let label: String
    }

    enum Kind {
        /// Shown as a switch.
        case toggle(on: UInt32, off: UInt32)
        /// Shown as a menu, or as segments when there are few short options.
        case choice([Option])
    }

    /// The param_id.
    let id: String
    let title: String
    let kind: Kind
    var note: String?

    var options: [Option] {
        switch kind {
        case let .toggle(on, off): [Option(value: on, label: "On"), Option(value: off, label: "Off")]
        case let .choice(options): options
        }
    }

    func label(for value: UInt32) -> String {
        options.first { $0.value == value }?.label ?? "\(value)"
    }

    /// Most Orus L switches use 2 = on, 3 = off.
    static func toggle(_ id: String, _ title: String, on: UInt32 = 2, off: UInt32 = 3,
                       note: String? = nil) -> CameraSetting {
        CameraSetting(id: id, title: title, kind: .toggle(on: on, off: off), note: note)
    }

    static func choice(_ id: String, _ title: String, _ options: [(UInt32, String)],
                       note: String? = nil) -> CameraSetting {
        CameraSetting(id: id, title: title,
                      kind: .choice(options.map { Option(value: $0.0, label: $0.1) }), note: note)
    }

    /// value 0..<count shown as labels from `label`.
    static func steps(_ id: String, _ title: String, count: UInt32, note: String? = nil,
                      label: (UInt32) -> String) -> CameraSetting {
        choice(id, title, (0..<count).map { ($0, label($0)) }, note: note)
    }
}

struct CameraSettingSection: Identifiable {
    let id: String
    let title: String
    let settings: [CameraSetting]
    var footer: String?
}

/// The settings the app shows, grouped as on the settings screen.
enum CameraSettingsCatalog {
    static let image: [CameraSettingSection] = [
        CameraSettingSection(id: "zoom", title: "Zoom", settings: [
            .choice("C_V_ZM_MODE", "Zoom mode", [(0, "Combined · 300×"), (2, "Super resolution · 25×")]),
            .steps("C_V_Z_SPD", "Zoom speed", count: 8) { "\($0 + 1)" },
            .toggle("C_V_STZOOM", "Stable zoom", on: 1, off: 0),
        ], footer: "Zoom speed applies to the + and − buttons, a pinch and the gamepad."),
        CameraSettingSection(id: "focus", title: "Focus", settings: [
            .choice("C_V_FM", "Focus mode", [(1, "Auto"), (0, "Manual"), (2, "One-push")]),
            .steps("C_V_F_SPD", "Focus speed", count: 8) { "\($0 + 1)" },
        ]),
        CameraSettingSection(id: "image", title: "Image", settings: [
            .toggle("C_V_DEFOG", "Defog"),
            .steps("C_V_DEFOG_LV", "Defog level", count: 4) { "\($0 + 1)" },
            .choice("C_V_EIS", "Stabilization (EIS)", [(2, "On"), (3, "Off"), (0, "Hold")]),
            .choice("C_V_EIS_LV", "Stabilization level", [(2, "Super"), (3, "Super+")]),
            .choice("C_V_NSREDUCT", "Noise reduction",
                    [(0, "Off"), (1, "1"), (2, "2"), (3, "3"), (4, "4"), (5, "5")]),
            .toggle("C_V_FLREDUCT", "Flicker reduction"),
            .toggle("C_V_FLIP", "Flip image"),
        ]),
        CameraSettingSection(id: "night", title: "Day / night", settings: [
            .choice("C_V_ICR", "IR-cut filter", [(2, "Auto"), (3, "Manual")]),
            .choice("C_V_ICR_MAN", "IR-cut (manual)", [(3, "Day"), (2, "Night")],
                    note: "Only with the IR-cut filter on manual."),
            .choice("C_F_DEFOG", "Lens heater", [(0, "Auto"), (2, "On"), (1, "Off")]),
        ]),
        CameraSettingSection(id: "osd", title: "Overlay", settings: [
            .choice("OSD_MODE", "Payload OSD in the video", [(0, "Off"), (2, "Status"), (1, "Debug")]),
        ], footer: "The text the payload draws into the picture itself (and into recordings)."),
    ]

    static let infrared: [CameraSettingSection] = [
        CameraSettingSection(id: "ir", title: "Thermal", settings: [
            .steps("C_T_PALETTE", "Palette", count: 10) { "\($0 + 1)" },
            .steps("C_T_ZOOM", "Digital zoom", count: 8) { "\($0 + 1)×" },
        ], footer: "Palette numbers follow the payload; their names are not documented yet."),
    ]

    static let gimbal: [CameraSettingSection] = [
        CameraSettingSection(id: "gimbal", title: "Gimbal", settings: [
            .choice("GB_MODE", "Mode", [(1, "Lock"), (2, "Follow"), (3, "Mapping"), (0, "Off")]),
            .choice("RC_MODE", "RC input", [(0, "Gremsy"), (1, "Standard")],
                    note: "How the pilot's RC drives the gimbal."),
        ]),
    ]

    static var all: [CameraSettingSection] { image + infrared + gimbal }

    /// Stream bitrates offered, bit/s.
    static let bitrates: [UInt32] = [1, 2, 3, 4, 6, 8].map { $0 * 1_000_000 }
}
