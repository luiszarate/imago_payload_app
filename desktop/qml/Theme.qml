pragma Singleton
import QtQuick

// Imago Aerospace visual identity (www.imago.aero): near-black background, magenta-to-purple
// accent, Space Grotesk for display text and figures, Manrope for body text.
QtObject {
    readonly property color background: "#05060D"
    readonly property color surface: "#0A0B10"
    readonly property color surfaceRaised: "#14151C"
    readonly property color text: "#F5F6F8"
    readonly property color textSecondary: alpha(text, 0.62)
    readonly property color textTertiary: alpha(text, 0.38)
    readonly property color stroke: Qt.rgba(1, 1, 1, 0.10)

    readonly property color magenta: "#EE49A7"
    readonly property color fuchsia: "#C2188F"
    readonly property color purple: "#7E2D91"

    // Status colours. Red is reserved for recording and errors.
    readonly property color danger: "#EF4444"
    readonly property color ok: "#34D399"
    readonly property color warning: "#FBBF24"

    // Colours for detection classes: distinct hues that stay readable on video.
    readonly property var classColors: ["#38BDF8", "#FBBF24", "#34D399", "#F472B6", "#A78BFA", "#FB923C",
                                        "#22D3EE", "#A3E635"]

    // The brand gradient, for active controls.
    readonly property Gradient accent: Gradient {
        orientation: Gradient.Horizontal
        GradientStop { position: 0.0; color: "#EE49A7" }
        GradientStop { position: 0.5; color: "#C2188F" }
        GradientStop { position: 1.0; color: "#7E2D91" }
    }

    function alpha(c, a) {
        return Qt.rgba(c.r, c.g, c.b, a)
    }
}
