import QtQuick 2.15
import VanishingPoint 1.0

Rectangle {
    color: "#4d4d4d"
    border.color: "#3e3e3e"
    border.width: 1

    property alias canvas: canvas

    VpCanvas {
        id: canvas
        anchors.fill: parent
        anchors.margins: parent.border.width
    }
}
