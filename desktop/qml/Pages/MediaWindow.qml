import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import Imago

// The payload's photos and videos, read as flight rolls: captures grouped into flights by their
// time gaps, newest first on the left, the chosen flight's sheet of thumbnails on the right. A
// window of its own, so the live video stays in sight in the main window; closing it closes
// Media.
ApplicationWindow {
    id: window
    objectName: "mediaWindow"

    readonly property var media: App.media
    readonly property bool hasFiles: media.fileCount > 0
    readonly property bool previewing: media.previewRow >= 0 || media.previewState.length > 0

    width: media.startSize.width > 0 ? Math.max(minimumWidth, media.startSize.width) : 1200
    height: media.startSize.height > 0 ? Math.max(minimumHeight, media.startSize.height) : 780
    minimumWidth: 820
    minimumHeight: 520
    visible: true
    transientParent: null
    title: "Media — Imago Payload Control"
    color: Theme.background

    Material.theme: Material.Dark
    Material.accent: Theme.magenta
    Material.primary: Theme.magenta
    Material.background: Theme.surfaceRaised
    Material.foreground: Theme.text
    font: AppFont.body(14, 500)

    onClosing: media.open = false

    // MARK: Actions that may ask first

    // Downloads share the datalink with the live video: the first of a batch asks whether the
    // video pauses meanwhile (the user's choice, 2026-10-19).
    function download(kind, flightRow) {
        const fetching = kind === "selection" ? media.selectionToFetch()
                       : kind === "flight" ? media.flightToFetch(flightRow)
                       // Files the preview already brought into its cache are copied, not fetched.
                       : media.previewToFetch()
        if (fetching > 0 && App.videoActive && !media.videoPaused && media.transfer.state !== "running") {
            liveVideo.kind = kind
            liveVideo.flightRow = flightRow
            liveVideo.open()
            return
        }
        startDownload(kind, flightRow, false)
    }
    function startDownload(kind, flightRow, pauseVideo) {
        if (kind === "selection") media.downloadSelection(pauseVideo)
        else if (kind === "flight") media.downloadFlight(flightRow, pauseVideo)
        else media.downloadPreviewed(pauseVideo)
    }
    function askDelete(fromPreview) {
        const count = fromPreview ? 1 : media.selectionCount
        if (count === 0) return
        deleteDialog.fromPreview = fromPreview
        deleteDialog.count = count
        deleteDialog.pairs = fromPreview ? (media.previewFile.pair || "").length > 0 : media.selectionHasPairs()
        deleteDialog.open()
    }

    // MARK: Header

    Item {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 64

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 22
            anchors.rightMargin: 16
            spacing: 14

            Text {
                text: "Media"
                color: Theme.text
                font: AppFont.display(21, 600)
            }

            // Storage: used of total.
            Row {
                visible: window.media.storageText.length > 0
                spacing: 9
                Layout.leftMargin: 6
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 72
                    height: 4
                    radius: 2
                    color: Theme.alpha(Theme.text, 0.12)
                    Rectangle {
                        width: parent.width * Math.max(0, window.media.storageFraction)
                        height: parent.height
                        radius: 2
                        color: window.media.storageFraction > 0.9 ? Theme.warning : Theme.textSecondary
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: window.media.storageText
                    color: Theme.textSecondary
                    font: AppFont.figures(12, 500)
                }
            }

            // Internal memory or SD card, when the payload has both.
            MediaButton {
                id: storageButton
                visible: window.media.storages.length > 1
                text: window.media.storages[window.media.currentStorage] || "Storage"
                glyph: "sd_card"
                onClicked: storageMenu.popup(storageButton, 0, height + 4)
                CheckMenu {
                    id: storageMenu
                    options: window.media.storages.map((title, value) => ({ value: value, title: title }))
                    current: window.media.currentStorage
                    onChosen: value => window.media.setStorage(value)
                }
            }

            Text {
                // The list is shown but could not be read again.
                visible: window.hasFiles && window.media.error.length > 0
                Layout.fillWidth: true
                text: "Could not refresh: " + window.media.error
                color: Theme.danger
                font: AppFont.body(12, 600)
                elide: Text.ElideRight
            }
            Item { Layout.fillWidth: true; visible: !(window.hasFiles && window.media.error.length > 0) }

            SegmentedControl {
                visible: window.hasFiles
                options: ["All", "Photos", "Videos"]
                current: window.media.filter
                onChosen: index => window.media.filter = index
            }
            Item {
                implicitWidth: 34
                implicitHeight: 34
                MediaButton {
                    anchors.fill: parent
                    visible: !refreshing.visible
                    glyph: "refresh"
                    tip: "Refresh (F5)"
                    enabled: window.media.connected
                    onClicked: window.media.refresh()
                }
                BusyIndicatorSmall {
                    id: refreshing
                    anchors.centerIn: parent
                    implicitWidth: 20
                    implicitHeight: 20
                    visible: window.hasFiles && window.media.status === 1
                }
            }
            MediaButton {
                id: folderButton
                glyph: "folder_open"
                tip: "Download folder"
                onClicked: folderMenu.popup(folderButton, width - folderMenu.width, height + 4)
                Menu {
                    id: folderMenu
                    width: Math.max(260, folderPath.implicitWidth + 32)
                    MenuItem {
                        id: folderPath
                        enabled: false
                        text: window.media.folder
                        font: AppFont.body(12, 600)
                    }
                    MenuSeparator {}
                    MenuItem {
                        text: "Change folder…"
                        font: AppFont.body(14, 600)
                        onTriggered: folderDialog.open()
                    }
                    MenuItem {
                        text: "Open folder"
                        font: AppFont.body(14, 600)
                        onTriggered: window.media.openFolder()
                    }
                }
            }
        }
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 1
            color: Theme.stroke
        }
    }

    // MARK: Flight rail

    Item {
        id: rail
        anchors { left: parent.left; top: header.bottom; bottom: parent.bottom }
        width: Math.round(Math.max(264, Math.min(340, window.width * 0.26)))
        visible: window.hasFiles

        ListView {
            id: flightList
            anchors.fill: parent
            anchors.topMargin: 8
            anchors.bottomMargin: 8
            clip: true
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            model: window.media.flights
            ScrollBar.vertical: ScrollBar {}
            delegate: FlightRow {
                width: flightList.width
                onChosen: window.media.currentFlight = index
                onDownloadRequested: window.download("flight", index)
                onScrubbed: fraction => {
                    const row = window.media.rowNearest(fraction)
                    if (row < 0) return
                    sheet.scrubIndex = row
                    sheet.positionViewAtIndex(row, GridView.Contain)
                }
                onScrubEnded: {
                    if (sheet.scrubIndex >= 0) sheet.currentIndex = sheet.scrubIndex
                    sheet.scrubIndex = -1
                }
            }
        }
        Rectangle {
            anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
            width: 1
            color: Theme.stroke
        }
    }

    // MARK: Sheet

    Item {
        id: sheetArea
        anchors { left: rail.visible ? rail.right : parent.left; right: parent.right; top: header.bottom; bottom: parent.bottom }

        GridView {
            id: sheet
            /// The capture the timeline points at while it is dragged.
            property int scrubIndex: -1
            readonly property int columns: Math.max(2, Math.floor(width / 212))

            anchors { left: parent.left; right: parent.right; top: parent.top; bottom: bar.visible ? bar.top : parent.bottom }
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            topMargin: 10
            bottomMargin: 10
            clip: true
            focus: true
            visible: window.hasFiles
            boundsBehavior: Flickable.StopAtBounds
            cellWidth: Math.floor(width / columns)
            cellHeight: Math.round((cellWidth - 12) * 9 / 16) + 12 + 26
            currentIndex: -1
            keyNavigationEnabled: false
            highlightFollowsCurrentItem: false
            model: window.media.files
            ScrollBar.vertical: ScrollBar {}

            // A new flight or filter starts at the top.
            Connections {
                target: window.media
                function onCurrentFlightChanged() {
                    sheet.currentIndex = -1
                    sheet.positionViewAtBeginning()
                }
            }

            delegate: MediaCell {
                width: sheet.cellWidth
                height: sheet.cellHeight
                isCurrent: sheet.currentIndex === index
                keyboardFocus: sheet.activeFocus
                scrubTarget: sheet.scrubIndex === index
                onClicked: modifiers => {
                    sheet.forceActiveFocus()
                    sheet.currentIndex = index
                    window.media.click(index, (modifiers & Qt.ControlModifier) !== 0, (modifiers & Qt.ShiftModifier) !== 0)
                }
                onDoubleClicked: window.media.openPreview(index)
                onMenuRequested: (x, y) => {
                    sheet.forceActiveFocus()
                    sheet.currentIndex = index
                    window.media.selectForMenu(index)
                    cellMenu.popup()
                }
            }

            // Click selects, Ctrl toggles, Shift extends; arrows move (Shift extends), Space
            // toggles, Ctrl+A takes the flight, Esc clears, Delete asks, Enter opens.
            Keys.onPressed: event => {
                const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
                const shift = (event.modifiers & Qt.ShiftModifier) !== 0
                const last = count - 1
                let next = -1
                switch (event.key) {
                case Qt.Key_Left: next = currentIndex - 1; break
                case Qt.Key_Right: next = currentIndex + 1; break
                case Qt.Key_Up: next = currentIndex - columns; break
                case Qt.Key_Down: next = currentIndex < 0 ? 0 : currentIndex + columns; break
                case Qt.Key_Home: next = 0; break
                case Qt.Key_End: next = last; break
                case Qt.Key_A:
                    if (!ctrl) return
                    window.media.selectAll()
                    event.accepted = true
                    return
                case Qt.Key_Escape:
                    if (window.media.selectionCount === 0) return
                    window.media.clearSelection()
                    event.accepted = true
                    return
                case Qt.Key_Delete:
                case Qt.Key_Backspace:
                    window.askDelete(false)
                    event.accepted = true
                    return
                case Qt.Key_Return:
                case Qt.Key_Enter:
                    if (currentIndex >= 0) window.media.openPreview(currentIndex)
                    event.accepted = true
                    return
                case Qt.Key_Space:
                    if (currentIndex >= 0) window.media.click(currentIndex, true, false)
                    event.accepted = true
                    return
                default:
                    return
                }
                if (last < 0) return
                next = Math.max(0, Math.min(last, next))
                currentIndex = next
                positionViewAtIndex(next, GridView.Contain)
                window.media.click(next, false, shift)
                event.accepted = true
            }
        }

        // Nothing to show: why, and what to do.
        SheetMessage {
            readonly property int status: window.media.status
            readonly property bool emptyView: window.hasFiles && sheet.count === 0
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -20
            visible: !window.media.connected || !window.hasFiles || emptyView
            busy: window.media.connected && !window.hasFiles && (status === 0 || status === 1)
            icon: !window.media.connected ? "link_off"
                  : status === 3 ? "cloud_off"
                  : status === 4 ? "lock_outline"
                  : status === 5 ? "hourglass_empty" : "perm_media"
            title: !window.media.connected ? "Connect to the payload to see its media."
                   : emptyView ? (window.media.filter === 2 ? "No videos in this flight." : "No photos in this flight.")
                   : status === 0 || status === 1 ? "Reading the payload's media…"
                   : status === 3 ? "Could not read the payload's media."
                   : status === 4 ? "This payload asks for a login before it shares its media."
                   : status === 5 ? "The payload is still starting its cameras."
                   : "No photos or videos on the payload."
            body: !window.media.connected || emptyView ? ""
                  : status === 3 ? window.media.error
                  : status === 4 ? "The app does not support that yet."
                  : status === 5 ? "Trying again in a few seconds." : ""
            action: window.media.connected && !window.hasFiles && status === 3 ? "Try again" : ""
            onTriggered: window.media.refresh()
        }

        TransferBar {
            id: bar
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            onDownloadRequested: window.download("selection", -1)
            onDeleteRequested: window.askDelete(false)
        }

        Menu {
            id: cellMenu
            MenuItem {
                text: window.media.selectionCount > 1 ? "Download " + window.media.selectionCount + " shots" : "Download"
                font: AppFont.body(14, 600)
                onTriggered: window.download("selection", -1)
            }
            MenuItem {
                text: window.media.selectionCount > 1 ? "Open preview" : "Open"
                font: AppFont.body(14, 600)
                onTriggered: window.media.openPreview(sheet.currentIndex)
            }
            MenuSeparator {}
            MenuItem {
                text: window.media.selectionCount > 1 ? "Delete " + window.media.selectionCount + " shots…" : "Delete…"
                font: AppFont.body(14, 600)
                Material.foreground: Theme.danger
                onTriggered: window.askDelete(false)
            }
        }
    }

    // MARK: Preview

    PreviewView {
        id: preview
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: parent.bottom }
        visible: window.previewing
        count: sheet.count
        focus: visible
        onVisibleChanged: if (!visible) sheet.forceActiveFocus()
        onDownloadRequested: window.download("preview", -1)
        onDeleteRequested: window.askDelete(true)
        Keys.onLeftPressed: window.media.stepPreview(-1)
        Keys.onRightPressed: window.media.stepPreview(1)
        Keys.onEscapePressed: window.media.closePreview()
        Keys.onDeletePressed: window.askDelete(true)
    }

    // MARK: Dialogs

    Dialog {
        id: liveVideo
        property string kind
        property int flightRow: -1
        parent: Overlay.overlay
        anchors.centerIn: parent
        contentWidth: 400
        modal: true
        title: "Live video is on"
        header: DialogTitle { text: liveVideo.title }
        Overlay.modal: Rectangle { color: Theme.alpha(Theme.background, 0.72) }
        background: DialogBackground {}
        Label {
            anchors.left: parent.left
            anchors.right: parent.right
            text: "Downloading over the datalink can make the live video stutter or fall behind."
            wrapMode: Text.WordWrap
            color: Theme.textSecondary
        }
        footer: DialogButtonBox {
            background: Item {}
            Button {
                text: "Cancel"
                flat: true
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            Button {
                text: "Download alongside"
                flat: true
                onClicked: { liveVideo.close(); window.startDownload(liveVideo.kind, liveVideo.flightRow, false) }
            }
            Button {
                text: "Pause live video"
                flat: true
                highlighted: true
                onClicked: { liveVideo.close(); window.startDownload(liveVideo.kind, liveVideo.flightRow, true) }
            }
        }
    }

    Dialog {
        id: deleteDialog
        property bool fromPreview: false
        property int count: 0
        parent: Overlay.overlay
        anchors.centerIn: parent
        contentWidth: 360
        modal: true
        // A shot is both of its files, EO and IR (field report 2026-10-20).
        property bool pairs: false
        title: count === 1 ? "Delete this shot from the payload?" : "Delete " + count + " shots from the payload?"
        header: DialogTitle { text: deleteDialog.title }
        Overlay.modal: Rectangle { color: Theme.alpha(Theme.background, 0.72) }
        background: DialogBackground {}
        Label {
            anchors.left: parent.left
            anchors.right: parent.right
            text: deleteDialog.pairs ? "Each shot's EO and IR files are deleted. This cannot be undone."
                                     : "This cannot be undone."
            wrapMode: Text.WordWrap
            color: Theme.textSecondary
        }
        footer: DialogButtonBox {
            background: Item {}
            Button {
                text: "Cancel"
                flat: true
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            Button {
                text: "Delete"
                flat: true
                Material.foreground: Theme.danger
                DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
                onClicked: {
                    deleteDialog.close()
                    if (deleteDialog.fromPreview) window.media.deletePreviewed()
                    else window.media.deleteSelection()
                }
            }
        }
    }

    FolderDialog {
        id: folderDialog
        title: "Download folder"
        currentFolder: window.media.folderUrl
        onAccepted: window.media.setFolder(selectedFolder)
    }

    Shortcut {
        sequences: ["Ctrl+M", StandardKey.Close]
        onActivated: window.close()
    }
    Shortcut {
        sequences: [StandardKey.Refresh, "Ctrl+R"]
        onActivated: window.media.refresh()
    }

    // MARK: Development aid (--media-demo)

    Connections {
        target: window.media
        function onFilesReady() { if (window.media.demo.length > 0) demoTimer.start() }
    }
    Timer {
        id: demoTimer
        interval: 400
        onTriggered: {
            const demo = window.media.demo
            if (demo === "preview" || demo === "preview-ir") {
                window.media.openPreview(1)
                if (demo === "preview-ir") window.media.previewCamera = 1
                return
            }
            // Six photos; for a running download, through the flight's first videos, which
            // take a while at orus-sim's --media-rate.
            const downloads = demo === "download" || demo === "pause"
            const last = downloads ? Math.min(sheet.count - 1, 13) : 5
            sheet.currentIndex = last
            window.media.click(0, false, false)
            window.media.click(last, false, true)
            if (downloads) window.startDownload("selection", -1, demo === "pause")
            else if (demo === "ask") liveVideo.open()
            else if (demo === "delete") window.askDelete(false)
        }
    }
}
