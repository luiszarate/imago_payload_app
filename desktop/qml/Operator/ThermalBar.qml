import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Imago

// Thermal controls at the bottom right while the IR picture is the main one (view IR or IR + EO):
// palette, the picture's temperature scale, IR zoom and, when the camera has it, the isotherm.
// No flat-field correction: the camera does it on its own, and the operator rarely needs it
// (decided 2026-10-16). It sits beside the attitude indicator, which stays in the centre, and
// gives up detail as the room shrinks, as the iOS bar does: the palette by name, then as its
// swatch, then zoom and isotherm folded into one "IR" menu, then without the scale.
Item {
    id: root

    property bool compact: false
    readonly property real buttonHeight: compact ? 42 : 52

    // Widths of the parts, to pick the most detailed layout that fits.
    readonly property real spacing: compact ? 6 : 8
    readonly property real padding: compact ? 5 : 8
    readonly property real namedPaletteWidth: compact ? 118 : 150
    readonly property real swatchWidth: compact ? 44 : 50
    readonly property real zoomWidth: (compact ? 34 : 42) * 2 + (compact ? 28 : 34)
    readonly property real buttonWidth: compact ? 50 : 60
    readonly property real isothermWidth: App.isothermSupported ? buttonWidth + spacing : 0
    function needs(paletteWidth, scaleWidth, tail) {
        return padding * 2 + paletteWidth + (scaleWidth > 0 ? scaleWidth + spacing : 0) + spacing + tail
    }
    /// 0 palette by name, 1 palette as its swatch, 2 zoom and isotherm in the "IR" menu,
    /// 3 without the scale.
    readonly property int detail:
        width >= needs(namedPaletteWidth, compact ? 90 : 140, zoomWidth + isothermWidth) ? 0
        : width >= needs(swatchWidth, compact ? 80 : 100, zoomWidth + isothermWidth) ? 1
        : width >= needs(swatchWidth, compact ? 80 : 100, buttonWidth) ? 2 : 3

    implicitHeight: compact ? 52 : 68

    // ISO and IR: icon, title and a state line.
    component ThermalButton: Rectangle {
        id: button
        property string icon
        property string title
        property string detail
        property bool active: false
        signal clicked(int button)

        Layout.preferredWidth: root.compact ? 50 : 60
        Layout.preferredHeight: root.buttonHeight
        radius: 12
        color: Theme.surfaceRaised
        gradient: active ? Theme.accent : null
        border.color: buttonMouse.containsMouse && !active ? Theme.stroke : "transparent"
        scale: buttonMouse.pressed ? 0.94 : 1
        Behavior on scale { NumberAnimation { duration: 80 } }
        Accessible.role: Accessible.Button
        Accessible.onPressAction: clicked(Qt.LeftButton)

        Column {
            anchors.centerIn: parent
            spacing: 1
            Icon {
                anchors.horizontalCenter: parent.horizontalCenter
                name: button.icon
                size: root.compact ? 15 : 18
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: button.title
                color: Theme.text
                font: AppFont.display(root.compact ? 9 : 10, 700)
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(implicitWidth, button.width - 4)
                text: button.detail
                color: button.active ? Theme.text : Theme.textSecondary
                font: AppFont.figures(root.compact ? 8 : 9, 600)
                elide: Text.ElideRight
            }
        }
        MouseArea {
            id: buttonMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: mouse => button.clicked(mouse.button)
        }
    }

    component StepButton: Item {
        id: step
        property string icon
        property string description
        signal clicked()

        width: root.compact ? 34 : 42
        height: root.buttonHeight
        scale: stepMouse.pressed ? 0.94 : 1
        Accessible.role: Accessible.Button
        Accessible.name: description
        Accessible.onPressAction: if (enabled) clicked()

        Icon {
            anchors.centerIn: parent
            name: step.icon
            size: root.compact ? 16 : 18
            color: step.enabled ? Theme.text : Theme.textTertiary
        }
        MouseArea {
            id: stepMouse
            anchors.fill: parent
            cursorShape: step.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: step.clicked()
        }
    }

    Panel {
        // Without the scale nothing stretches: the bar keeps to the right.
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: root.detail < 3 ? parent.width : Math.min(parent.width, bar.implicitWidth + root.padding * 2)
        radius: 16

        RowLayout {
            id: bar
            anchors.fill: parent
            anchors.leftMargin: root.padding
            anchors.rightMargin: root.padding
            spacing: root.spacing

            // Palette: a swatch, its name and a menu of all of them.
            Rectangle {
                id: paletteButton
                readonly property string name: App.irPalette >= 0 ? App.irPalettes[App.irPalette].title : "—"
                readonly property bool named: root.detail === 0
                Layout.preferredWidth: named ? root.namedPaletteWidth : root.swatchWidth
                Layout.preferredHeight: root.buttonHeight
                radius: 12
                color: Theme.surfaceRaised
                border.color: paletteMouse.containsMouse ? Theme.stroke : "transparent"
                scale: paletteMouse.pressed ? 0.94 : 1
                Behavior on scale { NumberAnimation { duration: 80 } }
                Accessible.role: Accessible.ButtonMenu
                Accessible.name: "IR palette"
                Accessible.description: App.irPalette >= 0 ? name : "Unknown"
                Accessible.onPressAction: paletteMenu.show()

                Row {
                    visible: paletteButton.named
                    anchors.left: parent.left
                    anchors.right: chevron.left
                    anchors.leftMargin: root.compact ? 8 : 10
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 7

                    PaletteStrip {
                        anchors.verticalCenter: parent.verticalCenter
                        width: root.compact ? 12 : 14
                        height: root.compact ? 26 : 32
                        radius: 4
                        vertical: true
                        colors: App.irPaletteColors
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - x
                        spacing: 1
                        Text {
                            text: "PALETTE"
                            color: Theme.textTertiary
                            font: AppFont.display(root.compact ? 8 : 9, 700, 1)
                        }
                        Text {
                            width: parent.width
                            text: paletteButton.name
                            color: Theme.text
                            font: AppFont.display(root.compact ? 12 : 14, 600)
                            elide: Text.ElideRight
                        }
                    }
                }
                // Without its name the button shows only the palette's colours; the menu names them all.
                PaletteStrip {
                    visible: !paletteButton.named
                    anchors.left: parent.left
                    anchors.leftMargin: root.compact ? 8 : 9
                    anchors.verticalCenter: parent.verticalCenter
                    width: root.compact ? 12 : 14
                    height: root.compact ? 26 : 32
                    radius: 4
                    vertical: true
                    colors: App.irPaletteColors
                }
                Icon {
                    id: chevron
                    anchors.right: parent.right
                    anchors.rightMargin: paletteButton.named ? (root.compact ? 6 : 8) : (root.compact ? 4 : 6)
                    anchors.verticalCenter: parent.verticalCenter
                    name: "unfold_more"
                    size: 16
                    color: Theme.textSecondary
                }
                MouseArea {
                    id: paletteMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: paletteMenu.show()
                }

                CheckMenu {
                    id: paletteMenu
                    options: App.irPalettes
                    current: App.irPalette
                    onChosen: value => App.setIRPalette(value)
                    // Above the bar, which sits at the bottom of the window.
                    function show() { popup(paletteButton, 0, -implicitHeight - 6) }
                }
            }

            TemperatureScale {
                visible: root.detail < 3
                Layout.fillWidth: true
                Layout.minimumWidth: root.detail === 0 ? (root.compact ? 90 : 140) : (root.compact ? 80 : 100)
                Layout.alignment: Qt.AlignVCenter
                compact: root.compact
            }

            // IR digital zoom, 1–8×.
            Rectangle {
                visible: root.detail < 2
                Layout.preferredWidth: zoomRow.implicitWidth
                Layout.preferredHeight: root.buttonHeight
                radius: 12
                color: Theme.surfaceRaised

                Row {
                    id: zoomRow
                    anchors.centerIn: parent
                    readonly property int zoom: App.irZoom > 0 ? App.irZoom : 1
                    StepButton {
                        icon: "remove"
                        description: "IR zoom out"
                        enabled: zoomRow.zoom > 1
                        onClicked: App.stepIRZoom(-1)
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: root.compact ? 28 : 34
                        horizontalAlignment: Text.AlignHCenter
                        text: App.irZoom > 0 ? App.irZoom + "×" : "—"
                        color: Theme.text
                        font: AppFont.figures(root.compact ? 13 : 15, 600)
                        Accessible.role: Accessible.StaticText
                        Accessible.name: "IR zoom " + zoomRow.zoom + " times"
                    }
                    StepButton {
                        icon: "add"
                        description: "IR zoom in"
                        enabled: zoomRow.zoom < App.irZoomMax
                        onClicked: App.stepIRZoom(1)
                    }
                }
            }

            // Isotherm, when the camera has it (it answered a read of C_T_ISO_M).
            ThermalButton {
                id: isothermButton
                visible: App.isothermSupported && root.detail < 2
                icon: "device_thermostat"
                title: "ISO"
                detail: App.isothermOn ? App.isothermLow + "–" + App.isothermHigh + "°" : "OFF"
                active: App.isothermOn
                Accessible.name: "Isotherm"
                Accessible.description: App.isothermOn ? App.isothermLow + " to " + App.isothermHigh + " degrees" : "Off"
                onClicked: isothermPanel.open()

                IsothermPanel {
                    id: isothermPanel
                    x: isothermButton.width - width
                    y: -height - 8
                }
            }

            // Zoom and isotherm when the bar has no room for their buttons.
            ThermalButton {
                id: moreButton
                visible: root.detail >= 2
                icon: "tune"
                title: "IR"
                detail: App.irZoom > 0 ? App.irZoom + "×" : " "
                active: App.isothermOn
                Accessible.role: Accessible.ButtonMenu
                Accessible.name: "Thermal settings"
                Accessible.description: "IR zoom and isotherm."
                onClicked: moreMenu.popup(moreButton, moreButton.width - moreMenu.implicitWidth, -moreMenu.implicitHeight - 6)

                Menu {
                    id: moreMenu
                    Menu {
                        id: zoomMenu
                        title: "IR zoom"
                        font: AppFont.body(14, 600)
                        Instantiator {
                            model: App.irZoomMax
                            delegate: MenuItem {
                                required property int index
                                readonly property int zoom: index + 1
                                text: zoom + "×"
                                checkable: true
                                checked: zoom === (App.irZoom > 0 ? App.irZoom : 1)
                                font: AppFont.body(14, 600)
                                onTriggered: App.setIRZoom(zoom)
                            }
                            onObjectAdded: (index, object) => zoomMenu.insertItem(index, object)
                            onObjectRemoved: (index, object) => zoomMenu.removeItem(object)
                        }
                    }
                    MenuItem {
                        visible: App.isothermSupported
                        height: visible ? implicitHeight : 0
                        text: App.isothermOn ? "Isotherm " + App.isothermLow + "–" + App.isothermHigh + "°" : "Isotherm…"
                        font: AppFont.body(14, 600)
                        onTriggered: menuIsothermPanel.open()
                    }
                }
                IsothermPanel {
                    id: menuIsothermPanel
                    x: moreButton.width - width
                    y: -height - 8
                }
            }
        }
    }
}
