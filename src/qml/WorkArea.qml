import QtQuick 2.15
import VanishingPoint 1.0

Rectangle {
    color: "#4D4D4D"
    border.color: "#3E3E3E"
    border.width: 1

    property alias canvas: canvas
    property alias controller: canvas.controller

    VpCanvas {
        id: canvas
        anchors.fill: parent
        anchors.margins: parent.border.width
    }
}
