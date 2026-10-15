pragma Singleton
import QtQuick

// Space Grotesk for titles, labels and numbers; Manrope for body text. Sizes in pixels, which
// Qt scales with the display (like dp on Android).
QtObject {
    readonly property string displayFamily: "Space Grotesk"
    readonly property string bodyFamily: "Manrope"
    readonly property string iconFamily: "Material Icons"

    function display(size, weight, spacing) {
        return Qt.font({ family: displayFamily, pixelSize: size, weight: weight === undefined ? 600 : weight,
                         letterSpacing: spacing === undefined ? 0 : spacing })
    }

    function body(size, weight) {
        return Qt.font({ family: bodyFamily, pixelSize: size, weight: weight === undefined ? 500 : weight })
    }

    // Tabular figures, for telemetry that changes while you read it.
    function figures(size, weight) {
        return Qt.font({ family: displayFamily, pixelSize: size, weight: weight === undefined ? 500 : weight,
                         features: { "tnum": 1 } })
    }
}
