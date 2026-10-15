import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Imago

Column {
    id: root

    property string title
    property string valueText
    property real value
    property real from: 0
    property real to: 1
    property real stepSize: 0.01
    property string minLabel
    property string maxLabel
    signal moved(real value)

    width: parent ? parent.width : 0
    leftPadding: 14
    rightPadding: 14
    topPadding: 8
    bottomPadding: 4

    RowLayout {
        width: parent.width - 28
        Text { Layout.fillWidth: true; text: root.title; color: Theme.text; font: AppFont.body(15, 600) }
        Text { text: root.valueText; color: Theme.textSecondary; font: AppFont.figures(14, 600) }
    }
    RowLayout {
        width: parent.width - 28
        spacing: 6
        Text { visible: root.minLabel.length > 0; text: root.minLabel; color: Theme.textSecondary; font: AppFont.body(11, 500) }
        Slider {
            Layout.fillWidth: true
            from: root.from
            to: root.to
            stepSize: root.stepSize
            snapMode: Slider.SnapAlways
            value: root.value
            onMoved: root.moved(value)
        }
        Text { visible: root.maxLabel.length > 0; text: root.maxLabel; color: Theme.textSecondary; font: AppFont.body(11, 500) }
    }
}
