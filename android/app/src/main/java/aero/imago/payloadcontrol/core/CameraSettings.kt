package aero.imago.payloadcontrol.core

/**
 * A setting the payload keeps, written with PARAM_EXT_SET and read with PARAM_EXT_REQUEST_READ
 * (values in docs/protocol/orus-l.md).
 */
data class CameraSetting(
    /** The param_id. */
    val id: String,
    val title: String,
    val kind: Kind,
    val note: String? = null,
) {
    data class Option(val value: Int, val label: String)

    sealed interface Kind {
        /** Shown as a switch. */
        data class Toggle(val on: Int, val off: Int) : Kind
        /** Shown as a menu. */
        data class Choice(val options: List<Option>) : Kind
    }

    val options: List<Option>
        get() = when (kind) {
            is Kind.Toggle -> listOf(Option(kind.on, "On"), Option(kind.off, "Off"))
            is Kind.Choice -> kind.options
        }

    fun label(value: Int): String = options.firstOrNull { it.value == value }?.label ?: "$value"

    companion object {
        /** Most Orus L switches use 2 = on, 3 = off. */
        fun toggle(id: String, title: String, on: Int = 2, off: Int = 3, note: String? = null) =
            CameraSetting(id, title, Kind.Toggle(on, off), note)

        fun choice(id: String, title: String, vararg options: Pair<Int, String>, note: String? = null) =
            CameraSetting(id, title, Kind.Choice(options.map { Option(it.first, it.second) }), note)

        /** value 0 until count shown as labels from `label`. */
        fun steps(id: String, title: String, count: Int, note: String? = null, label: (Int) -> String) =
            CameraSetting(id, title, Kind.Choice((0 until count).map { Option(it, label(it)) }), note)
    }
}

data class CameraSettingSection(
    val id: String,
    val title: String,
    val settings: List<CameraSetting>,
    val footer: String? = null,
)

/** The settings the app shows, grouped as on the settings screen. */
object CameraSettingsCatalog {
    private fun toggle(id: String, title: String, on: Int = 2, off: Int = 3, note: String? = null) =
        CameraSetting.toggle(id, title, on, off, note)

    val image = listOf(
        CameraSettingSection("zoom", "Zoom", listOf(
            CameraSetting.choice("C_V_ZM_MODE", "Zoom mode", 0 to "Combined · 300×", 2 to "Super resolution · 25×"),
            CameraSetting.steps("C_V_Z_SPD", "Zoom speed", 8) { "${it + 1}" },
            toggle("C_V_STZOOM", "Stable zoom", on = 1, off = 0),
        ), footer = "Zoom speed applies to the + and − buttons, a pinch and the gamepad."),
        CameraSettingSection("focus", "Focus", listOf(
            CameraSetting.choice("C_V_FM", "Focus mode", 1 to "Auto", 0 to "Manual", 2 to "One-push"),
            CameraSetting.steps("C_V_F_SPD", "Focus speed", 8) { "${it + 1}" },
        )),
        CameraSettingSection("image", "Image", listOf(
            toggle("C_V_DEFOG", "Defog"),
            CameraSetting.steps("C_V_DEFOG_LV", "Defog level", 4) { "${it + 1}" },
            CameraSetting.choice("C_V_EIS", "Stabilization (EIS)", 2 to "On", 3 to "Off", 0 to "Hold"),
            CameraSetting.choice("C_V_EIS_LV", "Stabilization level", 2 to "Super", 3 to "Super+"),
            CameraSetting.choice("C_V_NSREDUCT", "Noise reduction",
                0 to "Off", 1 to "1", 2 to "2", 3 to "3", 4 to "4", 5 to "5"),
            toggle("C_V_FLREDUCT", "Flicker reduction"),
            toggle("C_V_FLIP", "Flip image"),
        )),
        CameraSettingSection("night", "Day / night", listOf(
            CameraSetting.choice("C_V_ICR", "IR-cut filter", 2 to "Auto", 3 to "Manual"),
            CameraSetting.choice("C_V_ICR_MAN", "IR-cut (manual)", 3 to "Day", 2 to "Night",
                note = "Only with the IR-cut filter on manual."),
            CameraSetting.choice("C_F_DEFOG", "Lens heater", 0 to "Auto", 2 to "On", 1 to "Off"),
        )),
        CameraSettingSection("osd", "Overlay", listOf(
            CameraSetting.choice("OSD_MODE", "Payload OSD in the video", 0 to "Off", 2 to "Status", 1 to "Debug"),
        ), footer = "The text the payload draws into the picture itself (and into recordings)."),
    )

    val infrared = listOf(
        CameraSettingSection("ir", "Thermal", listOf(
            CameraSetting.steps("C_T_PALETTE", "Palette", IRPalette.COUNT) { IRPalette.name(it) },
            CameraSetting.steps("C_T_ZOOM", "Digital zoom", 8) { "${it + 1}×" },
        ), footer = "Also on the thermal bar under the video while the view is IR or IR + EO."),
    )

    val gimbal = listOf(
        CameraSettingSection("gimbal", "Gimbal", listOf(
            CameraSetting.choice("GB_MODE", "Mode", 1 to "Lock", 2 to "Follow", 3 to "Mapping", 0 to "Off"),
            CameraSetting.choice("RC_MODE", "RC input", 0 to "Gremsy", 1 to "Standard",
                note = "How the pilot's RC drives the gimbal."),
        )),
    )

    /** Stream bitrates offered, bit/s. */
    val bitrates = listOf(1, 2, 3, 4, 6, 8).map { it * 1_000_000 }
}
