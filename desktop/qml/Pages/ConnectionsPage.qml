import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Imago

// Saved connection profiles: pick one, edit, add or delete.
Item {
    id: root

    property var editing: null
    signal done()

    FormPage {
        anchors.fill: parent
        visible: root.editing === null
        title: "Connections"
        leading: [
            ToolButton {
                text: "add"
                font.family: AppFont.iconFamily
                font.pixelSize: 24
                Material.foreground: Theme.magenta
                Accessible.name: "Add connection"
                onClicked: root.editing = App.newProfile()
            }
        ]
        trailing: [BarButton { text: "Done"; bold: true; onClicked: root.done() }]

        FormSection {
            footer: "Changes apply the next time you connect."
            Repeater {
                model: App.profiles
                Column {
                    id: entry
                    required property var modelData
                    required property int index
                    readonly property bool selected: modelData.id === App.selectedProfileId
                    width: parent.width
                    RowDivider { visible: entry.index > 0 }
                    FormRow {
                        clickable: true
                        onClicked: App.selectProfile(entry.modelData.id)
                        Icon {
                            name: entry.selected ? "check_circle" : "radio_button_unchecked"
                            size: 22
                            color: entry.selected ? Theme.magenta : Theme.textTertiary
                        }
                        Column {
                            Layout.fillWidth: true
                            Layout.leftMargin: 4
                            Text { text: entry.modelData.name; color: Theme.text; font: AppFont.display(16, 600) }
                            Text {
                                width: parent.width
                                readonly property var p: entry.modelData
                                text: (p.mavlinkMode === "direct" ? p.payloadIP + ":" + p.mavlinkPort
                                                                  : "listen " + (p.localPort === 0 ? 14550 : p.localPort))
                                      + " · " + (p.videoURL || "video from payload")
                                color: Theme.textSecondary
                                font: AppFont.figures(12, 500)
                                elide: Text.ElideRight
                            }
                        }
                        ToolButton {
                            text: "edit"
                            font.family: AppFont.iconFamily
                            font.pixelSize: 20
                            Accessible.name: "Edit " + entry.modelData.name
                            onClicked: root.editing = entry.modelData
                        }
                        ToolButton {
                            text: "delete"
                            font.family: AppFont.iconFamily
                            font.pixelSize: 20
                            Material.foreground: Theme.textSecondary
                            Accessible.name: "Delete " + entry.modelData.name
                            onClicked: {
                                confirm.profile = entry.modelData
                                confirm.open()
                            }
                        }
                    }
                }
            }
        }
    }

    Loader {
        anchors.fill: parent
        active: root.editing !== null
        sourceComponent: ProfileEditor {
            profile: root.editing
            onCancelled: root.editing = null
            onSaved: draft => {
                App.saveProfile(draft)
                root.editing = null
            }
        }
    }

    Dialog {
        id: confirm
        property var profile: ({})
        anchors.centerIn: parent
        modal: true
        title: "Delete " + (profile.name || "") + "?"
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: App.deleteProfile(profile.id)
        Component.onCompleted: standardButton(Dialog.Ok).text = "Delete"
    }
}
