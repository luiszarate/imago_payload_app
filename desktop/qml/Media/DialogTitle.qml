import QtQuick
import QtQuick.Controls
import Imago

// The title of a Media dialog, in the app's display face instead of Material's large one.
Label {
    padding: 24
    bottomPadding: 6
    color: Theme.text
    font: AppFont.display(17, 600)
    wrapMode: Text.WordWrap
}
