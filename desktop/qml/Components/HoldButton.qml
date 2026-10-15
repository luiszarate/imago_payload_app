import QtQuick
import Imago

// Acts while held: zoom in/out. Fires pressed once and released once, also when the press is
// cancelled or the button goes away, so a zoom can never be left running.
Item {
    id: root

    property string icon
    property string description
    property bool held: false
    signal pressed()
    signal released()

    implicitWidth: 56
    implicitHeight: 52
    scale: held ? 0.94 : 1

    Accessible.role: Accessible.Button
    Accessible.name: description
    // Assistive technologies cannot hold: act for half a second.
    Accessible.onPressAction: {
        root.pressed()
        pulse.restart()
    }

    function press() {
        if (held) return
        held = true
        pressed()
    }
    function release() {
        if (!held) return
        held = false
        released()
    }

    Timer {
        id: pulse
        interval: 500
        onTriggered: root.released()
    }

    Rectangle {
        anchors.fill: parent
        radius: 13
        color: Theme.surfaceRaised
        gradient: root.held ? Theme.accent : null
        border.color: mouse.containsMouse && !root.held ? Theme.stroke : "transparent"
    }
    Icon {
        anchors.centerIn: parent
        name: root.icon
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onPressed: root.press()
        onReleased: root.release()
        onCanceled: root.release()
    }

    Component.onDestruction: release()
}
