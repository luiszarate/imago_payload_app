import QtQuick
import Imago

// One flight of the rail: date, time span, counts, size, and its capture timeline (photos as
// ticks, videos as short bars across the flight's span). On the current flight, pressing or
// dragging along the timeline scrolls the sheet to the capture nearest that moment.
Item {
    id: root

    required property int index
    required property string dateText
    required property string timeText
    required property string countsText
    required property string sizeText
    required property bool hasTime
    required property var photoMarks
    required property var videoMarks
    required property bool current

    signal chosen()
    signal downloadRequested()
    signal scrubbed(real fraction)
    signal scrubEnded()

    implicitHeight: content.implicitHeight + 24

    Accessible.role: Accessible.ListItem
    Accessible.name: dateText + " " + timeText + ", " + countsText
    Accessible.selected: current

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        radius: 12
        color: root.current ? Theme.surfaceRaised : rowMouse.containsMouse ? Theme.alpha(Theme.surfaceRaised, 0.55)
                                                                           : "transparent"
        border.color: root.current ? Theme.alpha(Theme.magenta, 0.35) : "transparent"
    }

    MouseArea {
        id: rowMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.chosen()
    }

    Column {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 22
        anchors.rightMargin: 16
        anchors.topMargin: 12
        spacing: 4

        Item {
            width: parent.width
            height: Math.max(titleRow.implicitHeight, download.height)

            Row {
                id: titleRow
                anchors.left: parent.left
                anchors.right: download.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10
                clip: true
                Text {
                    id: dateLabel
                    text: root.dateText
                    color: root.hasTime ? Theme.text : Theme.textSecondary
                    font: AppFont.display(15, 600)
                }
                Text {
                    visible: root.timeText.length > 0
                    anchors.baseline: dateLabel.baseline
                    text: root.timeText
                    color: Theme.textSecondary
                    font: AppFont.figures(13, 500)
                }
            }
            MediaButton {
                id: download
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                glyph: "file_download"
                tip: "Download flight"
                implicitWidth: 30
                implicitHeight: 30
                opacity: root.current || rowMouse.containsMouse || hovered ? 1 : 0.55
                onClicked: root.downloadRequested()
            }
        }

        Text {
            width: parent.width
            text: root.countsText + (root.sizeText.length > 0 ? "  ·  " + root.sizeText : "")
            color: Theme.textSecondary
            font: AppFont.figures(12, 500)
            elide: Text.ElideRight
        }

        // The capture timeline.
        Item {
            visible: root.hasTime
            width: parent.width
            height: 24

            Canvas {
                id: strip
                anchors.fill: parent
                antialiasing: true
                readonly property color ink: root.current ? Theme.magenta : Theme.textTertiary
                onInkChanged: requestPaint()
                onWidthChanged: requestPaint()
                Connections {
                    target: root
                    function onPhotoMarksChanged() { strip.requestPaint() }
                    function onVideoMarksChanged() { strip.requestPaint() }
                }
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    const w = width, mid = Math.round(height / 2)
                    // Inset by half a video bar, so marks at either end stay whole.
                    const x0 = 5, span = Math.max(1, w - 10)
                    ctx.fillStyle = Qt.rgba(1, 1, 1, 0.10)
                    ctx.fillRect(0, mid, w, 1)
                    ctx.fillStyle = ink
                    for (const at of root.photoMarks)
                        ctx.fillRect(Math.round(x0 + at * span) - 0.75, mid - 6, 1.5, 13)
                    for (const at of root.videoMarks) {
                        ctx.beginPath()
                        ctx.roundedRect(Math.round(x0 + at * span) - 5, mid - 3, 10, 7, 2, 2)
                        ctx.fill()
                    }
                }
            }
            MouseArea {
                anchors.fill: parent
                enabled: root.current
                cursorShape: Qt.SizeHorCursor
                preventStealing: true
                function report(x) { root.scrubbed(Math.max(0, Math.min(1, (x - 5) / Math.max(1, width - 10)))) }
                onPressed: mouse => report(mouse.x)
                onPositionChanged: mouse => { if (pressed) report(mouse.x) }
                onReleased: root.scrubEnded()
                onCanceled: root.scrubEnded()
            }
        }
    }
}
