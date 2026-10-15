import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Imago

// Thermal controls along the bottom while the IR picture is the main one (view IR or IR + EO):
// palette, the picture's temperature scale, IR zoom, flat-field correction and, when the camera
// has it, the isotherm.
Panel {
    id: root

    property bool compact: false
    readonly property real buttonHeight: compact ? 42 : 52

    radius: 16
    implicitHeight: compact ? 52 : 68

    // FFC and ISO: icon, title and a state line.
    component ThermalButton: Rectangle {
        id: button
        property string icon
        property string title
        property string detail
        property bool active: false
        /// Also right-click and press-and-hold.
        property bool hasMenu: false
        signal clicked(int button)
        signal pressed()
        signal held()

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
            acceptedButtons: button.hasMenu ? Qt.LeftButton | Qt.RightButton : Qt.LeftButton
            onPressed: button.pressed()
            onPressAndHold: if (button.hasMenu) button.held()
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

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: root.compact ? 5 : 8
        anchors.rightMargin: root.compact ? 5 : 8
        spacing: root.compact ? 6 : 10

        // Palette: a swatch, its name and a menu of all of them.
        Rectangle {
            id: paletteButton
            readonly property string name: App.irPalette >= 0 ? App.irPalettes[App.irPalette].title : "—"
            Layout.preferredWidth: root.compact ? 118 : 150
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
            Icon {
                id: chevron
                anchors.right: parent.right
                anchors.rightMargin: root.compact ? 6 : 8
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
            Layout.fillWidth: true
            Layout.minimumWidth: root.compact ? 90 : 140
            Layout.alignment: Qt.AlignVCenter
            compact: root.compact
        }

        // IR digital zoom, 1–8×.
        Rectangle {
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

        // Flat-field correction. Click: now. Right-click or hold: automatic or manual.
        ThermalButton {
            id: ffcButton
            /// The hold opened the menu: its release is not a click.
            property bool menuByHold: false
            icon: "autorenew"
            title: "FFC"
            detail: App.thermal.ffcMode < 0 ? " " : (App.thermal.ffcMode === 1 ? "AUTO" : "MAN")
            Accessible.name: "Flat-field correction"
            Accessible.description: "Recalibrates the thermal sensor. Right-click or hold for automatic or manual."
            hasMenu: true
            onPressed: menuByHold = false
            onHeld: {
                menuByHold = true
                ffcMenu.show()
            }
            onClicked: button => {
                if (button === Qt.RightButton) ffcMenu.show()
                else if (!menuByHold) App.triggerFFC()
            }

            Menu {
                id: ffcMenu
                function show() { popup(ffcButton, 0, -implicitHeight - 6) }
                MenuItem {
                    text: "Calibrate now"
                    font: AppFont.body(14, 600)
                    onTriggered: App.triggerFFC()
                }
                MenuItem {
                    text: "Automatic FFC"
                    font: AppFont.body(14, 600)
                    checkable: true
                    checked: App.thermal.ffcMode === 1
                    onTriggered: App.setFFCAuto(App.thermal.ffcMode !== 1)
                }
            }
        }

        // Isotherm, when the camera has it (it answered a read of C_T_ISO_M).
        ThermalButton {
            id: isothermButton
            visible: App.isothermSupported
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
    }
}
