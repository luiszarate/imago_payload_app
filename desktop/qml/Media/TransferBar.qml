import QtQuick
import QtQuick.Controls
import Imago

// The bottom of the sheet: the selection and its actions; while downloads run, the transfer
// (file n of N, percent, speed, Cancel); afterwards what came down, until dismissed.
Rectangle {
    id: root

    readonly property var media: App.media
    readonly property var transfer: media.transfer
    readonly property string mode: transfer.state === "running" ? "running"
                                   : transfer.state === "finished" ? "finished"
                                   : media.notice.length > 0 ? "notice"
                                   : media.selectionCount > 0 ? "selection"
                                   : (transfer.deleting || 0) > 0 ? "deleting" : ""

    signal downloadRequested()
    signal deleteRequested()

    implicitHeight: 60
    color: Theme.surface
    visible: mode.length > 0

    Rectangle {
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 1
        color: Theme.stroke
    }

    // Progress along the top edge.
    Rectangle {
        visible: root.mode === "running"
        anchors.left: parent.left
        anchors.top: parent.top
        height: 2
        width: parent.width * (root.transfer.fraction || 0)
        gradient: Theme.accent
        Behavior on width { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
    }

    // Left: what is going on. Right: what can be done about it.
    Row {
        id: lead
        anchors.left: parent.left
        anchors.leftMargin: 18
        anchors.right: actions.left
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        spacing: 10

        BusyIndicatorSmall {
            visible: root.mode === "deleting"
            anchors.verticalCenter: parent.verticalCenter
            implicitWidth: 18
            implicitHeight: 18
        }
        Icon {
            visible: root.mode === "finished" || root.mode === "notice"
            anchors.verticalCenter: parent.verticalCenter
            width: 22
            height: 22
            name: root.mode === "finished" && !root.transfer.failed ? "check_circle"
                  : root.mode === "finished" ? "warning" : "info"
            size: 20
            color: root.mode === "finished" && !root.transfer.failed ? Theme.ok
                   : root.mode === "finished" ? Theme.danger : Theme.textSecondary
        }
        Column {
            anchors.verticalCenter: parent.verticalCenter
            width: lead.width - x
            spacing: 1
            Row {
                width: parent.width
                spacing: 10
                Text {
                    id: titleText
                    width: Math.min(implicitWidth, parent.width - (detailText.visible ? detailText.implicitWidth + 10 : 0))
                    text: root.mode === "running" || root.mode === "finished" ? root.transfer.title
                          : root.mode === "notice" ? root.media.notice
                          : root.mode === "selection" ? root.media.selectionText
                          : root.mode === "deleting" ? "Deleting " + root.transfer.deleting + "…" : ""
                    color: Theme.text
                    font: AppFont.figures(14, 600)
                    elide: Text.ElideRight
                }
                Text {
                    id: detailText
                    visible: root.mode === "finished" && (root.transfer.detail || "").length > 0
                    text: root.transfer.detail || ""
                    color: root.transfer.failed ? Theme.danger : Theme.textSecondary
                    font: AppFont.figures(14, 600)
                }
            }
            Text {
                readonly property string line: root.mode === "running" ? root.transfer.name || ""
                                               : root.mode === "finished" ? root.transfer.error || "" : ""
                visible: line.length > 0
                width: parent.width
                text: line
                color: Theme.textSecondary
                font: AppFont.body(12, 500)
                elide: Text.ElideMiddle
            }
        }
    }

    Row {
        id: actions
        anchors.right: parent.right
        anchors.rightMargin: 14
        anchors.verticalCenter: parent.verticalCenter
        spacing: 10

        // Running
        Text {
            visible: root.mode === "running" && (root.transfer.rate || "").length > 0
            anchors.verticalCenter: parent.verticalCenter
            text: root.transfer.rate || ""
            color: Theme.textSecondary
            font: AppFont.figures(13, 500)
        }
        Text {
            visible: root.mode === "running"
            anchors.verticalCenter: parent.verticalCenter
            width: 46
            text: root.transfer.percent || ""
            color: Theme.text
            font: AppFont.figures(15, 600)
            horizontalAlignment: Text.AlignRight
        }
        MediaButton {
            visible: root.mode === "running"
            text: "Cancel"
            onClicked: root.media.cancelDownloads()
        }

        // Finished
        MediaButton {
            visible: root.mode === "finished" && root.transfer.title !== "Nothing downloaded"
            text: "Open folder"
            glyph: "folder_open"
            onClicked: root.media.openFolder()
        }

        // Selection
        MediaButton {
            visible: root.mode === "selection"
            text: "Download"
            glyph: "file_download"
            kind: "accent"
            onClicked: root.downloadRequested()
        }
        MediaButton {
            visible: root.mode === "selection"
            text: "Delete"
            glyph: "delete_outline"
            kind: "danger"
            onClicked: root.deleteRequested()
        }
        MediaButton {
            visible: root.mode === "selection"
            text: "Clear"
            onClicked: root.media.clearSelection()
        }

        MediaButton {
            visible: root.mode === "finished" || root.mode === "notice"
            glyph: "close"
            tip: "Dismiss"
            onClicked: root.media.dismissTransfer()
        }
    }
}
