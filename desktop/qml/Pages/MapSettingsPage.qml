import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Imago

// Settings › Map: the map over the picture and the tiles it is drawn from. On the payload's
// Wi-Fi there is no internet, so the flight area is downloaded here beforehand (as on the
// Quest's panel, section "Offline map").
Column {
    id: root

    property string lat: ""
    property string lon: ""
    property string radiusKm: ""
    property string problem: ""
    property string sourcesProblem: ""
    property var draftSources: ({})
    readonly property bool sourcesChanged: JSON.stringify(draftSources) !== JSON.stringify(App.map.sources)

    spacing: 6

    function fill() {
        const area = App.map.area()
        lat = area.lat
        lon = area.lon
        radiusKm = area.radiusKm
        showSources()
    }
    // The fields keep what is typed; Save and Restore defaults put the stored values back.
    function showSources() {
        draftSources = Object.assign({}, App.map.sources)
        satelliteUrlField.text = draftSources.satelliteUrl
        satelliteCreditField.text = draftSources.satelliteCredit
        streetUrlField.text = draftSources.streetUrl
        streetCreditField.text = draftSources.streetCredit
    }
    Component.onCompleted: {
        fill()
        App.map.refreshStored()
    }
    onVisibleChanged: if (visible) {
        fill()
        App.map.refreshStored()
    }

    function setSource(key, value) {
        let copy = Object.assign({}, draftSources)
        copy[key] = value
        draftSources = copy
    }

    component SmallButton: Button {
        property bool danger: false
        flat: true
        font: AppFont.body(13, 600)
        Material.foreground: danger ? Theme.danger : Theme.magenta
    }

    component AreaField: Column {
        id: areaField
        property alias label: caption.text
        property alias text: field.text
        signal edited(string text)
        // Shared out by the row, not by what each field holds.
        Layout.fillWidth: true
        Layout.preferredWidth: 100
        spacing: 2
        Text {
            id: caption
            color: Theme.textSecondary
            font: AppFont.body(12, 500)
        }
        TextField {
            id: field
            width: parent.width
            font: AppFont.figures(15, 600)
            selectByMouse: true
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            onTextEdited: areaField.edited(text)
        }
    }

    FormSection {
        header: "Map"
        SwitchRow {
            title: "Show the map"
            note: "In the picture's bottom right corner, centred on the aircraft. Its own buttons hide it, zoom it and switch satellite / street."
            checked: App.map.visible
            onToggled: on => App.map.visible = on
        }
    }

    FormSection {
        header: "Offline map"
        footer: "The map shows what is stored on this computer: on the payload's Wi-Fi there is no internet. With internet, download the flight area here first (zoom 11 to 16, or to 17 with the closest zoom; fewer close-ups for a large area). After a flight, the aircraft's last position is filled in."
        FormRow {
            implicitHeight: areaRow.implicitHeight + 16
            RowLayout {
                id: areaRow
                Layout.fillWidth: true
                spacing: 12
                AreaField {
                    label: "Latitude, °"
                    text: root.lat
                    onEdited: text => root.lat = text
                }
                AreaField {
                    label: "Longitude, °"
                    text: root.lon
                    onEdited: text => root.lon = text
                }
                AreaField {
                    Layout.maximumWidth: 120
                    label: "Radius, km"
                    text: root.radiusKm
                    onEdited: text => root.radiusKm = text
                }
            }
        }
        RowDivider {}
        SwitchRow {
            title: "Closest zoom too (17)"
            note: "Streets and buildings up close. About three times the tiles, so three times the wait: off, a download stops at zoom 16."
            checked: App.map.downloadDetail
            enabled: !App.map.downloading
            onToggled: on => App.map.downloadDetail = on
        }
        RowDivider {}
        FormRow {
            SmallButton {
                visible: !App.map.downloading
                text: "Download satellite"
                onClicked: root.problem = App.map.download("satellite", root.lat, root.lon, root.radiusKm)
            }
            SmallButton {
                visible: !App.map.downloading
                text: "Download street map"
                onClicked: root.problem = App.map.download("street", root.lat, root.lon, root.radiusKm)
            }
            SmallButton {
                visible: App.map.downloading
                danger: true
                text: "Stop"
                onClicked: App.map.stopDownload()
            }
            BusyIndicatorSmall { visible: App.map.downloading }
            Item { Layout.fillWidth: true }
        }
        FormRow {
            visible: root.problem.length > 0 || App.map.downloadStatus.length > 0
            Text {
                Layout.fillWidth: true
                text: root.problem.length > 0 ? root.problem : App.map.downloadStatus
                color: root.problem.length > 0 ? Theme.warning : Theme.text
                font: AppFont.body(13, 600)
                wrapMode: Text.WordWrap
            }
        }
        RowDivider {}
        FormRow {
            Text {
                Layout.fillWidth: true
                text: App.map.storedText.length > 0 ? App.map.storedText : "Counting the stored tiles…"
                color: Theme.textSecondary
                font: AppFont.body(13, 500)
                wrapMode: Text.WordWrap
            }
            BusyIndicatorSmall { visible: App.map.storeBusy }
        }
        FormRow {
            visible: !App.map.downloading
            SmallButton {
                danger: true
                enabled: !App.map.storeBusy
                text: "Delete satellite"
                onClicked: deleteTiles.ask("satellite", "Delete the satellite map?")
            }
            SmallButton {
                danger: true
                enabled: !App.map.storeBusy
                text: "Delete street map"
                onClicked: deleteTiles.ask("street", "Delete the street map?")
            }
            Item { Layout.fillWidth: true }
        }
    }

    FormSection {
        header: "Tile sources"
        footer: "URLs take {z}, {x} and {y}. Mind the providers' terms: both defaults are public services meant for online viewing; OpenStreetMap's tile policy discourages bulk downloads, and Esri's imagery is licensed. A flight area of a few hundred tiles is modest use; for regular work put your own provider's URL (with its key) here. The credit is drawn on the map."
        TextFieldRow {
            id: satelliteUrlField
            label: "Satellite tiles URL"
            onEdited: text => root.setSource("satelliteUrl", text)
        }
        TextFieldRow {
            id: satelliteCreditField
            label: "Satellite credit"
            onEdited: text => root.setSource("satelliteCredit", text)
        }
        RowDivider {}
        TextFieldRow {
            id: streetUrlField
            label: "Street map tiles URL"
            onEdited: text => root.setSource("streetUrl", text)
        }
        TextFieldRow {
            id: streetCreditField
            label: "Street map credit"
            onEdited: text => root.setSource("streetCredit", text)
        }
        RowDivider {}
        FormRow {
            SmallButton {
                enabled: root.sourcesChanged
                text: "Save"
                onClicked: {
                    // A URL that cannot be used is refused: what was typed stays to be fixed.
                    root.sourcesProblem = App.map.setSources(root.draftSources.streetUrl, root.draftSources.streetCredit,
                                                             root.draftSources.satelliteUrl, root.draftSources.satelliteCredit)
                    if (root.sourcesProblem.length === 0) root.showSources()
                }
            }
            SmallButton {
                text: "Restore defaults"
                onClicked: {
                    App.map.restoreSources()
                    root.sourcesProblem = ""
                    root.showSources()
                }
            }
            Item { Layout.fillWidth: true }
        }
        FormRow {
            visible: root.sourcesProblem.length > 0
            Text {
                Layout.fillWidth: true
                text: root.sourcesProblem
                color: Theme.warning
                font: AppFont.body(13, 600)
                wrapMode: Text.WordWrap
            }
        }
    }

    Dialog {
        id: deleteTiles
        property string style: ""
        function ask(which, question) {
            style = which
            title = question
            open()
        }
        parent: Overlay.overlay
        anchors.centerIn: parent
        contentWidth: 360
        modal: true
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: App.map.deleteTiles(style)
        Component.onCompleted: standardButton(Dialog.Ok).text = "Delete"
        Label {
            anchors.left: parent.left
            anchors.right: parent.right
            text: "The tiles go from this computer. The map shows a grid there until the area is downloaded again."
            wrapMode: Text.WordWrap
        }
    }
}
