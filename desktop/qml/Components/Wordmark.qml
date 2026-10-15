import QtQuick
import QtQuick.Shapes
import Imago

// "IMAGO" wordmark in the brand gradient, until the official logo is added.
Item {
    id: root

    property int size: 17
    readonly property font wordFont: AppFont.display(size, 700, size * 0.22)

    implicitWidth: metrics.advanceWidth
    implicitHeight: metrics.height
    Accessible.role: Accessible.StaticText
    Accessible.name: "Imago Aerospace"

    TextMetrics {
        id: metrics
        font: root.wordFont
        text: "IMAGO"
    }

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: "transparent"
            fillGradient: LinearGradient {
                x1: 0; y1: 0
                x2: root.width; y2: root.height
                GradientStop { position: 0.0; color: Theme.magenta }
                GradientStop { position: 0.5; color: Theme.fuchsia }
                GradientStop { position: 1.0; color: Theme.purple }
            }
            PathText {
                x: 0
                y: 0
                font: root.wordFont
                text: "IMAGO"
            }
        }
    }
}
