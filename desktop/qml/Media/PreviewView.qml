import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Imago

// One capture large, with previous / next (arrow keys). Photos come down whole into a cache
// folder first; until then the thumbnail stands in. Videos and radiometric TIFFs have no
// picture here: download, then open them with the system's app.
Rectangle {
    id: root

    readonly property var media: App.media
    readonly property var file: media.previewFile
    readonly property int row: media.previewRow
    /// The sheet's file count, for "3 of 24" and the next button.
    property int count: 0
    readonly property bool tile: media.previewState === "tile"

    signal downloadRequested()
    signal deleteRequested()

    color: Theme.background

    // The picture area takes the clicks, so none reach the sheet underneath.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: wheel => wheel.accepted = true
    }

    RowLayout {
        id: bar
        anchors { left: parent.left; right: parent.right; top: parent.top }
        anchors.leftMargin: 14
        anchors.rightMargin: 16
        height: 56
        spacing: 12

        MediaButton {
            glyph: "arrow_back"
            tip: "Back to the sheet (Esc)"
            onClicked: root.media.closePreview()
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            Text {
                Layout.fillWidth: true
                text: root.file.name || ""
                color: Theme.text
                font: AppFont.body(14, 700)
                elide: Text.ElideMiddle
            }
            Text {
                Layout.fillWidth: true
                text: [root.file.timeText, root.file.sizeText, root.row >= 0 ? (root.row + 1) + " of " + root.count : ""]
                      .filter(t => t).join("  ·  ")
                color: Theme.textSecondary
                font: AppFont.figures(12, 500)
                elide: Text.ElideRight
            }
        }
        Row {
            visible: root.file.onDevice === true
            spacing: 6
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: "check_circle"
                size: 16
                color: Theme.ok
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "On this device"
                color: Theme.textSecondary
                font: AppFont.body(13, 600)
            }
        }
        MediaButton {
            visible: root.file.onDevice === true && root.tile
            text: "Open"
            glyph: "open_in_new"
            onClicked: root.media.openPreviewedFile()
        }
        MediaButton {
            visible: root.file.onDevice !== true
            text: "Download"
            glyph: "file_download"
            kind: "accent"
            onClicked: root.downloadRequested()
        }
        MediaButton {
            text: "Delete"
            glyph: "delete_outline"
            kind: "danger"
            onClicked: root.deleteRequested()
        }
    }

    Item {
        id: stage
        anchors { left: parent.left; right: parent.right; top: bar.bottom; bottom: parent.bottom }
        anchors.margins: 16
        anchors.topMargin: 4

        // The thumbnail, scaled up, while the whole photo comes down.
        Image {
            id: standIn
            anchors.fill: parent
            visible: !root.tile && full.status !== Image.Ready
            source: root.tile || !root.file.name ? "" : "image://media/" + encodeURIComponent(root.file.name)
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            cache: false
            sourceSize.width: 320
            smooth: true
            opacity: 0.55
        }
        Image {
            id: full
            anchors.fill: parent
            source: root.media.previewSource
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            cache: false
            autoTransform: true
            sourceSize.width: Math.round(width * Screen.devicePixelRatio)
            sourceSize.height: Math.round(height * Screen.devicePixelRatio)
            opacity: status === Image.Ready ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 180; easing.type: Easing.OutQuad } }
        }

        // Loading, waiting, failed.
        Rectangle {
            id: notice
            readonly property string phase: root.media.previewState
            visible: phase === "loading" || phase === "waiting" || phase === "failed"
                     || (phase === "ready" && full.status === Image.Error)
            anchors.centerIn: parent
            width: statusRow.implicitWidth + 36
            height: 44
            radius: 22
            color: Theme.alpha(Theme.surface, 0.9)
            border.color: Theme.stroke
            Row {
                id: statusRow
                anchors.centerIn: parent
                spacing: 10
                BusyIndicatorSmall {
                    visible: notice.phase === "loading" || notice.phase === "waiting"
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    readonly property string s: notice.phase
                    text: s === "waiting" ? "Waiting for the downloads in progress…"
                          : s === "loading" ? "Loading the full picture" + (root.media.previewProgress > 0
                                              ? "  " + Math.floor(root.media.previewProgress * 100) + "%" : "…")
                          : "Could not load the picture."
                    color: Theme.text
                    font: AppFont.figures(13, 600)
                }
                MediaButton {
                    visible: notice.phase === "failed" || notice.phase === "ready"
                    anchors.verticalCenter: parent.verticalCenter
                    implicitHeight: 28
                    text: "Try again"
                    onClicked: root.media.openPreview(root.row)
                }
            }
        }

        // Videos and radiometric TIFFs.
        Rectangle {
            visible: root.tile
            anchors.centerIn: parent
            width: Math.min(parent.width, parent.height * 16 / 9, 760)
            height: width * 9 / 16
            color: Theme.surfaceRaised
            Column {
                anchors.centerIn: parent
                width: parent.width - 64
                spacing: 12
                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: root.file.video ? "movie" : "grid_on"
                    size: 44
                    color: Theme.textTertiary
                }
                Text {
                    width: parent.width
                    text: root.file.video
                          ? (root.file.onDevice ? "Open plays it in your video player." : "Videos play once they are on this computer.")
                          : (root.file.onDevice ? "Open it in your thermal analysis software."
                                                : "Radiometric TIFF: download it to open it in thermal analysis software.")
                    color: Theme.textSecondary
                    font: AppFont.body(14, 500)
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        MediaButton {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            implicitWidth: 44
            implicitHeight: 44
            glyph: "chevron_left"
            tip: "Previous (Left arrow)"
            enabled: root.row > 0
            onClicked: root.media.stepPreview(-1)
        }
        MediaButton {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            implicitWidth: 44
            implicitHeight: 44
            glyph: "chevron_right"
            tip: "Next (Right arrow)"
            enabled: root.row >= 0 && root.row < root.count - 1
            onClicked: root.media.stepPreview(1)
        }

        // The full picture's download, along the bottom edge.
        Rectangle {
            visible: root.media.previewState === "loading" && root.media.previewProgress > 0
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            height: 2
            width: parent.width * root.media.previewProgress
            gradient: Theme.accent
        }
    }
}
