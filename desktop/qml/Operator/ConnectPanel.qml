import QtQuick
import QtQuick.Controls
import Imago

// Shown while there is no session or no picture yet.
Panel {
    id: root

    property bool compact: false

    radius: 22
    implicitWidth: Math.max(column.implicitWidth, 300) + (compact ? 40 : 56)
    implicitHeight: column.implicitHeight + (compact ? 40 : 56)

    Column {
        id: column
        anchors.centerIn: parent
        spacing: root.compact ? 12 : 16

        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            Wordmark {
                anchors.horizontalCenter: parent.horizontalCenter
                size: 26
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Payload Control"
                color: Theme.textSecondary
                font: AppFont.display(15, 500)
            }
        }

        // Connected, waiting for the payload or the picture
        BusyIndicator {
            visible: App.isConnected
            running: visible
            anchors.horizontalCenter: parent.horizontalCenter
            implicitWidth: 40
            implicitHeight: 40
        }
        Text {
            visible: App.isConnected
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(implicitWidth, 380)
            text: App.videoMessage || "Connecting…"
            color: Theme.textSecondary
            font: AppFont.body(14, 500)
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }
        Text {
            visible: App.isConnected
            anchors.horizontalCenter: parent.horizontalCenter
            text: App.link.up ? "Payload link up" : "Waiting for the payload…"
            color: App.link.up ? Theme.ok : Theme.warning
            font: AppFont.body(13, 600)
        }

        // Not connected: choose a profile and connect.
        Rectangle {
            id: picker
            visible: !App.isConnected
            anchors.horizontalCenter: parent.horizontalCenter
            width: 300
            height: 54
            radius: 12
            color: pickerMouse.containsMouse ? Qt.lighter(Theme.surfaceRaised, 1.3) : Theme.surfaceRaised

            Column {
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.right: chevron.left
                anchors.verticalCenter: parent.verticalCenter
                Text {
                    width: parent.width
                    text: App.selectedProfile.name || "Choose a connection"
                    color: Theme.text
                    font: AppFont.display(15, 600)
                    elide: Text.ElideRight
                }
                Text {
                    visible: App.selectedProfile.id !== undefined
                    text: App.selectedProfile.mavlinkMode === "listen" ? "Listening for MAVLink" : (App.selectedProfile.payloadIP || "")
                    color: Theme.textSecondary
                    font: AppFont.figures(12, 500)
                }
            }
            Icon {
                id: chevron
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                name: "unfold_more"
                color: Theme.textSecondary
            }
            MouseArea {
                id: pickerMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: profileMenu.popup(picker, 0, picker.height + 4)
            }

            Menu {
                id: profileMenu
                width: picker.width
                Instantiator {
                    model: App.profiles
                    delegate: MenuItem {
                        required property var modelData
                        text: modelData.name
                        checkable: true
                        checked: modelData.id === App.selectedProfileId
                        font: AppFont.body(14, 600)
                        onTriggered: App.selectProfile(modelData.id)
                    }
                    onObjectAdded: (index, object) => profileMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => profileMenu.removeItem(object)
                }
                MenuSeparator {}
                MenuItem {
                    text: "Edit connections…"
                    font: AppFont.body(14, 600)
                    onTriggered: App.showConnections = true
                }
            }
        }

        Text {
            visible: !App.isConnected && App.connectError.length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(implicitWidth, 360)
            text: App.connectError
            color: Theme.danger
            font: AppFont.body(13, 600)
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }

        Rectangle {
            id: connectButton
            readonly property bool canConnect: App.selectedProfile.id !== undefined
            visible: !App.isConnected
            anchors.horizontalCenter: parent.horizontalCenter
            width: 220
            height: 50
            radius: 14
            gradient: Theme.accent
            opacity: canConnect ? (connectMouse.containsMouse ? 0.92 : 1) : 0.4
            scale: connectMouse.pressed ? 0.96 : 1
            Accessible.role: Accessible.Button
            Accessible.name: "Connect"
            Accessible.onPressAction: App.connectToPayload()

            Text {
                anchors.centerIn: parent
                text: "Connect"
                color: Theme.text
                font: AppFont.display(16, 700)
            }
            MouseArea {
                id: connectMouse
                anchors.fill: parent
                hoverEnabled: true
                enabled: connectButton.canConnect
                cursorShape: Qt.PointingHandCursor
                onClicked: App.connectToPayload()
            }
        }
    }
}
