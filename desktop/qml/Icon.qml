import QtQuick

// A Material icon by its name, e.g. "zoom_in" (the font's ligatures).
Text {
    property string name
    property real size: 20

    text: name
    color: Theme.text
    font.family: AppFont.iconFamily
    font.pixelSize: size
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
    Accessible.ignored: true
}
