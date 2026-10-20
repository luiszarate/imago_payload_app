import QtQuick
import Imago

// A Media dialog's panel: drawn here rather than by Material, whose elevation shadow needs the
// GPU (the headless screenshots draw in software and lost the whole panel).
Rectangle {
    color: Theme.surfaceRaised
    radius: 16
    border.color: Theme.stroke
}
