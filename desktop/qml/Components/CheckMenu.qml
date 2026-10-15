import QtQuick
import QtQuick.Controls
import Imago

// A menu of {value, title} options with a check mark on the current one.
Menu {
    id: root

    property var options: []
    property var current
    signal chosen(var value)

    Instantiator {
        model: root.options
        delegate: MenuItem {
            required property var modelData
            text: modelData.title
            checkable: true
            checked: modelData.value === root.current
            font: AppFont.body(14, 600)
            onTriggered: root.chosen(modelData.value)
        }
        onObjectAdded: (index, object) => root.insertItem(index, object)
        onObjectRemoved: (index, object) => root.removeItem(object)
    }
}
