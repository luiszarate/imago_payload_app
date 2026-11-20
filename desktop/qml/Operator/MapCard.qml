import QtQuick
import Imago

// The map in a large window: a twin of the attitude indicator's card beside it, of one size, on
// one baseline, with the same padding, corners and readout row (north up and the aircraft's
// heading, which the corner map draws on the map itself). A map of its own size next to the
// indicator looked out of line on the iPad (operator, 2026-11-19). Put away, only the map's
// faint button is left. The mouse wheel over the map still zooms it.
Item {
    id: root

    /// The attitude indicator, for its measures; its size too while it shows (twinned).
    required property AttitudeIndicator indicator
    property bool twinned: true
    readonly property bool shown: App.map.visible

    readonly property real cardWidth: twinned ? indicator.width : indicator.dialSide + indicator.horizontalPadding * 2
    readonly property real cardHeight: twinned ? indicator.height
                                               : indicator.dialSide + readouts.implicitHeight + indicator.rowSpacing
                                                 + indicator.verticalPadding * 2

    implicitWidth: shown ? cardWidth : map.implicitWidth
    implicitHeight: shown ? cardHeight : map.implicitHeight

    Panel {
        visible: root.shown
        anchors.fill: parent
        radius: root.indicator.radius
    }

    Column {
        anchors.centerIn: parent
        spacing: root.indicator.rowSpacing

        MapView {
            id: map
            anchors.horizontalCenter: parent.horizontalCenter
            mapWidth: Math.max(root.cardWidth - root.indicator.horizontalPadding * 2, root.indicator.dialSide)
            mapHeight: root.indicator.dialSide
            legend: false
            radius: 10
        }
        Row {
            id: readouts
            visible: root.shown
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 14
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "N ↑"
                color: Theme.magenta
                font: AppFont.display(10, 700, 1)
            }
            InstrumentReadout {
                label: "HDG"
                value: App.vehicleHeading >= 0 ? String(Math.round(App.vehicleHeading) % 360).padStart(3, "0") + "°" : "—"
            }
        }
    }
}
